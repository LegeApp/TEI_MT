#pragma once

#include <filesystem>
#include <string>

struct AppConfig {
    std::filesystem::path input_path;
    std::filesystem::path output_dir;
    std::string model_path;
    std::size_t workers = 0;
    int max_tokens = 192;
    int n_ctx = 2048;
    int n_gpu_layers = -1;
    int n_threads = 8;
    std::string tei_strategy = "note";
    bool emit_markdown = false;
    bool show_progress = true;
    bool resume = true;
    bool overwrite_existing_translations = false;
};

void print_usage(const char* program_name);
bool parse_args(int argc, char** argv, AppConfig& config, std::string& error);
