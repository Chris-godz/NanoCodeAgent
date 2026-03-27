#include "llm.hpp"
#include "http.hpp"
#include "logger.hpp"
#include "sse_parser.hpp"
#include "tool_call.hpp"
#include "tool_call_assembler.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <iostream>
#include <string_view>

using json = nlohmann::json;

namespace {

constexpr std::string_view kPlannerRepairPromptPrefix = "Planner contract repair required.";
constexpr std::string_view kPlannerRepairArtifactToolName = "planner_repair_artifact";

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string normalize_requested_planner_repair_mode(std::string value) {
    const std::string lowered = lower_copy(std::move(value));
    if (lowered.empty() || lowered == "auto" || lowered == "provider_profile" || lowered == "provider-profile") {
        return "auto";
    }
    if (lowered == "structured" ||
        lowered == "json_object" ||
        lowered == "json-object" ||
        lowered == "structured_output" ||
        lowered == "structured-output") {
        return "structured";
    }
    if (lowered == "artifact_envelope" ||
        lowered == "artifact-envelope" ||
        lowered == "artifact" ||
        lowered == "tool_envelope" ||
        lowered == "tool-envelope" ||
        lowered == "tool") {
        return "artifact_envelope";
    }
    if (lowered == "baseline" || lowered == "free_content" || lowered == "free-content") {
        return "baseline";
    }
    return "baseline";
}

bool provider_profile_is_deepseek(const AgentConfig& cfg) {
    const std::string base_url = lower_copy(cfg.base_url);
    const std::string model = lower_copy(cfg.model);
    return base_url.find("deepseek") != std::string::npos ||
           model.find("deepseek") != std::string::npos;
}

bool provider_supports_structured_output(const AgentConfig& cfg) {
    if (provider_profile_is_deepseek(cfg)) {
        return true;
    }

    const std::string base_url = lower_copy(cfg.base_url);
    const std::string model = lower_copy(cfg.model);
    if (base_url.find("openai.com") != std::string::npos) {
        return true;
    }
    return model.starts_with("gpt-");
}

bool is_planner_repair_message(const json& message) {
    return message.is_object() &&
           message.value("role", "") == "system" &&
           message.contains("content") &&
           message.at("content").is_string() &&
           message.at("content").get_ref<const std::string&>().starts_with(kPlannerRepairPromptPrefix);
}

json build_planner_repair_artifact_tool() {
    return json{
        {"type", "function"},
        {"function", {
            {"name", std::string(kPlannerRepairArtifactToolName)},
            {"description", "Emit the repaired planner contract as a single JSON object."},
            {"parameters", {
                {"type", "object"},
                {"additionalProperties", false},
                {"required", json::array({"plan"})},
                {"properties", {
                    {"plan", {
                        {"type", "object"},
                        {"additionalProperties", false},
                        {"required", json::array({"steps"})},
                        {"properties", {
                            {"summary", {{"type", "string"}}},
                            {"metadata", {{"type", "object"}}},
                            {"steps", {
                                {"type", "array"},
                                {"minItems", 1},
                                {"items", {
                                    {"type", "object"},
                                    {"additionalProperties", true},
                                    {"required", json::array({"id", "title", "detail"})},
                                    {"properties", {
                                        {"id", {{"type", "string"}}},
                                        {"title", {{"type", "string"}}},
                                        {"detail", {{"type", "string"}}},
                                        {"status", {{"type", "string"}}},
                                        {"metadata", {{"type", "object"}}}
                                    }}
                                }}
                            }}
                        }}
                    }}
                }}
            }}
        }}
    };
}

bool is_planner_repair_artifact_call(const ToolCall& tool_call) {
    return tool_call.name == kPlannerRepairArtifactToolName &&
           tool_call.arguments.is_object() &&
           tool_call.arguments.contains("plan") &&
           tool_call.arguments.at("plan").is_object();
}

} // namespace

bool llm_parse_response(const std::string& json_resp, std::string* out_text, std::string* err) {
    try {
        auto parsed = json::parse(json_resp);
        if (parsed.contains("error")) {
            if (err) *err = "API Error: " + parsed["error"].dump();
            return false;
        }
        if (parsed.contains("choices") && parsed["choices"].is_array() && parsed["choices"].size() > 0) {
            auto first_choice = parsed["choices"][0];
            if (first_choice.contains("message") && first_choice["message"].contains("content")) {
                if (first_choice["message"]["content"].is_string()) {
                    if (out_text) *out_text = first_choice["message"]["content"].get<std::string>();
                    return true;
                }
            }
        }
        if (err) *err = "Response missing choices[0].message.content";
        return false;
    } catch (const std::exception& e) {
        if (err) *err = "JSON parse error: " + std::string(e.what());
        return false;
    }
}

bool llm_stream_process_chunk(const std::string& chunk,
                              SseParser& parser,
                              const std::function<bool(const std::string&)>& on_content_delta,
                              ToolCallAssembler* tool_asm,
                              std::string* err,
                              nlohmann::json* usage) {
    auto events = parser.feed(chunk);
    
    for (const auto& event : events) {
        if (event == "[DONE]") {
            return true; 
        }

        try {
            auto parsed = json::parse(event);
            if (parsed.contains("error")) {
                if (err) {
                    *err = "API Error: ";
                    if (parsed["error"].contains("message") && parsed["error"]["message"].is_string()) {
                        *err += parsed["error"]["message"].get<std::string>();
                    } else {
                        *err += parsed["error"].dump();
                    }
                }
                return false;
            }

            if (usage != nullptr && parsed.contains("usage") && parsed["usage"].is_object()) {
                *usage = parsed["usage"];
            }

            if (parsed.contains("choices") && parsed["choices"].is_array() && parsed["choices"].size() > 0) {
                auto first_choice = parsed["choices"][0];
                if (first_choice.contains("delta")) {
                    auto delta = first_choice["delta"];
                    
                    if (delta.contains("content") && delta["content"].is_string()) {
                        std::string delta_text = delta["content"].get<std::string>();
                        if (!delta_text.empty() && on_content_delta) {
                            if (!on_content_delta(delta_text)) {
                                if (err) *err = "User aborted stream";
                                return false;
                            }
                        }
                    }
                    
                    if (tool_asm && delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                        for (const auto& tcd : delta["tool_calls"]) {
                            if (!tool_asm->ingest_delta(tcd, err)) return false;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            if (err) *err = "Stream Parse Error: " + std::string(e.what()) + " Event: " + event;
            return false;
        }
    }
    return true;
}

bool llm_is_planner_repair_turn(const json& messages) {
    if (!messages.is_array() || messages.empty()) {
        return false;
    }
    return is_planner_repair_message(messages.back());
}

bool llm_provider_profile_is_deepseek(const AgentConfig& cfg) {
    return provider_profile_is_deepseek(cfg);
}

PlannerRepairModeSelection llm_resolve_planner_repair_mode(const AgentConfig& cfg) {
    PlannerRepairModeSelection selection;
    selection.requested_mode = normalize_requested_planner_repair_mode(cfg.planner_repair_mode);

    if (selection.requested_mode == "auto") {
        if (provider_profile_is_deepseek(cfg)) {
            selection.effective_mode = "structured";
            selection.reason = "provider_profile_default_deepseek_structured";
        } else {
            selection.effective_mode = "baseline";
            selection.reason = "provider_profile_default_baseline_non_deepseek";
        }
        return selection;
    }

    if (selection.requested_mode == "structured") {
        if (provider_supports_structured_output(cfg)) {
            selection.effective_mode = "structured";
            selection.reason = "explicit_structured_provider_capable";
        } else {
            selection.effective_mode = "baseline";
            selection.reason = "explicit_structured_fallback_baseline_provider_capability_unknown";
        }
        return selection;
    }

    if (selection.requested_mode == "artifact_envelope") {
        selection.effective_mode = "artifact_envelope";
        selection.reason = "explicit_artifact_envelope_experimental";
        return selection;
    }

    selection.effective_mode = "baseline";
    selection.reason = "explicit_baseline";
    return selection;
}

json llm_build_chat_completion_request(const AgentConfig& cfg,
                                       const json& messages,
                                       const json& tools) {
    json req_json = {
        {"model", cfg.model},
        {"messages", messages},
        {"stream", true},
        {"stream_options", {
            {"include_usage", true}
        }}
    };

    const bool planner_repair_turn = llm_is_planner_repair_turn(messages);
    const PlannerRepairModeSelection repair_selection = planner_repair_turn
        ? llm_resolve_planner_repair_mode(cfg)
        : PlannerRepairModeSelection{"baseline", "baseline", "non_repair_turn"};

    if (planner_repair_turn) {
        if (repair_selection.effective_mode == "structured") {
            req_json["response_format"] = {
                {"type", "json_object"}
            };
        } else if (repair_selection.effective_mode == "artifact_envelope") {
            req_json["tools"] = json::array({build_planner_repair_artifact_tool()});
            req_json["tool_choice"] = {
                {"type", "function"},
                {"function", {
                    {"name", std::string(kPlannerRepairArtifactToolName)}
                }}
            };
        }
        return req_json;
    }

    if (!tools.empty()) {
        req_json["tools"] = tools;
    }
    return req_json;
}

json llm_materialize_assistant_message(const AgentConfig& cfg,
                                       const json& messages,
                                       const std::string& full_content,
                                       const std::vector<ToolCall>& tool_calls,
                                       const json& usage) {
    json res = {
        {"role", "assistant"}
    };

    const bool planner_repair_turn = llm_is_planner_repair_turn(messages);
    const PlannerRepairModeSelection repair_selection = planner_repair_turn
        ? llm_resolve_planner_repair_mode(cfg)
        : PlannerRepairModeSelection{"baseline", "baseline", "non_repair_turn"};

    if (planner_repair_turn && repair_selection.effective_mode == "artifact_envelope") {
        for (const ToolCall& tool_call : tool_calls) {
            if (is_planner_repair_artifact_call(tool_call)) {
                res["content"] = tool_call.arguments.dump();
                if (usage.is_object() && !usage.empty()) {
                    res["usage"] = usage;
                }
                return res;
            }
        }
    }

    if (!full_content.empty()) {
        res["content"] = full_content;
    }

    if (!tool_calls.empty()) {
        json tc_arr = json::array();
        for (const auto& tc : tool_calls) {
            tc_arr.push_back({
                {"id", tc.id},
                {"type", "function"},
                {"function", {
                    {"name", tc.name},
                    {"arguments", tc.arguments.dump()}
                }}
            });
        }
        res["tool_calls"] = tc_arr;
    }

    if (usage.is_object() && !usage.empty()) {
        res["usage"] = usage;
    }

    return res;
}

json llm_chat_completion_stream(
    const AgentConfig& cfg, 
    const json& messages,
    const json& tools,
    std::function<bool(const std::string&)> on_content_delta
) {
    if (!cfg.api_key.has_value() || cfg.api_key.value().empty()) {
        throw std::runtime_error("API key is not configured.");
    }

    json req_json = llm_build_chat_completion_request(cfg, messages, tools);

    std::string payload = req_json.dump();
    std::vector<std::string> headers = {
        "Content-Type: application/json",
        "Authorization: Bearer " + cfg.api_key.value()
    };

    std::string url = cfg.base_url;
    if (!url.empty() && url.back() == '/') url.pop_back();
    
    // Sometimes people configure --base-url https://api.openai.com/v1
    // We append /chat/completions. If base_url already contains it, don't append.
    if (url.find("/chat/completions") == std::string::npos) {
        url += "/chat/completions";
    }

    SseParser sse_parser;
    ToolCallAssembler asm_tools;
    std::string err_msg;
    bool process_ok = true;
    
    std::string full_content;
    json usage = json::object();

    if (cfg.debug_mode) {
        LOG_DEBUG("LLM Req URL: {}", url);
    }

    HttpOptions http_opts;
    bool req_ok = http_post_json_stream(
        url,
        headers,
        payload,
        http_opts,
        [&](const std::string& chunk_data) {
            // Intercept content_delta to also accumulate the full_content locally
            auto local_cb = [&](const std::string& txt) -> bool {
                full_content += txt;
                if (on_content_delta) return on_content_delta(txt);
                return true;
            };
            bool ok = llm_stream_process_chunk(chunk_data,
                                               sse_parser,
                                               local_cb,
                                               &asm_tools,
                                               &err_msg,
                                               &usage);
            if (!ok) process_ok = false;
            return ok; // false aborts the curl download
        },
        &err_msg
    );

    if (!req_ok) {
        throw std::runtime_error("Network request failed: " + err_msg);
    }
    if (!process_ok) {
        throw std::runtime_error("Stream processing failed: " + err_msg);
    }

    // Attempt to assemble tools. If their json args are busted, throw runtime_error
    std::vector<ToolCall> tools_res;
    if (!asm_tools.finalize(&tools_res, &err_msg)) {
        throw std::runtime_error("Tool finalization failed: " + err_msg);
    }

    return llm_materialize_assistant_message(cfg, messages, full_content, tools_res, usage);
}
