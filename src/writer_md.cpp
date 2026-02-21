#include "writer_md.hpp"

#include <fstream>

bool write_markdown_output(
    const std::filesystem::path& out_path,
    const TeiDocument& doc,
    const std::vector<std::string>& translations,
    std::string& error
) {
    if (translations.size() != doc.segments.size()) {
        error = "Translation count does not match segment count for markdown writer";
        return false;
    }

    std::ofstream out(out_path);
    if (!out) {
        error = "Failed to open markdown output: " + out_path.string();
        return false;
    }

    out << "# " << doc.source_path.filename().string() << "\n\n";

    for (std::size_t i = 0; i < doc.segments.size(); ++i) {
        const auto& seg = doc.segments[i];
        const auto& translated = translations[i];

        out << "## Segment " << (i + 1) << " (" << seg.id << ")\n";
        out << "**Original (lzh):** " << seg.source_zh << "\n\n";
        out << "**English:** " << translated << "\n\n";
        out << "---\n\n";
    }

    return true;
}
