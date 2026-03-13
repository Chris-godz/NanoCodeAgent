#pragma once

#include <string>
#include <vector>

enum class PatchRejectCode {
    None,
    EmptyOldText,
    FileReadFailure,
    BinaryFile,
    TruncatedFile,
    NoMatch,
    MultipleMatches,
    InvalidBatchEntry,
    WritebackFailure,
};

const char* patch_reject_code_to_string(PatchRejectCode code);

/// Result returned by both apply_patch_single and apply_patch_batch.
struct ApplyPatchResult {
    bool ok;
    /// For single patch: number of times old_text was found (0, 1, or >1).
    /// For batch patch:  number of patches successfully applied before failure,
    ///                   or total patch count on success.
    int match_count;
    std::string err;
    /// On success (ok == true), this is PatchRejectCode::None.
    /// On failure (ok == false), this is the primary categorized reason.
    PatchRejectCode reject_code = PatchRejectCode::None;
    /// For apply_patch_single: always -1.
    /// For apply_patch_batch:  -1 on success, or on failures not tied to a
    /// specific entry; otherwise the zero-based index of the first failing
    /// patch entry.
    int patch_index = -1;
};

/// A single (old_text -> new_text) replacement entry used in batch mode.
struct PatchEntry {
    std::string old_text;
    std::string new_text;
};

/// Apply a single exact-text replacement to a workspace file.
///
/// Reads the file, verifies that old_text occurs exactly once (overlapping
/// occurrences count), replaces it with new_text, then writes the result back.
/// An explicit empty new_text is valid (it deletes the matched text).
///
/// Failures:
///   - old_text empty            -> ok=false, match_count=0
///   - file read fails           -> ok=false, match_count=0
///   - file truncated on read    -> ok=false, match_count=0
///   - file is binary            -> ok=false, match_count=0
///   - old_text not found        -> ok=false, match_count=0
///   - old_text found >1 times   -> ok=false, match_count=N
///   - file write fails          -> ok=false, match_count=1
ApplyPatchResult apply_patch_single(const std::string& workspace_abs,
                                    const std::string& rel_path,
                                    const std::string& old_text,
                                    const std::string& new_text);

/// Apply an ordered sequence of exact-text replacements to a workspace file.
///
/// Reads the file once, applies all patches in memory in order (each patch
/// operates on the result of the previous one), then writes the final result
/// back in a single write_file_safe call.
///
/// The write-back is NOT filesystem-atomic (no temp-file / fsync / rename).
///
/// If any patch fails, the entire batch is aborted and the file is left
/// unchanged (no partial write occurs because nothing is written until all
/// in-memory patches succeed).
///
/// Failures:
///   - patches array empty       -> ok=false, match_count=0
///   - patches.size() > INT_MAX  -> ok=false, match_count=0
///   - any entry old_text empty  -> ok=false
///   - file read fails / truncated / binary -> ok=false
///   - any patch 0 or >1 match   -> ok=false (file not modified)
///   - file write fails          -> ok=false
ApplyPatchResult apply_patch_batch(const std::string& workspace_abs,
                                   const std::string& rel_path,
                                   const std::vector<PatchEntry>& patches);
