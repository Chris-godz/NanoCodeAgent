/// tests/test_apply_patch.cpp
///
/// TDD test suite for apply_patch_single, apply_patch_batch, and the
/// execute_tool / schema integration.
///
/// Tests are grouped:
///   A  – Single patch basic behaviour
///   B  – Safety boundary (path escape, symlink, read failure, binary, truncated)
///   C  – Overlapping-match correctness
///   D  – Batch patch semantics
///   E  – execute_tool / schema integration
///   F  – Error-message distinguishability

#include <gtest/gtest.h>
#include "apply_patch.hpp"
#include "agent_tools.hpp"
#include "tool_call.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>
#include <unistd.h>

namespace fs = std::filesystem;
using json   = nlohmann::json;

// ---------------------------------------------------------------------------
// Fixture helpers
// ---------------------------------------------------------------------------

class ApplyPatchTest : public ::testing::Test {
protected:
    std::string ws;

    void SetUp() override {
        ws = (fs::temp_directory_path() /
              ("nano_ap_" + std::to_string(getpid()) + "_" +
               ::testing::UnitTest::GetInstance()->current_test_info()->name()))
                 .string();
        fs::remove_all(ws);
        fs::create_directories(ws);
    }

    void TearDown() override { fs::remove_all(ws); }

    /// Write a file relative to the test workspace.
    void write(const std::string& rel, const std::string& content) {
        std::ofstream f(ws + "/" + rel);
        f << content;
    }

    /// Read back a file relative to the test workspace.
    std::string read(const std::string& rel) {
        std::ifstream f(ws + "/" + rel);
        return {std::istreambuf_iterator<char>(f), {}};
    }
};

// ---------------------------------------------------------------------------
// Group A – Single patch basic behaviour
// ---------------------------------------------------------------------------

TEST_F(ApplyPatchTest, A1_SingleUniqueHitReplaces) {
    write("a.txt", "hello world");
    auto r = apply_patch_single(ws, "a.txt", "world", "there");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.match_count, 1);
    EXPECT_EQ(r.reject_code, PatchRejectCode::None);
    EXPECT_EQ(r.patch_index, -1);
    EXPECT_EQ(read("a.txt"), "hello there");
}

TEST_F(ApplyPatchTest, A2_OldTextNotFound_Fails) {
    write("a.txt", "hello world");
    auto r = apply_patch_single(ws, "a.txt", "goodbye", "there");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.match_count, 0);
    EXPECT_EQ(r.reject_code, PatchRejectCode::NoMatch);
    EXPECT_EQ(r.patch_index, -1);
    EXPECT_EQ(read("a.txt"), "hello world");  // file unchanged
}

TEST_F(ApplyPatchTest, A3_OldTextAppearsMultipleTimes_Fails) {
    write("a.txt", "abc abc abc");
    auto r = apply_patch_single(ws, "a.txt", "abc", "xyz");
    EXPECT_FALSE(r.ok);
    EXPECT_GT(r.match_count, 1);
    EXPECT_EQ(r.reject_code, PatchRejectCode::MultipleMatches);
    EXPECT_EQ(read("a.txt"), "abc abc abc");  // file unchanged
}

TEST_F(ApplyPatchTest, A4_OldTextEmpty_Fails) {
    write("a.txt", "hello");
    auto r = apply_patch_single(ws, "a.txt", "", "anything");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.match_count, 0);
    EXPECT_EQ(r.reject_code, PatchRejectCode::EmptyOldText);
    EXPECT_NE(r.err.find("empty"), std::string::npos);
    EXPECT_EQ(read("a.txt"), "hello");
}

TEST_F(ApplyPatchTest, A5_NewTextEmptyString_DeletesMatch) {
    write("a.txt", "hello world");
    auto r = apply_patch_single(ws, "a.txt", " world", "");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(read("a.txt"), "hello");
}

TEST_F(ApplyPatchTest, A6_WholeFileReplacement) {
    write("a.txt", "entire content");
    auto r = apply_patch_single(ws, "a.txt", "entire content", "new stuff");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(read("a.txt"), "new stuff");
}

TEST_F(ApplyPatchTest, A7_NoOpReplacement_Succeeds) {
    write("a.txt", "same text");
    auto r = apply_patch_single(ws, "a.txt", "same text", "same text");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(read("a.txt"), "same text");
}

// ---------------------------------------------------------------------------
// Group B – Safety boundaries
// ---------------------------------------------------------------------------

TEST_F(ApplyPatchTest, B8_PathEscape_Fails) {
    // "../escape.txt" should be rejected by workspace_resolve inside read_file_safe
    auto r = apply_patch_single(ws, "../escape.txt", "x", "y");
    EXPECT_FALSE(r.ok);
    // err should mention something about escaping or boundary
    EXPECT_FALSE(r.err.empty());
}

TEST_F(ApplyPatchTest, B9_Symlink_Fails) {
    // Create a real file outside workspace, then symlink into workspace
    const std::string outside = ws + "_outside.txt";
    std::ofstream f(outside);
    f << "secret";
    f.close();

    std::error_code ec;
    fs::create_symlink(outside, ws + "/link.txt", ec);
    if (ec) {
        fs::remove(outside, ec);
        GTEST_SKIP() << "symlink not supported on this platform/filesystem: " << ec.message();
    }

    auto r = apply_patch_single(ws, "link.txt", "secret", "patched");
    EXPECT_FALSE(r.ok);

    fs::remove(outside);
}

TEST_F(ApplyPatchTest, B10_FileNotExist_Fails) {
    auto r = apply_patch_single(ws, "nonexistent.txt", "x", "y");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.err.empty());
}

TEST_F(ApplyPatchTest, B11_BinaryFile_Fails) {
    // Write a file with null bytes (binary)
    {
        std::ofstream f(ws + "/bin.bin", std::ios::binary);
        const char data[] = {'\x00', '\x01', '\x02', '\xff', '\xfe'};
        f.write(data, sizeof(data));
    }
    auto r = apply_patch_single(ws, "bin.bin", std::string("\x00\x01", 2), "xy");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.reject_code, PatchRejectCode::BinaryFile);
    EXPECT_FALSE(r.err.empty());
}

TEST_F(ApplyPatchTest, B12_TruncatedRead_FailsAndFileUnchanged) {
    // Write a file just over the internal 4 MiB read limit to trigger truncation.
    // Contract: a truncated read must be rejected and must not modify the file.
    const size_t over_limit = 4 * 1024 * 1024 + 1;
    write("huge.txt", std::string(over_limit, 'z'));
    const std::string original = read("huge.txt");

    auto r = apply_patch_single(ws, "huge.txt", "zzz", "aaa");
    EXPECT_FALSE(r.ok) << "Truncated read must be rejected";
    EXPECT_EQ(r.reject_code, PatchRejectCode::TruncatedFile);
    EXPECT_NE(r.err.find("truncat"), std::string::npos) << "Error must mention truncation";
    EXPECT_EQ(read("huge.txt"), original) << "File must be unchanged after truncated read";
}

// ---------------------------------------------------------------------------
// Group C – Overlapping-match correctness
// ---------------------------------------------------------------------------

TEST_F(ApplyPatchTest, C13_OverlappingMatch_AbaInAbaba_IsMultiHit) {
    // "aba" occurs at positions 0 and 2 in "ababa" (overlapping)
    write("c.txt", "ababa");
    auto r = apply_patch_single(ws, "c.txt", "aba", "x");
    EXPECT_FALSE(r.ok);
    EXPECT_GT(r.match_count, 1);
    EXPECT_EQ(read("c.txt"), "ababa");
}

TEST_F(ApplyPatchTest, C14_OverlappingMatch_aaInAaa_IsMultiHit) {
    // "aa" occurs at positions 0 and 1 in "aaa" (overlapping)
    write("c.txt", "aaa");
    auto r = apply_patch_single(ws, "c.txt", "aa", "x");
    EXPECT_FALSE(r.ok);
    EXPECT_GT(r.match_count, 1);
    EXPECT_EQ(read("c.txt"), "aaa");
}

// ---------------------------------------------------------------------------
// Group D – Batch patch semantics
// ---------------------------------------------------------------------------

TEST_F(ApplyPatchTest, D15_BatchMultiplePatchesInOrder) {
    write("d.txt", "aaa bbb ccc");
    std::vector<PatchEntry> patches = {
        {"aaa", "AAA"},
        {"bbb", "BBB"},
        {"ccc", "CCC"}
    };
    auto r = apply_patch_batch(ws, "d.txt", patches);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(read("d.txt"), "AAA BBB CCC");
}

TEST_F(ApplyPatchTest, D16_BatchSecondPatchFails_FileUnchanged) {
    write("d.txt", "aaa bbb ccc");
    std::vector<PatchEntry> patches = {
        {"aaa", "AAA"},
        {"MISSING", "X"}  // won't match
    };
    auto r = apply_patch_batch(ws, "d.txt", patches);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.reject_code, PatchRejectCode::NoMatch);
    EXPECT_EQ(r.patch_index, 1);
    EXPECT_EQ(r.match_count, 1);
    EXPECT_EQ(read("d.txt"), "aaa bbb ccc");  // unchanged
}

TEST_F(ApplyPatchTest, D17_BatchOrderDependency_Succeeds) {
    write("d.txt", "hello world");
    // Second patch depends on the result of the first
    std::vector<PatchEntry> patches = {
        {"hello", "goodbye"},
        {"goodbye world", "farewell"}
    };
    auto r = apply_patch_batch(ws, "d.txt", patches);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(read("d.txt"), "farewell");
}

TEST_F(ApplyPatchTest, D18_BatchEmptyArray_Fails) {
    write("d.txt", "content");
    auto r = apply_patch_batch(ws, "d.txt", {});
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.err.find("empty"), std::string::npos);
    EXPECT_EQ(read("d.txt"), "content");
}

TEST_F(ApplyPatchTest, D19_BatchEntryOldTextEmpty_Fails) {
    write("d.txt", "aaa");
    std::vector<PatchEntry> patches = {
        {"", "something"}
    };
    auto r = apply_patch_batch(ws, "d.txt", patches);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.reject_code, PatchRejectCode::InvalidBatchEntry);
    EXPECT_EQ(r.patch_index, 0);
    EXPECT_EQ(read("d.txt"), "aaa");
}

TEST_F(ApplyPatchTest, D20_BatchEntryMultipleMatches_Fails) {
    write("d.txt", "abc abc");
    std::vector<PatchEntry> patches = {
        {"abc", "xyz"}
    };
    auto r = apply_patch_batch(ws, "d.txt", patches);
    EXPECT_FALSE(r.ok);
    // batch match_count = completed patch count (0-based index of failure)
    EXPECT_EQ(r.match_count, 0);
    EXPECT_EQ(r.reject_code, PatchRejectCode::MultipleMatches);
    EXPECT_EQ(r.patch_index, 0);
    EXPECT_EQ(read("d.txt"), "abc abc");
}

TEST_F(ApplyPatchTest, D21_BatchEntryNoMatch_Fails) {
    write("d.txt", "hello");
    std::vector<PatchEntry> patches = {
        {"NOTHERE", "x"}
    };
    auto r = apply_patch_batch(ws, "d.txt", patches);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.match_count, 0);
    EXPECT_EQ(r.reject_code, PatchRejectCode::NoMatch);
    EXPECT_EQ(r.patch_index, 0);
    EXPECT_EQ(read("d.txt"), "hello");
}

TEST_F(ApplyPatchTest, D22_BatchTruncatedRead_FailsFileUnchanged) {
    // Same reasoning as B12: write a file just over the internal 4 MiB read limit.
    // Contract: truncated read must be rejected and must not modify the file.
    const size_t over_limit = 4 * 1024 * 1024 + 1;
    write("huge_batch.txt", std::string(over_limit, 'q'));
    const std::string original = read("huge_batch.txt");

    std::vector<PatchEntry> patches = {{"qqq", "rrr"}};
    auto r = apply_patch_batch(ws, "huge_batch.txt", patches);
    EXPECT_FALSE(r.ok) << "Truncated read must be rejected";
    EXPECT_EQ(r.reject_code, PatchRejectCode::TruncatedFile);
    EXPECT_NE(r.err.find("truncat"), std::string::npos) << "Error must mention truncation";
    EXPECT_EQ(read("huge_batch.txt"), original) << "File must be unchanged after truncated read";
}

TEST_F(ApplyPatchTest, D23_BatchNewTextEmpty_DeletesMatch) {
    write("d.txt", "hello beautiful world");
    std::vector<PatchEntry> patches = {
        {" beautiful", ""}
    };
    auto r = apply_patch_batch(ws, "d.txt", patches);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(read("d.txt"), "hello world");
}

TEST_F(ApplyPatchTest, D24_BatchFailureReportsCompletedPatchCountNotInnerMatchCount) {
    // patch[0] old_text matches 2 times → inner match_count would be 2.
    // Correct batch semantics: match_count = 0 (completed patches before failure).
    write("d.txt", "dup dup end");
    std::vector<PatchEntry> patches = {
        {"dup", "DUP"},  // matches 2 times → must fail
        {"end", "END"}   // never reached
    };
    auto r = apply_patch_batch(ws, "d.txt", patches);
    EXPECT_FALSE(r.ok);
    // Must be 0 (index of failing patch / completed count), NOT 2 (inner matches).
    EXPECT_EQ(r.match_count, 0);
    EXPECT_EQ(r.reject_code, PatchRejectCode::MultipleMatches);
    EXPECT_EQ(r.patch_index, 0);
    EXPECT_NE(r.err.find("patch[0]"), std::string::npos);
    // File must remain unchanged.
    EXPECT_EQ(read("d.txt"), "dup dup end");
}

// ---------------------------------------------------------------------------
// Group E – execute_tool / schema integration
// ---------------------------------------------------------------------------

class ApplyPatchToolTest : public ::testing::Test {
protected:
    std::string ws;
    AgentConfig config;

    void SetUp() override {
        ws = (fs::temp_directory_path() /
              ("nano_ap_tool_" + std::to_string(getpid()) + "_" +
               ::testing::UnitTest::GetInstance()->current_test_info()->name()))
                 .string();
        fs::remove_all(ws);
        fs::create_directories(ws);
        config.workspace_abs = ws;
        config.allow_mutating_tools = true;
    }

    void TearDown() override { fs::remove_all(ws); }

    void write_file(const std::string& rel, const std::string& content) {
        std::ofstream f(ws + "/" + rel);
        f << content;
    }

    std::string read_file(const std::string& rel) {
        std::ifstream f(ws + "/" + rel);
        return {std::istreambuf_iterator<char>(f), {}};
    }
};

TEST_F(ApplyPatchToolTest, E24_SchemaContainsApplyPatch) {
    json schema = get_agent_tools_schema();
    bool found = false;
    for (const auto& tool : schema) {
        if (tool["function"]["name"] == "apply_patch") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ApplyPatchToolTest, E25_ExecuteToolSinglePatch_Succeeds) {
    write_file("e.txt", "foo bar");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "e.txt"},
        {"old_text", "bar"},
        {"new_text", "baz"}
    };

    std::string res = execute_tool(tc, config);
    const json out = json::parse(res);
    EXPECT_TRUE(out["ok"].get<bool>());
    EXPECT_EQ(out["reject_code"], "none");
    EXPECT_FALSE(out.contains("patch_index"));
    EXPECT_EQ(read_file("e.txt"), "foo baz");
}

TEST_F(ApplyPatchToolTest, E26_ExecuteToolBatchPatch_Succeeds) {
    write_file("e.txt", "alpha beta gamma");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "e.txt"},
        {"patches", json::array({
            {{"old_text", "alpha"}, {"new_text", "ALPHA"}},
            {{"old_text", "gamma"}, {"new_text", "GAMMA"}}
        })}
    };

    std::string res = execute_tool(tc, config);
    const json out = json::parse(res);
    EXPECT_TRUE(out["ok"].get<bool>());
    EXPECT_EQ(out["reject_code"], "none");
    EXPECT_FALSE(out.contains("patch_index"));
    EXPECT_EQ(read_file("e.txt"), "ALPHA beta GAMMA");
}

TEST_F(ApplyPatchToolTest, E27_MissingPath_Fails) {
    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = json::object();

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(res.find("path"), std::string::npos);
}

TEST_F(ApplyPatchToolTest, E28_SingleModeMissingOldText_Fails) {
    write_file("e.txt", "content");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "e.txt"},
        {"new_text", "something"}
        // no old_text
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(res.find("old_text"), std::string::npos);
}

TEST_F(ApplyPatchToolTest, E29_SingleModeMissingNewText_Fails) {
    write_file("e.txt", "content");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "e.txt"},
        {"old_text", "content"}
        // no new_text — must NOT default to ""
    };

    std::string res = execute_tool(tc, config);
    const json out = json::parse(res);
    EXPECT_FALSE(out["ok"].get<bool>());
    // Must NOT silently treat as deletion
    EXPECT_NE(out["error"].get<std::string>().find("new_text"), std::string::npos);
    EXPECT_FALSE(out.contains("reject_code"));
    // File must be unchanged
    EXPECT_EQ(read_file("e.txt"), "content");
}

TEST_F(ApplyPatchToolTest, E30_PatchesNotArray_Fails) {
    write_file("e.txt", "content");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "e.txt"},
        {"patches", "not an array"}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(res.find("array"), std::string::npos);
}

TEST_F(ApplyPatchToolTest, E31_MixedMode_PatchesAndOldText_Fails) {
    write_file("e.txt", "content");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "e.txt"},
        {"old_text", "content"},
        {"new_text", "replaced"},
        {"patches", json::array({
            {{"old_text", "content"}, {"new_text", "replaced"}}
        })}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos);
    // Must explicitly mention the conflict
    const bool mentions_conflict =
        res.find("Ambiguous") != std::string::npos ||
        res.find("mixed") != std::string::npos ||
        res.find("cannot be combined") != std::string::npos;
    EXPECT_TRUE(mentions_conflict) << "Error should mention mode conflict, got: " << res;
    EXPECT_EQ(read_file("e.txt"), "content");
}

// ---------------------------------------------------------------------------
// Group F – Error message distinguishability
// ---------------------------------------------------------------------------

TEST_F(ApplyPatchTest, F33_DistinctErrorMessages) {
    write("f.txt", "hello hello world");

    // no match
    auto no_match = apply_patch_single(ws, "f.txt", "NOTHERE", "x");
    EXPECT_FALSE(no_match.ok);
    EXPECT_EQ(no_match.match_count, 0);
    EXPECT_EQ(no_match.reject_code, PatchRejectCode::NoMatch);

    // multiple match
    auto multi_match = apply_patch_single(ws, "f.txt", "hello", "x");
    EXPECT_FALSE(multi_match.ok);
    EXPECT_GT(multi_match.match_count, 1);
    EXPECT_EQ(multi_match.reject_code, PatchRejectCode::MultipleMatches);

    // empty old_text
    auto empty_old = apply_patch_single(ws, "f.txt", "", "x");
    EXPECT_FALSE(empty_old.ok);
    EXPECT_EQ(empty_old.reject_code, PatchRejectCode::EmptyOldText);

    // Verify the three errors are distinguishable
    EXPECT_NE(no_match.err, multi_match.err);
    EXPECT_NE(no_match.err, empty_old.err);
    EXPECT_NE(multi_match.err, empty_old.err);
}

TEST_F(ApplyPatchToolTest, F34_MissingNewText_Not_SameAs_EmptyNewText) {
    write_file("f.txt", "original content");

    // Case A: new_text field is absent -> must fail
    ToolCall missing_tc;
    missing_tc.name = "apply_patch";
    missing_tc.arguments = {
        {"path", "f.txt"},
        {"old_text", "original content"}
        // new_text deliberately absent
    };
    std::string missing_res = execute_tool(missing_tc, config);
    const json missing_out = json::parse(missing_res);
    EXPECT_FALSE(missing_out["ok"].get<bool>());
    EXPECT_FALSE(missing_out.contains("reject_code"));
    EXPECT_EQ(read_file("f.txt"), "original content");  // file unchanged

    // Case B: new_text is explicit empty string -> succeeds (deletion)
    ToolCall empty_tc;
    empty_tc.name = "apply_patch";
    empty_tc.arguments = {
        {"path", "f.txt"},
        {"old_text", "original content"},
        {"new_text", ""}
    };
    std::string empty_res = execute_tool(empty_tc, config);
    const json empty_out = json::parse(empty_res);
    EXPECT_TRUE(empty_out["ok"].get<bool>());
    EXPECT_EQ(empty_out["reject_code"], "none");
    EXPECT_EQ(read_file("f.txt"), "");  // deleted
}

TEST_F(ApplyPatchToolTest, F35_ToolRejectCodeForSinglePatchFailure) {
    write_file("f.txt", "hello world");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"old_text", "NOTHERE"},
        {"new_text", "x"}
    };

    const json out = json::parse(execute_tool(tc, config));
    EXPECT_FALSE(out["ok"].get<bool>());
    EXPECT_EQ(out["reject_code"], "no_match");
    EXPECT_EQ(out["match_count"], 0);
    EXPECT_FALSE(out.contains("patch_index"));
    EXPECT_EQ(read_file("f.txt"), "hello world");
}

TEST_F(ApplyPatchToolTest, F36_ToolRejectCodeAndIndexForBatchFailure) {
    write_file("f.txt", "alpha beta gamma");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"patches", json::array({
            {{"old_text", "alpha"}, {"new_text", "ALPHA"}},
            {{"old_text", "MISSING"}, {"new_text", "X"}}
        })}
    };

    const json out = json::parse(execute_tool(tc, config));
    EXPECT_FALSE(out["ok"].get<bool>());
    EXPECT_EQ(out["reject_code"], "no_match");
    EXPECT_EQ(out["patch_index"], 1);
    EXPECT_EQ(out["match_count"], 1);
    EXPECT_EQ(read_file("f.txt"), "alpha beta gamma");
}

// ---------------------------------------------------------------------------
// Group G – Type-safety: batch entry validation and binary/truncated errors
// ---------------------------------------------------------------------------

/// G35: patches[i] is not an object must return a structured error, not an
/// exception. The error message must reference the failing index.
TEST_F(ApplyPatchToolTest, G35_BatchEntryNotObject_StructuredError) {
    write_file("g.txt", "hello");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "g.txt"},
        {"patches", json::array({"not-an-object"})}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos);
    // Must be a structured apply_patch error, not a generic exception
    EXPECT_EQ(res.find("Exception"), std::string::npos)
        << "Should not produce generic exception response, got: " << res;
    EXPECT_NE(res.find("patches[0]"), std::string::npos)
        << "Error should reference the failing index, got: " << res;
    EXPECT_NE(res.find("object"), std::string::npos)
        << "Error should mention 'object', got: " << res;
}

/// G36: patches[i].old_text present but not a string must return a structured
/// error referencing the index and field name.
TEST_F(ApplyPatchToolTest, G36_BatchEntryOldTextNotString_StructuredError) {
    write_file("g.txt", "hello");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "g.txt"},
        {"patches", json::array({
            {{"old_text", 42}, {"new_text", "world"}}
        })}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos);
    EXPECT_EQ(res.find("Exception"), std::string::npos)
        << "Should not produce generic exception response, got: " << res;
    EXPECT_NE(res.find("patches[0]"), std::string::npos)
        << "Error should reference the failing index, got: " << res;
    EXPECT_NE(res.find("old_text"), std::string::npos)
        << "Error should mention the failing field, got: " << res;
}

/// G37: patches[i].new_text present but not a string must return a structured
/// error referencing the index and field name.
TEST_F(ApplyPatchToolTest, G37_BatchEntryNewTextNotString_StructuredError) {
    write_file("g.txt", "hello");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "g.txt"},
        {"patches", json::array({
            {{"old_text", "hello"}, {"new_text", 99}}
        })}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos);
    EXPECT_EQ(res.find("Exception"), std::string::npos)
        << "Should not produce generic exception response, got: " << res;
    EXPECT_NE(res.find("patches[0]"), std::string::npos)
        << "Error should reference the failing index, got: " << res;
    EXPECT_NE(res.find("new_text"), std::string::npos)
        << "Error should mention the failing field, got: " << res;
}

/// G38: single-mode old_text present but not a string must return a structured
/// error, not a generic exception.
TEST_F(ApplyPatchToolTest, G38_SingleModeOldTextNotString_StructuredError) {
    write_file("g.txt", "hello");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "g.txt"},
        {"old_text", 123},
        {"new_text", "world"}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos);
    EXPECT_EQ(res.find("Exception"), std::string::npos)
        << "Should not produce generic exception response, got: " << res;
    EXPECT_NE(res.find("old_text"), std::string::npos)
        << "Error should mention the failing field, got: " << res;
}

/// G39: single-mode new_text present but not a string must return a structured
/// error, not a generic exception.
TEST_F(ApplyPatchToolTest, G39_SingleModeNewTextNotString_StructuredError) {
    write_file("g.txt", "hello");

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "g.txt"},
        {"old_text", "hello"},
        {"new_text", false}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos);
    EXPECT_EQ(res.find("Exception"), std::string::npos)
        << "Should not produce generic exception response, got: " << res;
    EXPECT_NE(res.find("new_text"), std::string::npos)
        << "Error should mention the failing field, got: " << res;
}

/// G40: applying a patch to a binary file (single mode) must return the
/// specialised "binary" error, NOT the generic "failed to read file" message.
/// (Regression guard for the is_binary / !rr.ok ordering fix.)
TEST_F(ApplyPatchTest, G40_SingleBinaryFile_ReturnsBinarySpecificError) {
    {
        std::ofstream f(ws + "/g_bin.bin", std::ios::binary);
        const char data[] = {'\x00', '\x01', 'a', 'b'};
        f.write(data, sizeof(data));
    }

    auto r = apply_patch_single(ws, "g_bin.bin", "ab", "xy");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.reject_code, PatchRejectCode::BinaryFile);
    EXPECT_NE(r.err.find("binary"), std::string::npos)
        << "Expected 'binary' in error message, got: " << r.err;
}

/// G41: applying a patch to a binary file (batch mode) must return the
/// specialised "binary" error, NOT the generic "failed to read file" message.
TEST_F(ApplyPatchTest, G41_BatchBinaryFile_ReturnsBinarySpecificError) {
    {
        std::ofstream f(ws + "/g_bin2.bin", std::ios::binary);
        const char data[] = {'\x00', '\x01', 'a', 'b'};
        f.write(data, sizeof(data));
    }

    std::vector<PatchEntry> patches = {{"ab", "xy"}};
    auto r = apply_patch_batch(ws, "g_bin2.bin", patches);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.reject_code, PatchRejectCode::BinaryFile);
    EXPECT_NE(r.err.find("binary"), std::string::npos)
        << "Expected 'binary' in error message, got: " << r.err;
}
