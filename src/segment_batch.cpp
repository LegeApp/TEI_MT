#include "segment_batch.hpp"

#include <algorithm>
#include <cctype>

const char* k_coalesce_marker = "<<<SEG>>>";

namespace {

std::string trim_edges(std::string s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

}  // namespace

std::vector<TranslationWorkUnit> build_translation_work_units(
    const std::vector<Segment>& segments,
    const CoalesceParams& params
) {
    std::vector<TranslationWorkUnit> units;
    if (segments.empty()) {
        return units;
    }

    if (!params.enabled || params.max_per_batch <= 1) {
        units.reserve(segments.size());
        for (std::size_t i = 0; i < segments.size(); ++i) {
            TranslationWorkUnit u;
            u.segment_indices.push_back(i);
            units.push_back(std::move(u));
        }
        return units;
    }

    const std::string delim = std::string("\n") + k_coalesce_marker + "\n";
    std::size_t i = 0;
    while (i < segments.size()) {
        TranslationWorkUnit u;
        std::size_t merged_len = 0;

        while (i < segments.size() && u.segment_indices.size() < params.max_per_batch) {
            const std::size_t seg_len = segments[i].source_zh.size();
            const std::size_t extra = u.segment_indices.empty() ? seg_len : delim.size() + seg_len;

            if (!u.segment_indices.empty() && merged_len + extra > params.max_merged_chars) {
                break;
            }
            if (u.segment_indices.empty() && seg_len > params.max_merged_chars) {
                u.segment_indices.push_back(i);
                ++i;
                merged_len = seg_len;
                break;
            }

            u.segment_indices.push_back(i);
            merged_len += extra;
            ++i;
        }

        if (!u.segment_indices.empty()) {
            units.push_back(std::move(u));
        }
    }

    return units;
}

std::vector<std::string> split_coalesced_english(const std::string& text, std::size_t expected_parts) {
    if (expected_parts == 0) {
        return {};
    }
    if (expected_parts == 1) {
        std::vector<std::string> one;
        one.push_back(trim_edges(text));
        if (one[0].empty()) {
            return {};
        }
        return one;
    }

    const std::string marker(k_coalesce_marker);
    std::vector<std::string> parts;
    std::size_t start = 0;

    for (;;) {
        const std::size_t pos = text.find(marker, start);
        if (pos == std::string::npos) {
            parts.push_back(trim_edges(text.substr(start)));
            break;
        }
        parts.push_back(trim_edges(text.substr(start, pos - start)));
        start = pos + marker.size();
    }

    while (!parts.empty() && parts.back().empty()) {
        parts.pop_back();
    }

    if (parts.size() != expected_parts) {
        return {};
    }
    for (const auto& p : parts) {
        if (p.empty()) {
            return {};
        }
    }
    return parts;
}

std::string merge_source_zh(const std::vector<Segment>& segments, const std::vector<std::size_t>& indices) {
    const std::string delim = std::string("\n") + k_coalesce_marker + "\n";
    std::string out;
    for (std::size_t j = 0; j < indices.size(); ++j) {
        if (j > 0) {
            out += delim;
        }
        out += segments[indices[j]].source_zh;
    }
    return out;
}

int compute_batch_max_output_tokens(int per_segment_cap, std::size_t batch_size, int n_ctx) {
    const int per = std::max(1, per_segment_cap);
    const auto n = static_cast<long long>(batch_size);
    const long long raw = static_cast<long long>(per) * n;
    const int headroom = 384;
    const int cap = std::max(256, std::max(1, n_ctx) - headroom);
    if (raw > static_cast<long long>(cap)) {
        return cap;
    }
    return static_cast<int>(raw);
}
