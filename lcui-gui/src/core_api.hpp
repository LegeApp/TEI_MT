#pragma once

#include "progress_event.hpp"

#include <atomic>
#include <functional>
#include <string>

namespace tei_mt_gui {

struct RunConfig {
    std::string tei_mt_path;
    std::string input_path;
    std::string output_path;
    std::string model_path;
    int workers = 2;
    int threads = 8;
    int ctx = 2048;
    int max_tokens = 192;
    int n_gpu_layers = -1;
    bool emit_markdown = false;
    bool no_resume = false;
    bool overwrite_existing = false;
    bool no_progress = true;
};

struct RunControl {
    std::atomic<bool> cancel_requested{false};
    std::atomic<bool> pause_requested{false};
};

using ProgressCallback = std::function<void(const ProgressEvent&)>;

bool run_translation_process(const RunConfig& cfg, RunControl& control, const ProgressCallback& callback);

}  // namespace tei_mt_gui
