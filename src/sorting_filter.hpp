#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct SortingFilters {
    std::vector<std::string> canon;
    std::vector<std::string> tradition;
    std::vector<std::string> period;
    std::vector<std::string> origin;
};

struct SortingMetadataRecord {
    std::string canon;
    std::vector<std::string> traditions;
    std::string period;
    std::string origin;
};

class SortingMetadataIndex {
public:
    bool load(const std::filesystem::path& json_path, std::string& error);
    const SortingMetadataRecord* lookup(
        const std::filesystem::path& xml_file,
        const std::filesystem::path& input_root,
        bool input_is_dir
    ) const;
    bool match(
        const std::filesystem::path& xml_file,
        const std::filesystem::path& input_root,
        bool input_is_dir,
        const SortingFilters& filters
    ) const;

private:
    std::unordered_map<std::string, SortingMetadataRecord> records_;
};
