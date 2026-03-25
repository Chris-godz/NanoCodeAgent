#include <gtest/gtest.h>

#include "mcp.hpp"

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

McpServerSpec make_spec(const std::string& name, const std::string& scenario, const std::string& extra_arg) {
    return McpServerSpec{
        name,
        extra_arg.empty()
            ? std::vector<std::string>{"python3", fake_server_script().string(), scenario}
            : std::vector<std::string>{"python3", fake_server_script().string(), scenario, extra_arg}
    };
}

McpServerSpec make_spec(const std::string& name, const std::string& scenario) {
    return make_spec(name, scenario, "");
}

McpClientOptions fast_options() {
    McpClientOptions options;
    options.request_timeout_ms = 250;
    options.write_timeout_ms = 250;
    options.max_message_bytes = 128 * 1024;
    return options;
}

}  // namespace

TEST(McpTransportTest, FakeServerInitializeSuccess) {
    McpClient client(make_spec("alpha", "success"), fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(client.start(&err)) << err;

    McpServerInfo info;
    ASSERT_TRUE(client.initialize(&info, &err)) << err;
    EXPECT_EQ(info.server_name, "alpha");
    EXPECT_EQ(info.negotiated_protocol_version, "2024-11-05");
    EXPECT_TRUE(info.capabilities.contains("tools"));
}

TEST(McpTransportTest, ServerSpecIsExecutedWithoutShellExpansion) {
    const fs::path shell_marker = fs::temp_directory_path() / "nano_mcp_shell_marker";
    std::error_code remove_ec;
    fs::remove(shell_marker, remove_ec);

    const std::string raw_spec = "alpha=python3 " + shell_quote(fake_server_script().string()) +
                                 " success ;touch " + shell_quote(shell_marker.string());
    std::string err;
    auto parsed = parse_mcp_server_spec(raw_spec, &err);
    ASSERT_TRUE(parsed.has_value()) << err;

    McpClient client(*parsed, fs::temp_directory_path().string(), fast_options());
    ASSERT_TRUE(client.start(&err)) << err;

    McpServerInfo info;
    ASSERT_TRUE(client.initialize(&info, &err)) << err;
    EXPECT_FALSE(fs::exists(shell_marker));
}

TEST(McpTransportTest, ToolsListSuccess) {
    McpClient client(make_spec("alpha", "success"), fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(client.start(&err)) << err;
    McpServerInfo info;
    ASSERT_TRUE(client.initialize(&info, &err)) << err;

    std::vector<McpToolInfo> tools;
    ASSERT_TRUE(client.list_tools(&tools, &err)) << err;
    ASSERT_EQ(tools.size(), 3u);
    EXPECT_EQ(tools[0].remote_name, "adder");
    EXPECT_EQ(tools[0].local_name, "mcp.alpha.adder");
    EXPECT_EQ(tools[1].remote_name, "echo");
}

TEST(McpTransportTest, ToolsCallSuccess) {
    McpClient client(make_spec("alpha", "success"), fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(client.start(&err)) << err;
    McpServerInfo info;
    ASSERT_TRUE(client.initialize(&info, &err)) << err;

    nlohmann::json result;
    ASSERT_TRUE(client.call_tool("adder", nlohmann::json{{"a", 2}, {"b", 5}}, &result, &err)) << err;
    EXPECT_EQ(result["structuredContent"]["sum"], 7);
    EXPECT_EQ(result["content"][0]["text"], "7");
}

TEST(McpTransportTest, MalformedStdoutJsonFails) {
    McpClient client(make_spec("alpha", "malformed_json"), fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(client.start(&err)) << err;
    McpServerInfo info;
    EXPECT_FALSE(client.initialize(&info, &err));
    EXPECT_NE(err.find("Malformed JSON"), std::string::npos);
    EXPECT_NE(err.find("about to send malformed json"), std::string::npos);
}

TEST(McpTransportTest, StderrNoiseIsIgnored) {
    McpClient client(make_spec("alpha", "stderr_noise"), fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(client.start(&err)) << err;
    McpServerInfo info;
    ASSERT_TRUE(client.initialize(&info, &err)) << err;
    std::vector<McpToolInfo> tools;
    ASSERT_TRUE(client.list_tools(&tools, &err)) << err;
    EXPECT_EQ(tools.size(), 3u);
}

TEST(McpTransportTest, InitializedNotificationPrecedesToolsList) {
    McpClient client(make_spec("alpha", "lifecycle_order"), fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(client.start(&err)) << err;
    McpServerInfo info;
    ASSERT_TRUE(client.initialize(&info, &err)) << err;

    std::vector<McpToolInfo> tools;
    ASSERT_TRUE(client.list_tools(&tools, &err)) << err;
    EXPECT_EQ(tools.size(), 3u);
}

TEST(McpTransportTest, ToolsListSupportsPagination) {
    McpClient client(make_spec("alpha", "paged_tools"), fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(client.start(&err)) << err;
    McpServerInfo info;
    ASSERT_TRUE(client.initialize(&info, &err)) << err;

    std::vector<McpToolInfo> tools;
    ASSERT_TRUE(client.list_tools(&tools, &err)) << err;
    ASSERT_EQ(tools.size(), 4u);
    EXPECT_EQ(tools[0].remote_name, "adder");
    EXPECT_EQ(tools[1].remote_name, "echo");
    EXPECT_EQ(tools[2].remote_name, "big_text");
    EXPECT_EQ(tools[3].remote_name, "paged_echo");
}

TEST(McpTransportTest, TimeoutFails) {
    McpClient client(make_spec("alpha", "timeout"), fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(client.start(&err)) << err;
    McpServerInfo info;
    EXPECT_FALSE(client.initialize(&info, &err));
    EXPECT_NE(err.find("Timed out"), std::string::npos);
    EXPECT_NE(err.find("intentionally not responding"), std::string::npos);
}

TEST(McpTransportTest, WriteSideStallFailsWithTimeout) {
    McpClient client(make_spec("alpha", "write_stall_after_init"), fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(client.start(&err)) << err;
    McpServerInfo info;
    ASSERT_TRUE(client.initialize(&info, &err)) << err;

    std::vector<McpToolInfo> tools;
    ASSERT_TRUE(client.list_tools(&tools, &err)) << err;

    nlohmann::json result;
    const std::string huge_payload(512 * 1024, 'x');
    EXPECT_FALSE(client.call_tool("echo", nlohmann::json{{"text", huge_payload}}, &result, &err));
    EXPECT_NE(err.find("Timed out writing"), std::string::npos);
    EXPECT_NE(err.find("stop reading stdin"), std::string::npos);
}

TEST(McpTransportTest, BrokenPipeFails) {
    McpClient client(make_spec("alpha", "broken_pipe"), fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(client.start(&err)) << err;
    McpServerInfo info;
    EXPECT_FALSE(client.initialize(&info, &err));
    EXPECT_TRUE(err.find("Broken pipe") != std::string::npos || err.find("closed") != std::string::npos);
    EXPECT_NE(err.find("server is exiting early"), std::string::npos);
}

TEST(McpTransportTest, ToolNotFoundFails) {
    McpClient client(make_spec("alpha", "success"), fs::temp_directory_path().string(), fast_options());
    std::string err;

    ASSERT_TRUE(client.start(&err)) << err;
    McpServerInfo info;
    ASSERT_TRUE(client.initialize(&info, &err)) << err;

    nlohmann::json result;
    EXPECT_FALSE(client.call_tool("missing", nlohmann::json::object(), &result, &err));
    EXPECT_NE(err.find("Unknown tool"), std::string::npos);
}
