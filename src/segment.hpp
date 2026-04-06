#pragma once

#include <string>

struct Segment {
    std::size_t index = 0;
    std::string id;
    std::string source_zh;
    /// When true, LlamaTranslator uses a multi-passage prompt and relaxed post-processing.
    bool coalesced_batch = false;
    /// 0 = use translator default max_tokens; used for merged TEI batches.
    int max_output_tokens = 0;
};
