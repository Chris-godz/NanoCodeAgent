#include <gtest/gtest.h>

#include "state.hpp"
#include "state_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

std::string read_file_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

}  // namespace

class StateStoreTest : public ::testing::Test {
protected:
    fs::path temp_dir;

    void SetUp() override {
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        temp_dir = fs::temp_directory_path() / ("nano_state_store_" + unique);
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::permissions(temp_dir,
                        fs::perms::owner_all,
                        fs::perm_options::replace,
                        ec);
        fs::remove_all(temp_dir, ec);
    }
};

TEST_F(StateStoreTest, JsonFileRoundTrip) {
    JsonFileStateStore store((temp_dir / "session.json").string());

    SessionState session = make_session_state();
    prepare_session_state(session,
                          {"docgen-reviewer", "docgen-fact-check"},
                          nlohmann::json{{"allow_execution_tools", true}});
    seed_session_messages_if_empty(session, "system prompt", "user prompt");
    set_session_scratchpad(session, "working");
    session.turn_index = 2;
    session.counters.llm_turns = 2;
    session.counters.tool_calls_requested = 1;

    ToolCall call;
    call.id = "call_read";
    call.name = "read_file_safe";
    call.arguments = nlohmann::json{{"path", "hello.txt"}};

    const std::size_t record_index = append_tool_call_record(session, 2, call);
    finish_tool_call_record(session, record_index, "ok");
    append_observation_record(session, 2, call.id, call.name, "{\"ok\":true}");

    std::string save_err;
    ASSERT_TRUE(store.save(session, &save_err)) << save_err;

    const StateStoreLoadResult load_result = store.load();
    ASSERT_EQ(load_result.status, StateStoreLoadStatus::Loaded) << load_result.error;

    const SessionState& loaded = load_result.session;
    EXPECT_EQ(loaded.session_id, session.session_id);
    EXPECT_EQ(loaded.turn_index, 2);
    EXPECT_EQ(loaded.counters.llm_turns, 2);
    EXPECT_EQ(loaded.counters.tool_calls_requested, 1);
    EXPECT_EQ(loaded.counters.observations, 1);
    EXPECT_EQ(loaded.scratchpad, "working");
    ASSERT_EQ(loaded.active_skills.size(), 2u);
    EXPECT_EQ(loaded.active_skills[0], "docgen-reviewer");
    EXPECT_EQ(loaded.active_rules_snapshot["allow_execution_tools"], true);
    EXPECT_EQ(loaded.messages.dump(), session.messages.dump());
    ASSERT_EQ(loaded.tool_calls.size(), 1u);
    EXPECT_EQ(loaded.tool_calls[0].tool_name, "read_file_safe");
    EXPECT_EQ(loaded.tool_calls[0].status, "ok");
    ASSERT_EQ(loaded.observations.size(), 1u);
    EXPECT_EQ(loaded.observations[0].content, "{\"ok\":true}");
}

TEST_F(StateStoreTest, ToolCallAppendAndFinish) {
    SessionState session = make_session_state();

    ToolCall call;
    call.id = "call_write";
    call.name = "write_file_safe";
    call.arguments = nlohmann::json{{"path", "note.txt"}, {"content", "hi"}};

    const std::size_t record_index = append_tool_call_record(session, 3, call);
    ASSERT_EQ(session.tool_calls.size(), 1u);
    EXPECT_EQ(session.tool_calls[0].turn_index, 3);
    EXPECT_EQ(session.tool_calls[0].tool_call_id, "call_write");
    EXPECT_EQ(session.tool_calls[0].status, "started");
    EXPECT_FALSE(session.tool_calls[0].started_at.empty());

    finish_tool_call_record(session, record_index, "ok");
    EXPECT_EQ(session.tool_calls[0].status, "ok");
    EXPECT_FALSE(session.tool_calls[0].finished_at.empty());
}

TEST_F(StateStoreTest, ObservationAppend) {
    SessionState session = make_session_state();

    append_observation_record(session, 1, "call_read", "read_file_safe", "{\"ok\":true}");

    ASSERT_EQ(session.observations.size(), 1u);
    EXPECT_EQ(session.counters.observations, 1);
    EXPECT_EQ(session.observations[0].turn_index, 1);
    EXPECT_EQ(session.observations[0].tool_call_id, "call_read");
    EXPECT_EQ(session.observations[0].tool_name, "read_file_safe");
    EXPECT_EQ(session.observations[0].content, "{\"ok\":true}");
    EXPECT_FALSE(session.observations[0].created_at.empty());
}

TEST_F(StateStoreTest, ScratchpadUpdate) {
    SessionState session = make_session_state();

    set_session_scratchpad(session, "turn 1: awaiting assistant response");

    EXPECT_EQ(session.scratchpad, "turn 1: awaiting assistant response");
    EXPECT_FALSE(session.updated_at.empty());
}

TEST_F(StateStoreTest, MissingSessionFileReturnsMissing) {
    JsonFileStateStore store((temp_dir / "missing.json").string());

    const StateStoreLoadResult load_result = store.load();
    EXPECT_EQ(load_result.status, StateStoreLoadStatus::Missing);
    EXPECT_TRUE(load_result.error.empty());
}

TEST_F(StateStoreTest, MalformedSessionFileReturnsError) {
    const fs::path session_path = temp_dir / "broken.json";
    std::ofstream output(session_path, std::ios::binary);
    output << "{\"messages\":{}}";
    output.close();

    JsonFileStateStore store(session_path.string());
    const StateStoreLoadResult load_result = store.load();

    EXPECT_EQ(load_result.status, StateStoreLoadStatus::Error);
    EXPECT_NE(load_result.error.find("Invalid session file"), std::string::npos);
}

TEST_F(StateStoreTest, SaveFailureLeavesPreviousFileIntact) {
    const fs::path session_path = temp_dir / "session.json";
    JsonFileStateStore store(session_path.string());

    SessionState first = make_session_state();
    set_session_scratchpad(first, "before");
    std::string save_err;
    ASSERT_TRUE(store.save(first, &save_err)) << save_err;

    const std::string before_contents = read_file_text(session_path);

    std::error_code perms_ec;
    fs::permissions(temp_dir,
                    fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace,
                    perms_ec);
    ASSERT_FALSE(perms_ec);

    SessionState second = first;
    set_session_scratchpad(second, "after");
    EXPECT_FALSE(store.save(second, &save_err));

    fs::permissions(temp_dir,
                    fs::perms::owner_all,
                    fs::perm_options::replace,
                    perms_ec);
    ASSERT_FALSE(perms_ec);

    const std::string after_contents = read_file_text(session_path);
    EXPECT_EQ(after_contents, before_contents);
}
