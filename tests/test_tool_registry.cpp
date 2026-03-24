#include <gtest/gtest.h>

#include "tool_registry.hpp"

#include <nlohmann/json.hpp>

#include <utility>
#include <vector>

namespace {

ToolDescriptor make_descriptor(const std::string& name,
                               ToolCategory category,
                               bool mutates_repository_state,
                               bool can_execute_repo_controlled_code,
                               bool requires_approval = true,
                               std::vector<std::string> skill_aliases = {}) {
    ToolDescriptor descriptor;
    descriptor.name = name;
    descriptor.description = name + " tool";
    descriptor.category = category;
    descriptor.mutates_repository_state = mutates_repository_state;
    descriptor.can_execute_repo_controlled_code = can_execute_repo_controlled_code;
    descriptor.requires_approval = requires_approval;
    descriptor.skill_aliases = std::move(skill_aliases);
    descriptor.json_schema = {{"type", "object"}};
    descriptor.execute = [](const ToolCall&, const AgentConfig&, size_t) {
        return nlohmann::json{{"ok", true}};
    };
    return descriptor;
}

} // namespace

TEST(ToolRegistryTest, RegistersAndFindsToolsByName) {
    ToolRegistry registry;
    ToolDescriptor descriptor = make_descriptor("alpha", ToolCategory::ReadOnly, false, false, false);
    descriptor.max_output_bytes = 128;

    std::string err;
    EXPECT_TRUE(registry.register_tool(descriptor, &err)) << err;

    const ToolDescriptor* found = registry.find("alpha");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "alpha");
    EXPECT_EQ(found->description, "alpha tool");
    EXPECT_EQ(found->category, ToolCategory::ReadOnly);
    EXPECT_FALSE(found->mutates_repository_state);
    EXPECT_FALSE(found->can_execute_repo_controlled_code);
    EXPECT_FALSE(found->requires_approval);
}

TEST(ToolRegistryTest, RejectsDuplicateRegistration) {
    ToolRegistry registry;
    ToolDescriptor descriptor = make_descriptor("duplicate", ToolCategory::ReadOnly, false, false, false);

    std::string err;
    EXPECT_TRUE(registry.register_tool(descriptor, &err)) << err;
    EXPECT_FALSE(registry.register_tool(descriptor, &err));
    EXPECT_NE(err.find("already registered"), std::string::npos);
}

TEST(ToolRegistryTest, SchemaPreservesRegistrationOrder) {
    ToolRegistry registry;
    ToolDescriptor first = make_descriptor("first", ToolCategory::ReadOnly, false, false, false);
    ToolDescriptor second = make_descriptor("second", ToolCategory::ReadOnly, false, false, false);

    std::string err;
    EXPECT_TRUE(registry.register_tool(first, &err)) << err;
    EXPECT_TRUE(registry.register_tool(second, &err)) << err;

    const auto schema = registry.to_openai_schema();
    ASSERT_TRUE(schema.is_array());
    ASSERT_EQ(schema.size(), 2);
    EXPECT_EQ(schema[0]["function"]["name"], "first");
    EXPECT_EQ(schema[1]["function"]["name"], "second");
}

TEST(ToolRegistryTest, ApprovalFilteredSchemaUsesRegistrationOrder) {
    ToolRegistry registry;
    ToolDescriptor first = make_descriptor("first", ToolCategory::ReadOnly, false, false, false);
    ToolDescriptor second = make_descriptor("second", ToolCategory::Mutating, true, false, true);
    ToolDescriptor third = make_descriptor("third", ToolCategory::Execution, false, true, true);

    std::string err;
    ASSERT_TRUE(registry.register_tool(first, &err)) << err;
    ASSERT_TRUE(registry.register_tool(second, &err)) << err;
    ASSERT_TRUE(registry.register_tool(third, &err)) << err;

    AgentConfig config;
    const auto schema = registry.to_openai_schema(config);
    ASSERT_TRUE(schema.is_array());
    ASSERT_EQ(schema.size(), 1);
    EXPECT_EQ(schema[0]["function"]["name"], "first");
}

TEST(ToolRegistryTest, ApprovalFilteredSchemaIncludesApprovedTools) {
    ToolRegistry registry;
    ToolDescriptor read = make_descriptor("read", ToolCategory::ReadOnly, false, false, false);
    ToolDescriptor write = make_descriptor("write", ToolCategory::Mutating, true, false, true);
    ToolDescriptor exec = make_descriptor("exec", ToolCategory::Execution, false, true, true);

    std::string err;
    ASSERT_TRUE(registry.register_tool(read, &err)) << err;
    ASSERT_TRUE(registry.register_tool(write, &err)) << err;
    ASSERT_TRUE(registry.register_tool(exec, &err)) << err;

    AgentConfig config;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;

    const auto schema = registry.to_openai_schema(config);
    ASSERT_TRUE(schema.is_array());
    ASSERT_EQ(schema.size(), 3);
    EXPECT_EQ(schema[0]["function"]["name"], "read");
    EXPECT_EQ(schema[1]["function"]["name"], "write");
    EXPECT_EQ(schema[2]["function"]["name"], "exec");
}

TEST(ToolRegistryTest, ReadOnlyToolExecutesWithoutApproval) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor = make_descriptor("read", ToolCategory::ReadOnly, false, false, false);
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}, {"value", 1}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "read";
    AgentConfig config;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_TRUE(called);
    EXPECT_TRUE(result["ok"].get<bool>());
}

TEST(ToolRegistryTest, RiskFreeToolApprovalFlagIsNormalizedOffAtRegistration) {
    ToolRegistry registry;
    ToolDescriptor descriptor = make_descriptor("read", ToolCategory::ReadOnly, false, false, true);

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    const ToolDescriptor* stored = registry.find("read");
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->category, ToolCategory::ReadOnly);
    EXPECT_FALSE(stored->requires_approval);
}

TEST(ToolRegistryTest, MutatingOnlyToolBlockedWithoutApproval) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor = make_descriptor("write", ToolCategory::Mutating, true, false);
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "write";
    AgentConfig config;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_FALSE(called);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"], "blocked");
    EXPECT_EQ(result["category"], "mutating");
    EXPECT_TRUE(result["requires_mutating_approval"].get<bool>());
    EXPECT_FALSE(result["requires_execution_approval"].get<bool>());
    EXPECT_EQ(result["missing_approvals"], nlohmann::json::array({"mutating"}));
    EXPECT_NE(result["error"].get<std::string>().find("allow_mutating_tools"), std::string::npos);
}

TEST(ToolRegistryTest, ExecutionOnlyToolBlockedWithoutApproval) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor = make_descriptor("exec", ToolCategory::Execution, false, true);
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "exec";
    AgentConfig config;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_FALSE(called);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"], "blocked");
    EXPECT_EQ(result["category"], "execution");
    EXPECT_FALSE(result["requires_mutating_approval"].get<bool>());
    EXPECT_TRUE(result["requires_execution_approval"].get<bool>());
    EXPECT_EQ(result["missing_approvals"], nlohmann::json::array({"execution"}));
    EXPECT_NE(result["error"].get<std::string>().find("allow_execution_tools"), std::string::npos);
}

TEST(ToolRegistryTest, DualRiskToolBlockedWhenBothApprovalsAreMissing) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor = make_descriptor("git_commit", ToolCategory::Mutating, true, true);
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "git_commit";
    AgentConfig config;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_FALSE(called);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"], "blocked");
    EXPECT_EQ(result["category"], "mutating");
    EXPECT_TRUE(result["requires_mutating_approval"].get<bool>());
    EXPECT_TRUE(result["requires_execution_approval"].get<bool>());
    EXPECT_EQ(result["missing_approvals"], nlohmann::json::array({"mutating", "execution"}));
    EXPECT_NE(result["error"].get<std::string>().find("allow_mutating_tools"), std::string::npos);
    EXPECT_NE(result["error"].get<std::string>().find("allow_execution_tools"), std::string::npos);
}

TEST(ToolRegistryTest, DualRiskToolBlockedWhenExecutionApprovalIsMissing) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor = make_descriptor("git_commit", ToolCategory::Mutating, true, true);
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "git_commit";
    AgentConfig config;
    config.allow_mutating_tools = true;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_FALSE(called);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["missing_approvals"], nlohmann::json::array({"execution"}));
}

TEST(ToolRegistryTest, DualRiskToolBlockedWhenMutatingApprovalIsMissing) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor = make_descriptor("git_commit", ToolCategory::Mutating, true, true);
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "git_commit";
    AgentConfig config;
    config.allow_execution_tools = true;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_FALSE(called);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["missing_approvals"], nlohmann::json::array({"mutating"}));
}

TEST(ToolRegistryTest, MutatingOnlyToolExecutesWhenApproved) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor = make_descriptor("write", ToolCategory::Mutating, true, false);
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "write";
    AgentConfig config;
    config.allow_mutating_tools = true;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_TRUE(called);
    EXPECT_TRUE(result["ok"].get<bool>());
}

TEST(ToolRegistryTest, ExecutionOnlyToolExecutesWhenApproved) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor = make_descriptor("exec", ToolCategory::Execution, false, true);
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "exec";
    AgentConfig config;
    config.allow_execution_tools = true;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_TRUE(called);
    EXPECT_TRUE(result["ok"].get<bool>());
}

TEST(ToolRegistryTest, DualRiskToolExecutesOnlyWhenBothApprovalsAreEnabled) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor = make_descriptor("git_commit", ToolCategory::Mutating, true, true);
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "git_commit";
    AgentConfig config;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_TRUE(called);
    EXPECT_TRUE(result["ok"].get<bool>());
}

TEST(ToolRegistryTest, UnregisteredToolStillReturnsRegistryError) {
    ToolRegistry registry;
    ToolCall tc;
    tc.name = "missing";
    AgentConfig config;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"], "failed");
    EXPECT_NE(result["error"].get<std::string>().find("not registered"), std::string::npos);
}
