#pragma once

#include "tei_reader.hpp"

#include <filesystem>
#include <string>
#include <vector>

bool write_markdown_output(
    const std::filesystem::path& out_path,
    const TeiDocument& doc,
    const std::vector<std::string>& translations,
    std::string& error
);
