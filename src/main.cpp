#include "config.hpp"
#include "pipeline.hpp"
#include "tei_reader.hpp"
#include "translator_llama.hpp"
#include "writer_md.hpp"
#include "writer_tei.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <pugixml.hpp>

namespace {

bool has_xml_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".xml";
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

    std::vector<std::filesystem::path> input_files;
    if (!collect_input_files(config.input_path, config.output_dir, input_files, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    std::filesystem::create_directories(config.output_dir);

    LlamaTranslatorConfig translator_cfg;
    translator_cfg.model_path = config.model_path;
    translator_cfg.n_ctx = config.n_ctx;
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

    const bool input_is_dir = std::filesystem::is_directory(config.input_path);

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

        const auto rel_path = output_relative_for(config.input_path, input_is_dir, xml_file);
        const auto out_parent = config.output_dir / rel_path.parent_path();
        const auto tei_path = config.output_dir / rel_path;

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

        if (!translate_segments_parallel(
                doc.segments,
                *translator,
                config.workers,
                translations,
                stats,
                error,
                progress_callback
            )) {
            std::cerr << "[error] translation failed for " << xml_file << ": " << error << "\n";
            ++files_failed;
            continue;
        }

        std::filesystem::create_directories(out_parent);

        if (config.emit_markdown) {
            auto md_name = rel_path.filename();
            md_name.replace_extension(".en.md");
            const auto md_path = out_parent / md_name;
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
