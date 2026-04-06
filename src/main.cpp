#include "config.hpp"
#include "pipeline.hpp"
#include "sorting_filter.hpp"
#include "tei_reader.hpp"
#include "translator_llama.hpp"
#include "writer_md.hpp"
#include "writer_tei.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <pugixml.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr const char* kDefaultModelUrl =
    "https://huggingface.co/tencent/HY-MT1.5-1.8B-GGUF/resolve/main/HY-MT1.5-1.8B-Q8_0.gguf?download=true";
constexpr const char* kDefaultModelName = "HY-MT1.5-1.8B-Q8_0.gguf";
constexpr const char* kDefaultSortingDataName = "buddhist_metadata_analysis.json";

std::filesystem::path detect_runtime_dir(const char* argv0) {
#ifdef _WIN32
    char module_path[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, module_path, MAX_PATH);
    if (len > 0) {
        std::filesystem::path p(std::string(module_path, len));
        const auto parent = p.parent_path();
        if (!parent.empty()) {
            return parent;
        }
    }
#endif

    if (argv0 == nullptr || *argv0 == '\0') {
        return std::filesystem::current_path();
    }

    std::filesystem::path exe_path(argv0);
    std::error_code ec;
    if (exe_path.is_relative()) {
        exe_path = std::filesystem::current_path(ec) / exe_path;
    }
    exe_path = std::filesystem::weakly_canonical(exe_path, ec);
    if (!ec && !exe_path.empty()) {
        const auto parent = exe_path.parent_path();
        if (!parent.empty()) {
            return parent;
        }
    }

    return std::filesystem::current_path();
}

std::filesystem::path resolve_optional_path_with_runtime_dir(
    const std::filesystem::path& maybe_relative,
    const std::filesystem::path& runtime_dir
) {
    if (maybe_relative.empty()) {
        return maybe_relative;
    }
    if (maybe_relative.is_absolute()) {
        return maybe_relative;
    }
    return runtime_dir / maybe_relative;
}

std::filesystem::path derive_default_output_dir(const std::filesystem::path& input, bool input_is_dir) {
    std::filesystem::path base = input_is_dir ? input : input.parent_path();
    if (base.empty()) {
        base = std::filesystem::current_path();
    }

    const auto base_name = base.filename().string();
    const auto suffixed = (base_name.empty() ? std::string("translatedt") : base_name + "t");
    return base.parent_path() / suffixed;
}

std::string shell_single_quote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

bool has_xml_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".xml";
}

bool has_sorting_filters(const AppConfig& config) {
    return !config.filter_canon.empty()
        || !config.filter_tradition.empty()
        || !config.filter_period.empty()
        || !config.filter_origin.empty();
}

std::string join_values(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "(any)";
    }
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << values[i];
    }
    return out.str();
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

enum class DrilldownCategory {
    Canon,
    Tradition,
    Period,
    Origin,
};

std::string category_label(DrilldownCategory category) {
    switch (category) {
        case DrilldownCategory::Canon:
            return "Canon";
        case DrilldownCategory::Tradition:
            return "Tradition";
        case DrilldownCategory::Period:
            return "Dynasty/Period";
        case DrilldownCategory::Origin:
            return "Geography/Origin";
    }
    return "Unknown";
}

std::string category_key(DrilldownCategory category) {
    switch (category) {
        case DrilldownCategory::Canon:
            return "canon";
        case DrilldownCategory::Tradition:
            return "tradition";
        case DrilldownCategory::Period:
            return "period";
        case DrilldownCategory::Origin:
            return "origin";
    }
    return "unknown";
}

bool parse_category_token(const std::string& token, DrilldownCategory& out_category) {
    std::string normalized = trim_copy(token);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (normalized == "canon") {
        out_category = DrilldownCategory::Canon;
        return true;
    }
    if (normalized == "tradition" || normalized == "traditions" || normalized == "sect") {
        out_category = DrilldownCategory::Tradition;
        return true;
    }
    if (normalized == "period" || normalized == "dynasty" || normalized == "timeperiod") {
        out_category = DrilldownCategory::Period;
        return true;
    }
    if (normalized == "origin" || normalized == "geography" || normalized == "geo") {
        out_category = DrilldownCategory::Origin;
        return true;
    }

    return false;
}

void add_filter_value(SortingFilters& filters, DrilldownCategory category, const std::string& value) {
    switch (category) {
        case DrilldownCategory::Canon:
            filters.canon.push_back(value);
            break;
        case DrilldownCategory::Tradition:
            filters.tradition.push_back(value);
            break;
        case DrilldownCategory::Period:
            filters.period.push_back(value);
            break;
        case DrilldownCategory::Origin:
            filters.origin.push_back(value);
            break;
    }
}

bool parse_drilldown_term(
    const std::string& term,
    DrilldownCategory& out_category,
    std::string& out_value,
    std::string& error
) {
    const auto eq = term.find('=');
    const auto colon = term.find(':');
    const auto sep = eq != std::string::npos ? eq : colon;
    if (sep == std::string::npos || sep == 0 || sep + 1 >= term.size()) {
        error = "Invalid --drilldown term '" + term + "'. Expected category=value.";
        return false;
    }

    const auto category_token = trim_copy(term.substr(0, sep));
    out_value = trim_copy(term.substr(sep + 1));
    if (out_value.empty()) {
        error = "Invalid --drilldown term '" + term + "': missing value.";
        return false;
    }
    if (!parse_category_token(category_token, out_category)) {
        error = "Unknown drill-down category '" + category_token + "'.";
        return false;
    }
    return true;
}

bool build_filters_from_drilldown_terms(
    const std::vector<std::string>& terms,
    SortingFilters& out_filters,
    std::string& error
) {
    out_filters = {};
    std::unordered_map<std::string, std::size_t> category_hits;
    for (const auto& term : terms) {
        DrilldownCategory category = DrilldownCategory::Canon;
        std::string value;
        if (!parse_drilldown_term(term, category, value, error)) {
            return false;
        }
        add_filter_value(out_filters, category, value);
        ++category_hits[category_key(category)];
    }

    if (category_hits.empty()) {
        error = "No drill-down terms provided.";
        return false;
    }
    if (category_hits.size() > 2) {
        error = "--drilldown currently supports up to two categories.";
        return false;
    }
    for (const auto& [category, count] : category_hits) {
        if (count > 1) {
            error = "Category '" + category + "' specified multiple times in --drilldown.";
            return false;
        }
    }
    return true;
}

std::vector<std::filesystem::path> apply_sorting_filters(
    const std::vector<std::filesystem::path>& files,
    const SortingMetadataIndex& metadata_index,
    const std::filesystem::path& input_root,
    bool input_is_dir,
    const SortingFilters& filters
) {
    std::vector<std::filesystem::path> out;
    out.reserve(files.size());
    for (const auto& file : files) {
        if (metadata_index.match(file, input_root, input_is_dir, filters)) {
            out.push_back(file);
        }
    }
    return out;
}

std::vector<std::pair<std::string, std::size_t>> count_by_category(
    const std::vector<std::filesystem::path>& files,
    const SortingMetadataIndex& metadata_index,
    const std::filesystem::path& input_root,
    bool input_is_dir,
    DrilldownCategory category
) {
    std::unordered_map<std::string, std::size_t> counts;

    for (const auto& file : files) {
        const auto* rec = metadata_index.lookup(file, input_root, input_is_dir);
        if (rec == nullptr) {
            continue;
        }

        if (category == DrilldownCategory::Tradition) {
            std::vector<std::string> values = rec->traditions;
            if (values.empty()) {
                values.push_back("Unknown Tradition");
            }
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
            for (const auto& value : values) {
                const auto label = value.empty() ? "Unknown Tradition" : value;
                ++counts[label];
            }
            continue;
        }

        std::string value;
        if (category == DrilldownCategory::Canon) {
            value = rec->canon.empty() ? "Unknown" : rec->canon;
        } else if (category == DrilldownCategory::Period) {
            value = rec->period.empty() ? "Unknown Period" : rec->period;
        } else if (category == DrilldownCategory::Origin) {
            value = rec->origin.empty() ? "Unknown Origin" : rec->origin;
        }
        ++counts[value];
    }

    std::vector<std::pair<std::string, std::size_t>> out;
    out.reserve(counts.size());
    for (const auto& [key, count] : counts) {
        out.push_back({key, count});
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) {
            return a.second > b.second;
        }
        return a.first < b.first;
    });
    return out;
}

void print_options_with_counts(
    const std::vector<std::pair<std::string, std::size_t>>& options
) {
    for (std::size_t i = 0; i < options.size(); ++i) {
        std::cout
            << "  " << (i + 1) << ". "
            << options[i].first
            << " (" << options[i].second << ")\n";
    }
}

void print_drilldown_help_for_dataset(
    const std::vector<std::filesystem::path>& files,
    const SortingMetadataIndex& metadata_index,
    const std::filesystem::path& input_root,
    bool input_is_dir
) {
    const std::vector<DrilldownCategory> categories = {
        DrilldownCategory::Tradition,
        DrilldownCategory::Period,
        DrilldownCategory::Origin,
        DrilldownCategory::Canon,
    };

    std::cout << "Drill-down help\n";
    std::cout << "Category keys:\n";
    std::cout << "  canon\n";
    std::cout << "  tradition (alias: traditions, sect)\n";
    std::cout << "  period (alias: dynasty, timeperiod)\n";
    std::cout << "  origin (alias: geography, geo)\n\n";
    std::cout << "Syntax:\n";
    std::cout << "  --drilldown category=value\n";
    std::cout << "  --drilldown category:value\n";
    std::cout << "  (repeat --drilldown up to two categories)\n\n";
    std::cout << "Combination mode: AND across categories.\n";
    std::cout << "Supported category pairs:\n";
    for (std::size_t i = 0; i < categories.size(); ++i) {
        for (std::size_t j = i + 1; j < categories.size(); ++j) {
            std::cout << "  " << category_key(categories[i]) << " + " << category_key(categories[j]) << "\n";
        }
    }
    std::cout << "\n";

    std::size_t matched_records = 0;
    for (const auto& file : files) {
        if (metadata_index.lookup(file, input_root, input_is_dir) != nullptr) {
            ++matched_records;
        }
    }
    std::cout << "Dataset scope:\n";
    std::cout << "  input XML files: " << files.size() << "\n";
    std::cout << "  files with metadata records: " << matched_records << "\n\n";

    for (const auto category : categories) {
        std::cout << category_label(category) << " subcategories:\n";
        const auto options = count_by_category(files, metadata_index, input_root, input_is_dir, category);
        print_options_with_counts(options);
        std::cout << "\n";
    }

    std::cout << "Examples:\n";
    std::cout << "  --drilldown period=Tang --drilldown tradition=Chan/Zen\n";
    std::cout << "  --drilldown origin=\"Unknown Origin\"\n";
}

bool prompt_yes_no(const std::string& prompt, bool default_yes, bool& answer, bool& cancelled) {
    cancelled = false;
    while (true) {
        std::cout << prompt << (default_yes ? " [Y/n]: " : " [y/N]: ");
        std::string line;
        if (!std::getline(std::cin, line)) {
            cancelled = true;
            return false;
        }
        line = trim_copy(line);
        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (line.empty()) {
            answer = default_yes;
            return true;
        }
        if (line == "y" || line == "yes") {
            answer = true;
            return true;
        }
        if (line == "n" || line == "no") {
            answer = false;
            return true;
        }
        if (line == "q" || line == "quit") {
            cancelled = true;
            return false;
        }
        std::cout << "Please answer y or n (or q to cancel).\n";
    }
}

bool prompt_index_choice(
    const std::string& prompt,
    std::size_t max_value,
    std::size_t& out_index,
    bool& cancelled
) {
    cancelled = false;
    while (true) {
        std::cout << prompt;
        std::string line;
        if (!std::getline(std::cin, line)) {
            cancelled = true;
            return false;
        }
        line = trim_copy(line);
        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (line == "q" || line == "quit") {
            cancelled = true;
            return false;
        }
        try {
            const auto choice = static_cast<std::size_t>(std::stoull(line));
            if (choice >= 1 && choice <= max_value) {
                out_index = choice - 1;
                return true;
            }
        } catch (...) {
        }
        std::cout << "Invalid selection. Enter a number between 1 and " << max_value << " (or q to cancel).\n";
    }
}

bool interactive_drilldown_select(
    const SortingMetadataIndex& metadata_index,
    const std::filesystem::path& input_root,
    bool input_is_dir,
    const std::vector<std::filesystem::path>& all_files,
    SortingFilters& out_filters,
    std::vector<std::filesystem::path>& out_files,
    bool& cancelled,
    std::string& error
) {
    cancelled = false;
    out_filters = {};
    out_files = all_files;

    const std::vector<DrilldownCategory> categories = {
        DrilldownCategory::Tradition,
        DrilldownCategory::Period,
        DrilldownCategory::Origin,
        DrilldownCategory::Canon,
    };

    std::cout << "[drilldown] interactive selector\n";
    std::cout << "Choose primary category:\n";
    for (std::size_t i = 0; i < categories.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << category_label(categories[i]) << "\n";
    }

    std::size_t primary_category_index = 0;
    if (!prompt_index_choice("Primary category number (or q to cancel): ", categories.size(), primary_category_index, cancelled)) {
        return false;
    }
    const auto primary_category = categories[primary_category_index];

    const auto primary_options = count_by_category(out_files, metadata_index, input_root, input_is_dir, primary_category);
    if (primary_options.empty()) {
        error = "No metadata buckets available for selected primary category.";
        return false;
    }

    std::cout << "Primary subcategory options for " << category_label(primary_category) << ":\n";
    print_options_with_counts(primary_options);
    std::size_t primary_value_index = 0;
    if (!prompt_index_choice("Primary subcategory number (or q to cancel): ", primary_options.size(), primary_value_index, cancelled)) {
        return false;
    }
    const auto& primary_value = primary_options[primary_value_index].first;
    const auto primary_count = primary_options[primary_value_index].second;
    add_filter_value(out_filters, primary_category, primary_value);
    out_files = apply_sorting_filters(all_files, metadata_index, input_root, input_is_dir, out_filters);

    std::cout
        << "[drilldown] selected "
        << category_label(primary_category) << " = " << primary_value
        << " -> " << primary_count << " files\n";

    bool add_secondary = false;
    if (!prompt_yes_no("Add secondary drill-down?", false, add_secondary, cancelled)) {
        return false;
    }
    if (add_secondary) {
        std::vector<DrilldownCategory> secondary_categories;
        for (const auto category : categories) {
            if (category != primary_category) {
                secondary_categories.push_back(category);
            }
        }

        std::cout << "Choose secondary category:\n";
        for (std::size_t i = 0; i < secondary_categories.size(); ++i) {
            std::cout << "  " << (i + 1) << ". " << category_label(secondary_categories[i]) << "\n";
        }

        std::size_t secondary_category_index = 0;
        if (!prompt_index_choice("Secondary category number (or q to cancel): ", secondary_categories.size(), secondary_category_index, cancelled)) {
            return false;
        }
        const auto secondary_category = secondary_categories[secondary_category_index];
        const auto secondary_options = count_by_category(out_files, metadata_index, input_root, input_is_dir, secondary_category);
        if (secondary_options.empty()) {
            error = "No metadata buckets available for selected secondary category.";
            return false;
        }

        std::cout
            << "Secondary subcategory options for "
            << category_label(secondary_category)
            << " (within current selection):\n";
        print_options_with_counts(secondary_options);

        std::size_t secondary_value_index = 0;
        if (!prompt_index_choice("Secondary subcategory number (or q to cancel): ", secondary_options.size(), secondary_value_index, cancelled)) {
            return false;
        }
        const auto& secondary_value = secondary_options[secondary_value_index].first;
        const auto secondary_count = secondary_options[secondary_value_index].second;

        add_filter_value(out_filters, secondary_category, secondary_value);
        out_files = apply_sorting_filters(all_files, metadata_index, input_root, input_is_dir, out_filters);

        std::cout
            << "[drilldown] selected "
            << category_label(secondary_category) << " = " << secondary_value
            << " -> " << secondary_count << " files in subcategory\n";
    }

    if (out_files.empty()) {
        error = "Drill-down matched zero files.";
        return false;
    }

    std::cout
        << "[drilldown] final selection files=" << out_files.size()
        << " canon=" << join_values(out_filters.canon)
        << " tradition=" << join_values(out_filters.tradition)
        << " period=" << join_values(out_filters.period)
        << " origin=" << join_values(out_filters.origin)
        << "\n";

    bool start = false;
    if (!prompt_yes_no("Start translation job for this queue now?", true, start, cancelled)) {
        return false;
    }
    if (!start) {
        cancelled = true;
        return false;
    }

    return true;
}

bool collect_input_files(
    const std::filesystem::path& input,
    const std::filesystem::path& output_dir,
    std::vector<std::filesystem::path>& out_files,
    std::string& error
) {
    out_files.clear();

    if (!std::filesystem::exists(input)) {
        error = "Input path does not exist: " + input.string();
        return false;
    }

    if (std::filesystem::is_regular_file(input)) {
        if (!has_xml_extension(input)) {
            error = "Input file is not XML: " + input.string();
            return false;
        }
        out_files.push_back(input);
        return true;
    }

    if (!std::filesystem::is_directory(input)) {
        error = "Input path is neither file nor directory: " + input.string();
        return false;
    }

    std::error_code ec;
    const auto input_abs = std::filesystem::weakly_canonical(input, ec);
    const auto output_abs = std::filesystem::weakly_canonical(output_dir, ec);
    const bool skip_output_subtree = !ec && output_abs.string().starts_with(input_abs.string());

    for (const auto& entry : std::filesystem::recursive_directory_iterator(input)) {
        if (entry.is_regular_file() && has_xml_extension(entry.path())) {
            if (skip_output_subtree) {
                const auto entry_abs = std::filesystem::weakly_canonical(entry.path(), ec);
                if (!ec && entry_abs.string().starts_with(output_abs.string())) {
                    continue;
                }
            }
            out_files.push_back(entry.path());
        }
    }

    std::sort(out_files.begin(), out_files.end());

    if (out_files.empty()) {
        error = "No XML files found under: " + input.string();
        return false;
    }

    return true;
}

bool output_path_looks_like_xml_file(const std::filesystem::path& p) {
    if (p.empty()) {
        return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(p, ec)) {
        return std::filesystem::is_regular_file(p, ec);
    }
    return has_xml_extension(p);
}

bool ensure_model_available(std::string& model_path, std::string& error) {
    std::filesystem::path model(model_path);
    std::error_code ec;
    if (std::filesystem::exists(model, ec) && std::filesystem::is_regular_file(model, ec)) {
        return true;
    }

    // If only a filename is provided, resolve to current runtime directory.
    if (!model.has_parent_path()) {
        model = std::filesystem::current_path() / model;
    }

    const auto parent = model.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "Failed to create model directory: " + parent.string();
            return false;
        }
    }

    const auto partial = model.string() + ".part";

    std::cerr
        << "[model] missing model file at: " << model.string() << "\n"
        << "[model] downloading from: " << kDefaultModelUrl << "\n";

    const std::string cmd =
        "curl -L --fail --progress-bar -o " + shell_single_quote(partial) +
        " " + shell_single_quote(kDefaultModelUrl);
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::filesystem::remove(partial, ec);
        error = "Model download failed (curl exit code " + std::to_string(rc) + ")";
        return false;
    }

    std::filesystem::rename(partial, model, ec);
    if (ec) {
        std::filesystem::remove(partial, ec);
        error = "Failed to finalize downloaded model at: " + model.string();
        return false;
    }

    model_path = model.string();
    std::cerr << "[model] download complete: " << model_path << "\n";
    return true;
}

std::filesystem::path output_relative_for(
    const std::filesystem::path& input_root,
    bool root_is_dir,
    const std::filesystem::path& xml_file
) {
    if (root_is_dir) {
        return std::filesystem::relative(xml_file, input_root);
    }

    return xml_file.filename();
}

std::string format_progress_bar(double ratio, std::size_t width) {
    ratio = std::clamp(ratio, 0.0, 1.0);
    const std::size_t filled = static_cast<std::size_t>(ratio * static_cast<double>(width));
    std::string bar(width, '-');
    for (std::size_t i = 0; i < filled && i < width; ++i) {
        bar[i] = '=';
    }
    if (filled < width) {
        bar[filled] = '>';
    }
    return bar;
}

void print_progress(
    std::size_t file_index,
    std::size_t total_files,
    std::size_t done_segments,
    std::size_t total_segments,
    const std::string& current_file,
    bool done
) {
    if (total_files == 0) {
        return;
    }

    const double file_fraction = static_cast<double>(file_index) / static_cast<double>(total_files);
    const double segment_fraction = total_segments > 0
        ? static_cast<double>(done_segments) / static_cast<double>(total_segments)
        : 0.0;
    double overall_fraction = 0.0;
    if (file_index >= total_files) {
        overall_fraction = 1.0;
    } else {
        overall_fraction = file_fraction + segment_fraction / static_cast<double>(total_files);
    }
    overall_fraction = std::clamp(overall_fraction, 0.0, 1.0);
    const auto pct = static_cast<int>(overall_fraction * 100.0);

    std::ostringstream line;
    line
        << "\r["
        << format_progress_bar(overall_fraction, 30)
        << "] "
        << std::setw(3) << pct << "% "
        << "files " << file_index << "/" << total_files
        << " segments " << done_segments << "/" << total_segments
        << " " << current_file;

    std::cerr << line.str();
    if (done) {
        std::cerr << "\n";
    }
    std::cerr.flush();
}

std::size_t count_translation_notes_en(const std::filesystem::path& path, std::string& error) {
    pugi::xml_document doc;
    const pugi::xml_parse_result parse = doc.load_file(path.c_str(), pugi::parse_default);
    if (!parse) {
        error = "Failed to parse existing output XML " + path.string() + ": " + parse.description();
        return 0;
    }

    std::size_t count = 0;
    std::function<void(const pugi::xml_node&)> walk = [&](const pugi::xml_node& node) {
        if (node.type() == pugi::node_element) {
            std::string name = node.name();
            const auto colon = name.find(':');
            if (colon != std::string::npos) {
                name = name.substr(colon + 1);
            }
            if (name == "note") {
                const auto type = node.attribute("type");
                const auto lang = node.attribute("xml:lang");
                if (type && lang && std::string(type.value()) == "translation" && std::string(lang.value()) == "en") {
                    ++count;
                }
            }
        }
        for (const auto& child : node.children()) {
            walk(child);
        }
    };
    walk(doc.document_element());

    return count;
}

bool should_resume_skip_file(
    const std::filesystem::path& input_xml,
    const std::filesystem::path& output_xml,
    std::size_t expected_segments,
    bool resume_enabled,
    std::string& reason
) {
    if (!resume_enabled || !std::filesystem::exists(output_xml)) {
        return false;
    }

    std::error_code ec;
    const auto in_time = std::filesystem::last_write_time(input_xml, ec);
    if (ec) {
        reason = "cannot read input mtime";
        return false;
    }

    const auto out_time = std::filesystem::last_write_time(output_xml, ec);
    if (ec) {
        reason = "cannot read output mtime";
        return false;
    }

    if (out_time < in_time) {
        reason = "output older than input";
        return false;
    }

    std::string parse_error;
    const std::size_t note_count = count_translation_notes_en(output_xml, parse_error);
    if (!parse_error.empty()) {
        reason = parse_error;
        return false;
    }

    if (note_count == expected_segments) {
        reason = "output complete";
        return true;
    }

    reason = "note_count=" + std::to_string(note_count) + " expected=" + std::to_string(expected_segments);
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    AppConfig config;
    std::string error;

    if (!parse_args(argc, argv, config, error)) {
        if (error != "help") {
            std::cerr << "Argument error: " << error << "\n\n";
        }
        print_usage(argv[0]);
        return error == "help" ? 0 : 1;
    }

    {
        const unsigned hc = std::thread::hardware_concurrency();
        std::cout << "[config] workers=" << config.workers << " llama_threads=" << config.n_threads
                  << " (workers*llama_threads=" << (config.workers * static_cast<std::size_t>(config.n_threads));
        if (hc == 0) {
            std::cout << ", hardware_concurrency=unknown)\n";
        } else {
            std::cout << ", hardware_concurrency=" << hc << ")\n";
        }
        std::cout << "[config] segment_coalesce=" << (config.coalesce_segments ? "on" : "off")
                  << " coalesce_max_batch=" << config.coalesce_max_batch
                  << " coalesce_max_chars=" << config.coalesce_max_merged_chars << "\n";
        std::cout << "[config] ctx=" << config.n_ctx << " max_ctx=" << config.max_n_ctx << " (auto-grow on)\n";
    }

    const auto runtime_dir = detect_runtime_dir(argv[0]);
    if (config.model_path.empty()) {
        config.model_path = kDefaultModelName;
    }
    config.model_path = resolve_optional_path_with_runtime_dir(config.model_path, runtime_dir).string();

    const bool needs_sorting_data =
        has_sorting_filters(config) || config.interactive_drilldown || config.drilldown_help || !config.drilldown_select.empty();
    if (needs_sorting_data) {
        if (config.sorting_data_path.empty()) {
            config.sorting_data_path = kDefaultSortingDataName;
        }
        config.sorting_data_path = resolve_optional_path_with_runtime_dir(config.sorting_data_path, runtime_dir);
    }

    if (!std::filesystem::exists(config.input_path)) {
        std::cerr << "Input path does not exist: " << config.input_path << "\n";
        return 1;
    }
    const bool input_is_dir = std::filesystem::is_directory(config.input_path);
    if (config.output_dir.empty() && !config.drilldown_help) {
        config.output_dir = derive_default_output_dir(config.input_path, input_is_dir);
        std::cout << "[config] default output=" << config.output_dir << "\n";
    }

    const auto scan_output_anchor = config.output_dir.empty()
        ? (std::filesystem::current_path() / "__tei_mt_no_output__")
        : config.output_dir;

    std::vector<std::filesystem::path> input_files;
    if (!collect_input_files(config.input_path, scan_output_anchor, input_files, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    if (has_sorting_filters(config) || config.interactive_drilldown || config.drilldown_help || !config.drilldown_select.empty()) {
        SortingMetadataIndex metadata_index;
        if (!metadata_index.load(config.sorting_data_path, error)) {
            std::cerr << "[fatal] " << error << "\n";
            return 1;
        }

        if (config.drilldown_help) {
            print_drilldown_help_for_dataset(
                input_files,
                metadata_index,
                config.input_path,
                input_is_dir
            );
            return 0;
        } else if (config.interactive_drilldown) {
            SortingFilters interactive_filters;
            std::vector<std::filesystem::path> interactive_files;
            bool cancelled = false;
            if (!interactive_drilldown_select(
                    metadata_index,
                    config.input_path,
                    input_is_dir,
                    input_files,
                    interactive_filters,
                    interactive_files,
                    cancelled,
                    error
                )) {
                if (cancelled) {
                    std::cout << "[drilldown] cancelled by user.\n";
                    return 0;
                }
                std::cerr << "[fatal] " << error << "\n";
                return 1;
            }

            input_files.swap(interactive_files);
        } else if (!config.drilldown_select.empty()) {
            SortingFilters drilldown_filters;
            if (!build_filters_from_drilldown_terms(config.drilldown_select, drilldown_filters, error)) {
                std::cerr << "[fatal] " << error << "\n";
                return 1;
            }

            std::vector<std::filesystem::path> filtered_files = apply_sorting_filters(
                input_files,
                metadata_index,
                config.input_path,
                input_is_dir,
                drilldown_filters
            );

            std::cout
                << "[drilldown] canon=" << join_values(drilldown_filters.canon)
                << " tradition=" << join_values(drilldown_filters.tradition)
                << " period=" << join_values(drilldown_filters.period)
                << " origin=" << join_values(drilldown_filters.origin)
                << " matched=" << filtered_files.size() << "/" << input_files.size()
                << "\n" << std::flush;

            if (filtered_files.empty()) {
                std::cerr << "[fatal] Drill-down matched zero XML files.\n";
                return 1;
            }

            input_files.swap(filtered_files);
        } else {
            SortingFilters filters;
            filters.canon = config.filter_canon;
            filters.tradition = config.filter_tradition;
            filters.period = config.filter_period;
            filters.origin = config.filter_origin;

            std::vector<std::filesystem::path> filtered_files = apply_sorting_filters(
                input_files,
                metadata_index,
                config.input_path,
                input_is_dir,
                filters
            );

            std::cout
                << "[filter] sorting-data=" << config.sorting_data_path
                << " canon=" << join_values(config.filter_canon)
                << " tradition=" << join_values(config.filter_tradition)
                << " period=" << join_values(config.filter_period)
                << " origin=" << join_values(config.filter_origin)
                << " matched=" << filtered_files.size() << "/" << input_files.size()
                << "\n" << std::flush;

            if (filtered_files.empty()) {
                std::cerr << "[fatal] Metadata filters matched zero XML files.\n";
                return 1;
            }

            input_files.swap(filtered_files);
        }
    }

    const bool output_is_single_xml_file = !input_is_dir && output_path_looks_like_xml_file(config.output_dir);
    if (input_is_dir && output_is_single_xml_file) {
        std::cerr << "For directory input, --output must be a directory path.\n";
        return 1;
    }

    if (output_is_single_xml_file) {
        const auto parent = config.output_dir.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
    } else {
        std::filesystem::create_directories(config.output_dir);
    }

    if (!ensure_model_available(config.model_path, error)) {
        std::cerr << "[fatal] " << error << "\n";
        return 1;
    }

    LlamaTranslatorConfig translator_cfg;
    translator_cfg.model_path = config.model_path;
    translator_cfg.n_ctx = config.n_ctx;
    translator_cfg.max_n_ctx = config.max_n_ctx;
    translator_cfg.n_gpu_layers = config.n_gpu_layers;
    translator_cfg.n_threads = config.n_threads;
    translator_cfg.max_tokens = config.max_tokens;

    std::unique_ptr<LlamaTranslator> translator;
    try {
        translator = std::make_unique<LlamaTranslator>(translator_cfg);
    } catch (const std::exception& ex) {
        std::cerr << "[fatal] failed to initialize translator: " << ex.what() << "\n";
        return 1;
    }

    std::size_t total_segments = 0;
    std::chrono::milliseconds total_time{0};
    std::size_t files_ok = 0;
    std::size_t files_failed = 0;

    if (config.show_progress) {
        print_progress(0, input_files.size(), 0, 0, "", false);
    }

    for (std::size_t file_idx = 0; file_idx < input_files.size(); ++file_idx) {
        const auto& xml_file = input_files[file_idx];
        TeiDocument doc;
        if (!read_tei_file(xml_file, doc, error)) {
            std::cerr << "[skip] " << error << "\n";
            ++files_failed;
            continue;
        }

        std::filesystem::path rel_path;
        std::filesystem::path out_parent;
        std::filesystem::path tei_path;
        if (output_is_single_xml_file) {
            tei_path = config.output_dir;
            out_parent = tei_path.parent_path();
            rel_path = tei_path.filename();
        } else {
            rel_path = output_relative_for(config.input_path, input_is_dir, xml_file);
            out_parent = config.output_dir / rel_path.parent_path();
            tei_path = config.output_dir / rel_path;
        }

        std::string resume_reason;
        if (should_resume_skip_file(
                xml_file,
                tei_path,
                doc.segments.size(),
                config.resume,
                resume_reason
            )) {
            ++files_ok;
            if (config.show_progress) {
                print_progress(
                    file_idx + 1,
                    input_files.size(),
                    doc.segments.size(),
                    doc.segments.size(),
                    xml_file.filename().string(),
                    file_idx + 1 == input_files.size()
                );
            }
            std::cout << "[skip] " << xml_file.filename().string() << " " << resume_reason << "\n";
            continue;
        }

        std::vector<std::string> translations;
        TranslationStats stats;
        auto progress_callback = [&](std::size_t done_segments, std::size_t total_segments_in_file) {
            if (!config.show_progress) {
                return;
            }
            print_progress(
                file_idx,
                input_files.size(),
                done_segments,
                total_segments_in_file,
                xml_file.filename().string(),
                false
            );
        };

        const bool ok_translate = config.coalesce_segments
            ? translate_segments_coalesced_parallel(
                  doc.segments,
                  *translator,
                  config.workers,
                  CoalesceParams{
                      .enabled = true,
                      .max_per_batch = static_cast<std::size_t>(config.coalesce_max_batch),
                      .max_merged_chars = static_cast<std::size_t>(config.coalesce_max_merged_chars),
                      .max_tokens_per_segment = config.max_tokens,
                      .n_ctx = config.n_ctx,
                  },
                  translations,
                  stats,
                  error,
                  progress_callback
              )
            : translate_segments_parallel(
                  doc.segments,
                  *translator,
                  config.workers,
                  translations,
                  stats,
                  error,
                  progress_callback
              );

        if (!ok_translate) {
            std::cerr << "[error] translation failed for " << xml_file << ": " << error << "\n";
            ++files_failed;
            continue;
        }

        std::filesystem::create_directories(out_parent);

        if (config.emit_markdown) {
            std::filesystem::path md_path;
            if (output_is_single_xml_file) {
                md_path = tei_path;
                md_path.replace_extension(".en.md");
            } else {
                auto md_name = rel_path.filename();
                md_name.replace_extension(".en.md");
                md_path = out_parent / md_name;
            }
            if (!write_markdown_output(md_path, doc, translations, error)) {
                std::cerr << "[error] markdown write failed for " << xml_file << ": " << error << "\n";
                ++files_failed;
                continue;
            }
        }

        if (!write_tei_note_output(
                tei_path,
                doc,
                translations,
                config.overwrite_existing_translations,
                error
            )) {
            std::cerr << "[error] TEI write failed for " << xml_file << ": " << error << "\n";
            ++files_failed;
            continue;
        }

        total_segments += stats.segments_total;
        total_time += stats.wall_time;
        ++files_ok;

        if (config.show_progress) {
            print_progress(
                file_idx + 1,
                input_files.size(),
                stats.segments_total,
                stats.segments_total,
                xml_file.filename().string(),
                file_idx + 1 == input_files.size()
            );
        }

        std::cout
            << "[ok] " << xml_file.filename().string()
            << " segments=" << stats.segments_total
            << " units=" << stats.translation_units
            << " coalesce_fallbacks=" << stats.coalesce_fallback_units
            << " workers=" << stats.workers_used
            << " time_ms=" << stats.wall_time.count()
            << " ms_per_segment=" << stats.ms_per_segment
            << " seg_per_sec=" << stats.segments_per_second
            << "\n";
    }

    const double total_seconds = static_cast<double>(total_time.count()) / 1000.0;
    const double total_sps = total_seconds > 0.0 ? static_cast<double>(total_segments) / total_seconds : 0.0;

    std::cout
        << "[summary] files=" << input_files.size()
        << " ok=" << files_ok
        << " failed=" << files_failed
        << " total_segments=" << total_segments
        << " total_time_ms=" << total_time.count()
        << " seg_per_sec=" << total_sps
        << "\n";

    return 0;
}
