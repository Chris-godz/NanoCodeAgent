/// src/apply_patch.cpp
///
/// Exact-text replacement primitive used by the apply_patch tool.
///
/// Design notes
/// ─────────────
/// • Uses read_file_safe / write_file_safe for all I/O, inheriting their
///   workspace-boundary enforcement (workspace_resolve + O_NOFOLLOW).
/// • count_occurrences_with_overlap advances by 1 per iteration so that
///   overlapping occurrences (e.g. "aa" in "aaa") are counted correctly.
/// • Truncated or binary reads are rejected immediately; no patch is applied.
/// • Batch mode applies all patches in memory first; the file is written back
///   only if every patch succeeds.  Note: write_file_safe does NOT perform a
///   filesystem-atomic rename — no temp-file / fsync / rename is used.

#include "apply_patch.hpp"
#include "read_file.hpp"
#include "write_file.hpp"

#include <limits>
#include <string>
#include <vector>

namespace {

struct PatchValidationResult {
    bool ok;
    int match_count;
    std::string err;
    PatchRejectCode reject_code = PatchRejectCode::None;
    int patch_index = -1;
    std::string content;
};

/// Count how many times needle appears in haystack, including overlapping
/// occurrences.  Advances the search position by 1 on every hit so that
/// "aa" in "aaa" is counted as 2, not 1.
static int count_occurrences_with_overlap(const std::string& haystack,
                                          const std::string& needle) {
    if (needle.empty()) {
        return 0;
    }
    int count = 0;
    std::string::size_type pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += 1;  // advance by 1, NOT by needle.size(), to detect overlaps
    }
    return count;
}

static PatchValidationResult reject_validation(int match_count,
                                               std::string err,
                                               PatchRejectCode reject_code,
                                               int patch_index = -1) {
    return {false, match_count, std::move(err), reject_code, patch_index, ""};
}

static ApplyPatchResult to_apply_patch_result(const PatchValidationResult& validation) {
    return {
        validation.ok,
        validation.match_count,
        validation.err,
        validation.reject_code,
        validation.patch_index,
    };
}

static PatchValidationResult validate_file_for_patch(const std::string& workspace_abs,
                                                     const std::string& rel_path) {
    const auto rr = read_file_safe(workspace_abs, rel_path);
    if (rr.is_binary) {
        return reject_validation(
            0,
            "file appears to be binary; refusing to patch binary content",
            PatchRejectCode::BinaryFile);
    }
    if (rr.truncated) {
        return reject_validation(
            0,
            "file was truncated during read; refusing to patch truncated content",
            PatchRejectCode::TruncatedFile);
    }
    if (!rr.ok) {
        return reject_validation(
            0,
            "failed to read file: " + rr.err,
            PatchRejectCode::FileReadFailure);
    }

    return {true, 0, "", PatchRejectCode::None, -1, rr.content};
}

/// Apply a single (old_text → new_text) replacement on an in-memory string.
/// Returns a validated replacement result. On failure, the original content is
/// preserved in the caller.
static PatchValidationResult validate_single_on_content(std::string content,
                                                        const std::string& old_text,
                                                        const std::string& new_text) {
    if (old_text.empty()) {
        return reject_validation(
            0,
            "old_text must not be empty",
            PatchRejectCode::EmptyOldText);
    }

    const int count = count_occurrences_with_overlap(content, old_text);

    if (count == 0) {
        return reject_validation(
            0,
            "old_text not found in file content",
            PatchRejectCode::NoMatch);
    }
    if (count > 1) {
        return reject_validation(
            count,
            "old_text occurs " + std::to_string(count) +
                " times in file; it must be unique",
            PatchRejectCode::MultipleMatches);
    }

    // Exactly one match: replace it.
    const std::string::size_type pos = content.find(old_text);
    content.replace(pos, old_text.size(), new_text);
    return {true, 1, "", PatchRejectCode::None, -1, std::move(content)};
}

static PatchValidationResult validate_patch_single(const std::string& workspace_abs,
                                                   const std::string& rel_path,
                                                   const std::string& old_text,
                                                   const std::string& new_text) {
    auto file_validation = validate_file_for_patch(workspace_abs, rel_path);
    if (!file_validation.ok) {
        return file_validation;
    }

    return validate_single_on_content(std::move(file_validation.content), old_text, new_text);
}

static PatchValidationResult validate_patch_batch(const std::string& workspace_abs,
                                                  const std::string& rel_path,
                                                  const std::vector<PatchEntry>& patches) {
    if (patches.empty()) {
        return reject_validation(
            0,
            "'patches' array must not be empty",
            PatchRejectCode::InvalidBatchEntry);
    }
    if (patches.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return reject_validation(
            0,
            "'patches' array is too large (exceeds INT_MAX entries)",
            PatchRejectCode::InvalidBatchEntry);
    }

    auto file_validation = validate_file_for_patch(workspace_abs, rel_path);
    if (!file_validation.ok) {
        return file_validation;
    }

    std::string content = std::move(file_validation.content);
    for (std::size_t i = 0; i < patches.size(); ++i) {
        auto step = validate_single_on_content(std::move(content), patches[i].old_text, patches[i].new_text);
        if (!step.ok) {
            PatchRejectCode reject_code = step.reject_code;
            if (step.reject_code == PatchRejectCode::EmptyOldText) {
                reject_code = PatchRejectCode::InvalidBatchEntry;
            }
            std::string err_msg = "patch[";
            err_msg += std::to_string(i);
            err_msg += "]: ";
            err_msg += step.err;
            return reject_validation(
                static_cast<int>(i),
                std::move(err_msg),
                reject_code,
                static_cast<int>(i));
        }
        content = std::move(step.content);
    }

    return {
        true,
        static_cast<int>(patches.size()),
        "",
        PatchRejectCode::None,
        -1,
        std::move(content),
    };
}

}  // namespace

const char* patch_reject_code_to_string(PatchRejectCode code) {
    switch (code) {
        case PatchRejectCode::None:
            return "none";
        case PatchRejectCode::EmptyOldText:
            return "empty_old_text";
        case PatchRejectCode::FileReadFailure:
            return "file_read_failure";
        case PatchRejectCode::BinaryFile:
            return "binary_file";
        case PatchRejectCode::TruncatedFile:
            return "truncated_file";
        case PatchRejectCode::NoMatch:
            return "no_match";
        case PatchRejectCode::MultipleMatches:
            return "multiple_matches";
        case PatchRejectCode::InvalidBatchEntry:
            return "invalid_batch_entry";
        case PatchRejectCode::WritebackFailure:
            return "writeback_failure";
        default:
            return "unknown";
    }
}

ApplyPatchResult apply_patch_single(const std::string& workspace_abs,
                                    const std::string& rel_path,
                                    const std::string& old_text,
                                    const std::string& new_text) {
    const auto validation = validate_patch_single(workspace_abs, rel_path, old_text, new_text);
    if (!validation.ok) {
        return to_apply_patch_result(validation);
    }

    const auto wr = write_file_safe(workspace_abs, rel_path, validation.content);
    if (!wr.ok) {
        return {
            false,
            validation.match_count,
            "patch applied in memory but write-back failed: " + wr.err,
            PatchRejectCode::WritebackFailure,
            -1,
        };
    }

    return {true, validation.match_count, "", PatchRejectCode::None, -1};
}

ApplyPatchResult apply_patch_batch(const std::string& workspace_abs,
                                   const std::string& rel_path,
                                   const std::vector<PatchEntry>& patches) {
    const auto validation = validate_patch_batch(workspace_abs, rel_path, patches);
    if (!validation.ok) {
        return to_apply_patch_result(validation);
    }

    // All patches succeeded in memory — write back once.
    // Note: write_file_safe is not filesystem-atomic (no temp-file+fsync+rename).
    const auto wr = write_file_safe(workspace_abs, rel_path, validation.content);
    if (!wr.ok) {
        return {
            false,
            validation.match_count,
            "patches applied in memory but write-back failed: " + wr.err,
            PatchRejectCode::WritebackFailure,
            -1,
        };
    }

    return {true, validation.match_count, "", PatchRejectCode::None, -1};
}
