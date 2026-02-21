#pragma once

#include "segment.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <pugixml.hpp>

struct TeiDocument {
    std::filesystem::path source_path;
    pugi::xml_document xml;
    std::vector<Segment> segments;
    std::vector<pugi::xml_node> segment_nodes;
};

bool read_tei_file(const std::filesystem::path& path, TeiDocument& out_doc, std::string& error);
