#include "writer_tei.hpp"

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

std::string prefixed_note_name(const pugi::xml_node& node) {
    const std::string node_name = node.name();
    const auto colon = node_name.find(':');
    if (colon == std::string::npos) {
        return "note";
    }
    return node_name.substr(0, colon) + ":note";
}

bool is_translation_note_en(const pugi::xml_node& node) {
    if (!node || node.type() != pugi::node_element) {
        return false;
    }
    if (local_name(node.name()) != "note") {
        return false;
    }
    const auto type = node.attribute("type");
    const auto lang = node.attribute("xml:lang");
    return type && lang && std::string(type.value()) == "translation" && std::string(lang.value()) == "en";
}

void remove_following_translation_notes(pugi::xml_node parent, pugi::xml_node anchor) {
    for (pugi::xml_node cur = anchor.next_sibling(); cur;) {
        pugi::xml_node next = cur.next_sibling();
        if (cur.type() == pugi::node_pcdata || cur.type() == pugi::node_cdata) {
            cur = next;
            continue;
        }
        if (is_translation_note_en(cur)) {
            parent.remove_child(cur);
            cur = next;
            continue;
        }
        break;
    }
}

}  // namespace

bool write_tei_note_output(
    const std::filesystem::path& out_path,
    TeiDocument& doc,
    const std::vector<std::string>& translations,
    bool overwrite_existing_translations,
    std::string& error
) {
    if (translations.size() != doc.segment_nodes.size()) {
        error = "Translation count does not match segment node count for TEI writer";
        return false;
    }

    for (std::size_t i = 0; i < doc.segment_nodes.size(); ++i) {
        auto node = doc.segment_nodes[i];
        auto parent = node.parent();
        if (!parent) {
            continue;
        }

        if (overwrite_existing_translations) {
            remove_following_translation_notes(parent, node);
        } else {
            pugi::xml_node first_after = node.next_sibling();
            while (first_after && (first_after.type() == pugi::node_pcdata || first_after.type() == pugi::node_cdata)) {
                first_after = first_after.next_sibling();
            }
            if (is_translation_note_en(first_after)) {
                continue;
            }
        }

        const std::string note_name = prefixed_note_name(node);
        auto note = parent.insert_child_after(note_name.c_str(), node);
        note.append_attribute("type") = "translation";
        note.append_attribute("xml:lang") = "en";
        note.text().set(translations[i].c_str());
    }

    if (!doc.xml.save_file(out_path.c_str(), "  ", pugi::format_default, pugi::encoding_utf8)) {
        error = "Failed to write translated TEI XML: " + out_path.string();
        return false;
    }

    return true;
}
