#include "tei_reader.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace {

std::string local_name(const char* raw_name) {
    if (raw_name == nullptr) {
        return {};
    }
    std::string name(raw_name);
    const auto pos = name.find(':');
    if (pos == std::string::npos) {
        return name;
    }
    return name.substr(pos + 1);
}

bool is_translatable_tag(const std::string& name) {
    static const std::unordered_set<std::string> tags = {"p", "l", "ab", "head", "seg"};
    return tags.contains(name);
}

bool should_skip_text_subtree(const std::string& name) {
    static const std::unordered_set<std::string> skip_tags = {
        "note", "pb", "lb", "cb", "fw", "ref", "anchor", "milestone"
    };
    return skip_tags.contains(name);
}

std::string normalize_whitespace(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    bool in_space = false;

    for (unsigned char ch : input) {
        if (std::isspace(ch)) {
            if (!in_space) {
                out.push_back(' ');
                in_space = true;
            }
        } else {
            out.push_back(static_cast<char>(ch));
            in_space = false;
        }
    }

    while (!out.empty() && out.front() == ' ') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }

    return out;
}

void collect_text(const pugi::xml_node& node, std::string& out) {
    if (node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata) {
        out.append(node.value());
        out.push_back(' ');
        return;
    }

    if (node.type() != pugi::node_element) {
        return;
    }

    const auto name = local_name(node.name());
    if (should_skip_text_subtree(name)) {
        return;
    }

    for (const auto& child : node.children()) {
        collect_text(child, out);
    }
}

std::string node_id_or_fallback(const pugi::xml_node& node, std::size_t index) {
    if (const auto attr = node.attribute("xml:id")) {
        return attr.value();
    }
    if (const auto attr = node.attribute("id")) {
        return attr.value();
    }

    std::ostringstream oss;
    oss << "seg-" << index;
    return oss.str();
}

void collect_segments(
    const pugi::xml_node& node,
    bool in_header,
    bool in_body,
    TeiDocument& out_doc
) {
    if (node.type() != pugi::node_element) {
        return;
    }

    const auto name = local_name(node.name());
    const bool now_in_header = in_header || name == "teiHeader";
    const bool now_in_body = in_body || name == "body";

    if (now_in_header) {
        for (const auto& child : node.children()) {
            collect_segments(child, now_in_header, now_in_body, out_doc);
        }
        return;
    }

    if (now_in_body && is_translatable_tag(name)) {
        std::string raw_text;
        collect_text(node, raw_text);
        const std::string normalized = normalize_whitespace(raw_text);

        if (!normalized.empty()) {
            Segment segment;
            segment.index = out_doc.segments.size();
            segment.id = node_id_or_fallback(node, segment.index);
            segment.source_zh = normalized;

            out_doc.segments.push_back(std::move(segment));
            out_doc.segment_nodes.push_back(node);
        }

        // paragraph-level segmentation: do not recurse into nested translatable nodes.
        return;
    }

    for (const auto& child : node.children()) {
        collect_segments(child, now_in_header, now_in_body, out_doc);
    }
}

}  // namespace

bool read_tei_file(const std::filesystem::path& path, TeiDocument& out_doc, std::string& error) {
    out_doc = TeiDocument{};
    out_doc.source_path = path;

    pugi::xml_parse_result parse = out_doc.xml.load_file(path.c_str(), pugi::parse_default | pugi::parse_ws_pcdata);
    if (!parse) {
        error = "Failed to parse XML " + path.string() + ": " + parse.description();
        return false;
    }

    const auto root = out_doc.xml.document_element();
    if (!root) {
        error = "No root element in XML: " + path.string();
        return false;
    }

    collect_segments(root, false, false, out_doc);

    if (out_doc.segments.empty()) {
        error = "No translatable segments found in " + path.string();
        return false;
    }

    return true;
}
