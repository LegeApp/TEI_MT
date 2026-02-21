#include "config.hpp"

#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

bool parse_int_arg(const std::string& key, const std::string& value, int& out, std::string& error) {
    try {
        out = std::stoi(value);
        return true;
    } catch (...) {
        error = "Invalid integer for " + key + ": " + value;
        return false;
    }
}

bool parse_size_arg(const std::string& key, const std::string& value, std::size_t& out, std::string& error) {
    try {
        out = static_cast<std::size_t>(std::stoull(value));
        return true;
    } catch (...) {
        error = "Invalid integer for " + key + ": " + value;
        return false;
    }
}

}  // namespace

void print_usage(const char* program_name) {
    std::cout
        << "Usage:\n"
        << "  " << program_name << " --input <tei-file-or-dir> --output <out-dir> --model <gguf-path> [options]\n\n"
        << "Options:\n"
        << "  --workers <n>         Worker threads (default: hardware concurrency)\n"
        << "  --max-tokens <n>      Max generated tokens per segment (default: 192)\n"
        << "  --ctx <n>             Context size (default: 2048)\n"
        << "  --n-gpu-layers <n>    llama.cpp GPU layers (default: -1)\n"
        << "  --threads <n>         llama.cpp CPU threads per context (default: 8)\n"
        << "  --tei-strategy <s>    TEI output strategy, currently: note\n"
        << "  --emit-markdown       Also write sidecar Markdown output (*.en.md)\n"
        << "  --no-progress         Disable progress bar output\n"
        << "  --no-resume           Always reprocess files even if output looks complete\n"
        << "  --overwrite-existing-translations  Replace existing translation notes while writing\n"
        << "  -h, --help            Show this help\n";
}

bool parse_args(int argc, char** argv, AppConfig& config, std::string& error) {
    if (argc <= 1) {
        error = "No arguments provided";
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            error = "help";
            return false;
        }

        auto require_value = [&](const std::string& key) -> std::string {
            if (i + 1 >= argc) {
                error = "Missing value for " + key;
                return {};
            }
            ++i;
            return argv[i];
        };

        if (arg == "--input") {
            config.input_path = require_value(arg);
        } else if (arg == "--output") {
            config.output_dir = require_value(arg);
        } else if (arg == "--model") {
            config.model_path = require_value(arg);
        } else if (arg == "--workers") {
            std::size_t workers = 0;
            if (!parse_size_arg(arg, require_value(arg), workers, error)) {
                return false;
            }
            config.workers = workers;
        } else if (arg == "--max-tokens") {
            if (!parse_int_arg(arg, require_value(arg), config.max_tokens, error)) {
                return false;
            }
        } else if (arg == "--ctx") {
            if (!parse_int_arg(arg, require_value(arg), config.n_ctx, error)) {
                return false;
            }
        } else if (arg == "--n-gpu-layers") {
            if (!parse_int_arg(arg, require_value(arg), config.n_gpu_layers, error)) {
                return false;
            }
        } else if (arg == "--threads") {
            if (!parse_int_arg(arg, require_value(arg), config.n_threads, error)) {
                return false;
            }
        } else if (arg == "--tei-strategy") {
            config.tei_strategy = require_value(arg);
        } else if (arg == "--emit-markdown") {
            config.emit_markdown = true;
        } else if (arg == "--no-progress") {
            config.show_progress = false;
        } else if (arg == "--no-resume") {
            config.resume = false;
        } else if (arg == "--overwrite-existing-translations") {
            config.overwrite_existing_translations = true;
        } else {
            error = "Unknown argument: " + arg;
            return false;
        }

        if (!error.empty()) {
            return false;
        }
    }

    if (config.workers == 0) {
        const auto hw = std::thread::hardware_concurrency();
        config.workers = hw == 0 ? 4 : static_cast<std::size_t>(hw);
    }

    if (config.input_path.empty()) {
        error = "--input is required";
        return false;
    }
    if (config.output_dir.empty()) {
        error = "--output is required";
        return false;
    }
    if (config.model_path.empty()) {
        error = "--model is required";
        return false;
    }

    if (config.tei_strategy != "note") {
        error = "Unsupported --tei-strategy: " + config.tei_strategy + " (supported: note)";
        return false;
    }

    return true;
}
