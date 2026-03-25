#include <gtest/gtest.h>

#include "agent_tools.hpp"
#include "mcp.hpp"
#include "state_store.hpp"

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

fs::path fake_server_script() {
    return fs::path(__FILE__).parent_path() / "testdata" / "fake_mcp_server.py";
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted += "'";
    return quoted;
}

std::string make_server_spec(const std::string& name, const std::string& scenario, const std::string& extra_arg = "") {
    std::string spec = name + "=python3 " + shell_quote(fake_server_script().string()) + " " + scenario;
    if (!extra_arg.empty()) {
        spec += " " + shell_quote(extra_arg);
    }
    return spec;
}

McpClientOptions fast_options() {
    McpClientOptions options;
    options.request_timeout_ms = 250;
    options.write_timeout_ms = 250;
    options.max_message_bytes = 128 * 1024;
    return options;
}

std::string read_text_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(McpIntegrationTest, SessionStateUpdatesAfterMcpInteractions) {
    SessionState session_state = make_session_state();
    McpSession mcp_session(fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(mcp_session.start({make_server_spec("alpha", "paged_tools")}, &session_state, &err)) << err;
    ASSERT_EQ(session_state.mcp_servers.size(), 1u);
    EXPECT_EQ(session_state.mcp_servers[0].server_name, "alpha");
    EXPECT_EQ(session_state.mcp_servers[0].negotiated_protocol_version, "2024-11-05");
    ASSERT_EQ(session_state.mcp_servers[0].tool_cache.size(), 4u);

    ToolRegistry registry = get_default_tool_registry();
    McpToolBridge bridge(&mcp_session);
    ASSERT_TRUE(bridge.register_tools(&registry, &err)) << err;

    ToolCall call;
    call.id = "mcp-call-1";
    call.name = "mcp.alpha.adder";
    call.arguments = nlohmann::json{{"a", 10}, {"b", 7}};

    AgentConfig config;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;

    ToolExecutionContext context;
    context.session_state = &session_state;
    context.turn_index = 4;
    context.tool_call_id = call.id;

    const nlohmann::json result = nlohmann::json::parse(registry.execute(call, config, &context));
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["result"]["structuredContent"]["sum"], 17);

    ASSERT_EQ(session_state.mcp_tool_call_observations.size(), 1u);
    EXPECT_EQ(session_state.mcp_tool_call_observations[0].turn_index, 4);
    EXPECT_EQ(session_state.mcp_tool_call_observations[0].tool_call_id, "mcp-call-1");
    EXPECT_EQ(session_state.mcp_tool_call_observations[0].server_name, "alpha");
    EXPECT_EQ(session_state.mcp_tool_call_observations[0].tool_name, "adder");
    EXPECT_EQ(session_state.mcp_tool_call_observations[0].status, "ok");
}

TEST(McpIntegrationTest, McpToolsRespectApprovalPolicyAndDoNotBypassRuntimeGuards) {
    SessionState session_state = make_session_state();
    McpSession mcp_session(fs::temp_directory_path().string(), fast_options());
    const fs::path call_log = fs::temp_directory_path() / "nano_mcp_blocked_call_log.txt";
    std::error_code remove_ec;
    fs::remove(call_log, remove_ec);
    std::string err;

    ASSERT_TRUE(mcp_session.start({make_server_spec("alpha", "record_calls", call_log.string())}, &session_state, &err))
        << err;

    ToolRegistry registry = get_default_tool_registry();
    McpToolBridge bridge(&mcp_session);
    ASSERT_TRUE(bridge.register_tools(&registry, &err)) << err;

    AgentConfig blocked_config;
    const nlohmann::json schema = registry.to_openai_schema(blocked_config);
    bool found_mcp_tool = false;
    for (const auto& item : schema) {
        if (item["function"]["name"] == "mcp.alpha.adder") {
            found_mcp_tool = true;
            break;
        }
    }
    EXPECT_FALSE(found_mcp_tool);

    ToolCall call;
    call.id = "blocked-call";
    call.name = "mcp.alpha.adder";
    call.arguments = nlohmann::json{{"a", 1}, {"b", 2}};

    const nlohmann::json result = nlohmann::json::parse(registry.execute(call, blocked_config));
    EXPECT_EQ(result["status"], "blocked");
    EXPECT_EQ(result["missing_approvals"], nlohmann::json::array({"mutating", "execution"}));
    EXPECT_FALSE(fs::exists(call_log));

    AgentConfig allowed_config;
    allowed_config.allow_mutating_tools = true;
    allowed_config.allow_execution_tools = true;

    ToolExecutionContext context;
    context.session_state = &session_state;
    context.turn_index = 7;
    context.tool_call_id = call.id;

    const nlohmann::json allowed_result = nlohmann::json::parse(registry.execute(call, allowed_config, &context));
    EXPECT_TRUE(allowed_result["ok"].get<bool>());
    ASSERT_TRUE(fs::exists(call_log));
    EXPECT_NE(read_text_file(call_log).find("adder"), std::string::npos);
}

TEST(McpIntegrationTest, McpToolOutputRespectsOutputCap) {
    SessionState session_state = make_session_state();
    McpSession mcp_session(fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(mcp_session.start({make_server_spec("alpha", "success")}, &session_state, &err)) << err;

    ToolRegistry registry = get_default_tool_registry();
    McpToolBridge bridge(&mcp_session);
    ASSERT_TRUE(bridge.register_tools(&registry, &err)) << err;

    ToolCall call;
    call.id = "mcp-call-2";
    call.name = "mcp.alpha.big_text";
    call.arguments = nlohmann::json{{"size", 4096}};

    AgentConfig config;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_tool_output_bytes = 256;

    ToolExecutionContext context;
    context.session_state = &session_state;
    context.turn_index = 5;
    context.tool_call_id = call.id;

    const nlohmann::json result = nlohmann::json::parse(registry.execute(call, config, &context));
    EXPECT_TRUE(result["truncated"].get<bool>());
    EXPECT_TRUE(result.contains("result_preview"));
}

TEST(McpIntegrationTest, McpMetadataPersistsThroughStateStoreRoundTrip) {
    const fs::path session_path = fs::temp_directory_path() / "nano_mcp_session_roundtrip.json";
    std::error_code remove_ec;
    fs::remove(session_path, remove_ec);

    JsonFileStateStore store(session_path.string());
    SessionState session_state = make_session_state();
    McpSession mcp_session(fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(mcp_session.start({make_server_spec("alpha", "success")}, &session_state, &err)) << err;

    ToolRegistry registry = get_default_tool_registry();
    McpToolBridge bridge(&mcp_session);
    ASSERT_TRUE(bridge.register_tools(&registry, &err)) << err;

    ToolCall call;
    call.id = "persist-call";
    call.name = "mcp.alpha.echo";
    call.arguments = nlohmann::json{{"text", "hello"}};

    AgentConfig config;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;

    ToolExecutionContext context;
    context.session_state = &session_state;
    context.turn_index = 2;
    context.tool_call_id = call.id;

    const nlohmann::json result = nlohmann::json::parse(registry.execute(call, config, &context));
    EXPECT_TRUE(result["ok"].get<bool>());

    std::string save_err;
    ASSERT_TRUE(store.save(session_state, &save_err)) << save_err;

    const std::string session_file_text = read_text_file(session_path);
    EXPECT_EQ(session_file_text.find("\"command\""), std::string::npos);
    EXPECT_EQ(session_file_text.find(fake_server_script().string()), std::string::npos);
    EXPECT_EQ(session_file_text.find("python3"), std::string::npos);

    const StateStoreLoadResult load_result = store.load();
    ASSERT_EQ(load_result.status, StateStoreLoadStatus::Loaded) << load_result.error;
    ASSERT_EQ(load_result.session.mcp_servers.size(), 1u);
    EXPECT_EQ(load_result.session.mcp_servers[0].server_name, "alpha");
    ASSERT_EQ(load_result.session.mcp_servers[0].tool_cache.size(), 3u);
    ASSERT_EQ(load_result.session.mcp_tool_call_observations.size(), 1u);
    EXPECT_EQ(load_result.session.mcp_tool_call_observations[0].tool_call_id, "persist-call");
    EXPECT_EQ(load_result.session.mcp_tool_call_observations[0].server_name, "alpha");
    EXPECT_EQ(load_result.session.mcp_tool_call_observations[0].tool_name, "echo");
}
