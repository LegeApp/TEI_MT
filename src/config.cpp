#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

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

std::string trim_copy(std::string s) {
    const auto not_space = [](unsigned char c) {
        return !std::isspace(c);
    };
    const auto first = std::find_if(s.begin(), s.end(), not_space);
    if (first == s.end()) {
        return {};
    }
    const auto last = std::find_if(s.rbegin(), s.rend(), not_space).base();
    return std::string(first, last);
}

void append_csv_values(const std::string& value, std::vector<std::string>& out) {
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(',', begin);
        const auto token = trim_copy(value.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (!token.empty()) {
            out.push_back(token);
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
}

bool has_any_sorting_filter(const AppConfig& config) {
    return !config.filter_canon.empty()
        || !config.filter_tradition.empty()
        || !config.filter_period.empty()
        || !config.filter_origin.empty();
}

bool has_noninteractive_drilldown(const AppConfig& config) {
    return !config.drilldown_select.empty();
}

}  // namespace

void print_usage(const char* program_name) {
    std::cout
        << "Usage:\n"
        << "  " << program_name << " --input <tei-file-or-dir> [--output <out-dir-or-file.xml>] [--model <gguf-path>] [options]\n\n"
        << "Options:\n"
        << "  --workers <n>         Worker threads (0=auto: 2 or fewer for GPU offload, else up to 4 on CPU)\n"
        << "  --max-tokens <n>      Max generated tokens per segment (default: 192)\n"
        << "  --ctx <n>             Initial context size (default: 2048); may auto-grow up to --max-ctx\n"
        << "  --max-ctx <n>         Maximum context when auto-growing for long prompts (default: 131072)\n"
        << "  --n-gpu-layers <n>    llama.cpp GPU layers (default: -1)\n"
        << "  --threads <n>         llama.cpp CPU threads per context (0=auto: ~cores/workers; default: 0)\n"
        << "  --no-coalesce         Translate each TEI segment separately (disables batching)\n"
        << "  --coalesce-max-batch <n> Max segments merged per inference (default: 6)\n"
        << "  --coalesce-max-chars <n> Max UTF-8 chars per merged batch, approximate (default: 2800)\n"
        << "  --tei-strategy <s>    TEI output strategy, currently: note\n"
        << "  --emit-markdown       Also write sidecar Markdown output (*.en.md)\n"
        << "  --no-progress         Disable progress bar output\n"
        << "  --no-resume           Always reprocess files even if output looks complete\n"
        << "  --overwrite-existing-translations  Replace existing translation notes while writing\n"
        << "  --output <path>       Output path (default: input folder name + 't')\n"
        << "  --model <path>        GGUF model (default: HY-MT1.5-1.8B-Q8_0.gguf in exe directory)\n"
        << "  --interactive-drilldown  Interactive metadata drill-down selector\n"
        << "  --drilldown <expr>      Noninteractive drill-down selector (repeatable)\n"
        << "  --drilldown-help        Print drill-down categories/subcategories for current dataset\n"
        << "                          Requires: --input (sorting path defaults to exe directory)\n"
        << "                          Drilldown categories: canon, tradition, period, origin\n"
        << "                          Drilldown combos: any one or any two categories (AND)\n"
        << "                          Expr syntax: category=value or category:value\n"
        << "  --sorting-data <path> Buddhist metadata JSON (default: buddhist_metadata_analysis.json in exe directory)\n"
        << "  --filter-canon <v>    Canon filter (repeatable, CSV supported)\n"
        << "  --filter-tradition <v> Tradition filter (repeatable, CSV supported)\n"
        << "  --filter-period <v>   Historical period filter (repeatable, CSV supported)\n"
        << "  --filter-origin <v>   Geographic origin filter (repeatable, CSV supported)\n"
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
        } else if (arg == "--max-ctx") {
            if (!parse_int_arg(arg, require_value(arg), config.max_n_ctx, error)) {
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
            if (config.n_threads < 0) {
                error = "Invalid --threads: must be >= 0 (0 selects auto based on CPU cores and workers)";
                return false;
            }
        } else if (arg == "--no-coalesce") {
            config.coalesce_segments = false;
        } else if (arg == "--coalesce-max-batch") {
            if (!parse_int_arg(arg, require_value(arg), config.coalesce_max_batch, error)) {
                return false;
            }
            if (config.coalesce_max_batch < 1) {
                error = "--coalesce-max-batch must be >= 1";
                return false;
            }
        } else if (arg == "--coalesce-max-chars") {
            if (!parse_int_arg(arg, require_value(arg), config.coalesce_max_merged_chars, error)) {
                return false;
            }
            if (config.coalesce_max_merged_chars < 256) {
                error = "--coalesce-max-chars must be >= 256";
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
        } else if (arg == "--interactive-drilldown") {
            config.interactive_drilldown = true;
        } else if (arg == "--drilldown") {
            append_csv_values(require_value(arg), config.drilldown_select);
        } else if (arg == "--drilldown-help") {
            config.drilldown_help = true;
        } else if (arg == "--sorting-data") {
            config.sorting_data_path = require_value(arg);
        } else if (arg == "--filter-canon") {
            append_csv_values(require_value(arg), config.filter_canon);
        } else if (arg == "--filter-tradition") {
            append_csv_values(require_value(arg), config.filter_tradition);
        } else if (arg == "--filter-period") {
            append_csv_values(require_value(arg), config.filter_period);
        } else if (arg == "--filter-origin") {
            append_csv_values(require_value(arg), config.filter_origin);
        } else {
            error = "Unknown argument: " + arg;
            return false;
        }

        if (!error.empty()) {
            return false;
        }
    }

    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned cores = hw == 0 ? 4u : hw;

    if (config.workers == 0) {
        const bool cpu_only = (config.n_gpu_layers == 0);
        if (cpu_only) {
            config.workers = std::min<std::size_t>(4u, std::max<std::size_t>(1u, cores / 2));
        } else {
            config.workers = std::min<std::size_t>(2u, std::max<std::size_t>(1u, cores / 8));
        }
    }
    if (config.workers < 1) {
        config.workers = 1;
    }

    if (config.n_threads == 0) {
        config.n_threads = static_cast<int>(std::max(1u, cores / static_cast<unsigned>(config.workers)));
    }

    const auto thread_product = [&]() -> std::uint64_t {
        return static_cast<std::uint64_t>(config.workers) * static_cast<std::uint64_t>(config.n_threads);
    };
    const std::uint64_t cap = static_cast<std::uint64_t>(cores);
    while (thread_product() > cap) {
        if (config.n_threads > 1) {
            --config.n_threads;
        } else {
            config.workers = std::min(config.workers, static_cast<std::size_t>(cap));
            break;
        }
    }
    if (config.n_threads < 1) {
        config.n_threads = 1;
    }

    if (config.input_path.empty()) {
        error = "--input is required";
        return false;
    }
    if (config.interactive_drilldown && has_any_sorting_filter(config)) {
        error = "--interactive-drilldown cannot be combined with --filter-* arguments";
        return false;
    }
    if (config.interactive_drilldown && has_noninteractive_drilldown(config)) {
        error = "--interactive-drilldown cannot be combined with --drilldown";
        return false;
    }
    if (has_noninteractive_drilldown(config) && has_any_sorting_filter(config)) {
        error = "--drilldown cannot be combined with --filter-* arguments";
        return false;
    }

    if (!config.drilldown_help && config.tei_strategy != "note") {
        error = "Unsupported --tei-strategy: " + config.tei_strategy + " (supported: note)";
        return false;
    }

    if (config.n_ctx < 512) {
        error = "--ctx must be >= 512";
        return false;
    }
    if (config.max_n_ctx < config.n_ctx) {
        error = "--max-ctx must be >= --ctx";
        return false;
    }
    if (config.max_n_ctx > 1 << 22) {
        error = "--max-ctx unreasonably large";
        return false;
    }

    return true;
}
