#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct AppConfig {
    std::filesystem::path input_path;
    std::filesystem::path output_dir;
    std::string model_path;
    std::size_t workers = 0;
    int max_tokens = 192;
    int n_ctx = 2048;
    /// Ceiling for automatic context growth (see LlamaTranslator).
    int max_n_ctx = 131072;
    int n_gpu_layers = -1;
    /// 0 = derive from hardware_concurrency and workers after parsing (see config.cpp).
    int n_threads = 0;
    bool coalesce_segments = true;
    int coalesce_max_batch = 6;
    int coalesce_max_merged_chars = 2800;
    std::string tei_strategy = "note";
    bool emit_markdown = false;
    bool show_progress = true;
    bool resume = true;
    bool overwrite_existing_translations = false;
    bool interactive_drilldown = false;
    bool drilldown_help = false;
    std::filesystem::path sorting_data_path;
    std::vector<std::string> drilldown_select;
    std::vector<std::string> filter_canon;
    std::vector<std::string> filter_tradition;
    std::vector<std::string> filter_period;
    std::vector<std::string> filter_origin;
};

void print_usage(const char* program_name);
bool parse_args(int argc, char** argv, AppConfig& config, std::string& error);
