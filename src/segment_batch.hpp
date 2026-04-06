#pragma once

#include "segment.hpp"

#include <cstddef>
#include <string>
#include <vector>

/// Source-side delimiter between passages in a merged batch (also requested on the English side).
extern const char* k_coalesce_marker;

struct CoalesceParams {
    bool enabled = true;
    std::size_t max_per_batch = 6;
    std::size_t max_merged_chars = 2800;
    int max_tokens_per_segment = 192;
    int n_ctx = 2048;
};

struct TranslationWorkUnit {
    std::vector<std::size_t> segment_indices;
};

std::vector<TranslationWorkUnit> build_translation_work_units(
    const std::vector<Segment>& segments,
    const CoalesceParams& params
);

/// Split model output on `<<<SEG>>>` markers; returns empty on failure.
std::vector<std::string> split_coalesced_english(const std::string& text, std::size_t expected_parts);

std::string merge_source_zh(
    const std::vector<Segment>& segments,
    const std::vector<std::size_t>& indices
);

int compute_batch_max_output_tokens(int per_segment_cap, std::size_t batch_size, int n_ctx);
