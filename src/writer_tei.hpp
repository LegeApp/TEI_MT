#pragma once

#include "tei_reader.hpp"

#include <filesystem>
#include <string>
#include <vector>

bool write_tei_note_output(
    const std::filesystem::path& out_path,
    TeiDocument& doc,
    const std::vector<std::string>& translations,
    bool overwrite_existing_translations,
    std::string& error
);
