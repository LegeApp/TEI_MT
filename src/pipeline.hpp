#pragma once

#include "segment.hpp"
#include "translator.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct TranslationStats {
    std::size_t segments_total = 0;
    std::size_t workers_used = 0;
    std::chrono::milliseconds wall_time{0};
    double segments_per_second = 0.0;
    double ms_per_segment = 0.0;
};

bool translate_segments_parallel(
    const std::vector<Segment>& segments,
    const Translator& prototype,
    std::size_t workers,
    std::vector<std::string>& out_translations,
    TranslationStats& out_stats,
    std::string& error,
    const std::function<void(std::size_t, std::size_t)>& progress_callback = {}
);
