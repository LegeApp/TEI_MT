#include "sorting_filter.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace {

std::string normalize_text(std::string value) {
    const auto not_space = [](unsigned char c) {
        return !std::isspace(c);
    };
    const auto first = std::find_if(value.begin(), value.end(), not_space);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
    std::string out(first, last);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string normalize_path_for_key(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');

    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    const std::string needle = "/xml-p5/";
    const auto pos = lowered.find(needle);
    if (pos != std::string::npos) {
        value = value.substr(pos + needle.size());
    }

    while (!value.empty() && (value.front() == '/' || value.front() == '\\')) {
        value.erase(value.begin());
    }

    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string make_file_key(
    const std::filesystem::path& xml_file,
    const std::filesystem::path& input_root,
    bool input_is_dir
) {
    std::filesystem::path rel;
    if (input_is_dir) {
        std::error_code ec;
        rel = std::filesystem::relative(xml_file, input_root, ec);
        if (ec) {
            rel = xml_file.filename();
        }
    } else {
        rel = xml_file.filename();
    }

    return normalize_path_for_key(rel.generic_string());
}

std::unordered_set<std::string> make_filter_set(const std::vector<std::string>& values) {
    std::unordered_set<std::string> out;
    out.reserve(values.size());
    for (const auto& value : values) {
        const auto normalized = normalize_text(value);
        if (!normalized.empty()) {
            out.insert(normalized);
        }
    }
    return out;
}

bool match_scalar(const std::string& value, const std::unordered_set<std::string>& filter_set) {
    if (filter_set.empty()) {
        return true;
    }
    return filter_set.find(normalize_text(value)) != filter_set.end();
}

bool match_traditions(
    const std::vector<std::string>& traditions,
    const std::unordered_set<std::string>& filter_set
) {
    if (filter_set.empty()) {
        return true;
    }
    for (const auto& tradition : traditions) {
        if (filter_set.find(normalize_text(tradition)) != filter_set.end()) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool SortingMetadataIndex::load(const std::filesystem::path& json_path, std::string& error) {
    records_.clear();

    std::ifstream in(json_path);
    if (!in.is_open()) {
        error = "Failed to open sorting data: " + json_path.string();
        return false;
    }

    nlohmann::json data;
    try {
        in >> data;
    } catch (const std::exception& ex) {
        error = "Failed to parse sorting data JSON: " + std::string(ex.what());
        return false;
    }

    if (!data.contains("detailed_analysis") || !data["detailed_analysis"].is_array()) {
        error = "Sorting data JSON missing array: detailed_analysis";
        return false;
    }

    for (const auto& item : data["detailed_analysis"]) {
        if (!item.is_object()) {
            continue;
        }
        const std::string file = item.value("file", "");
        if (file.empty()) {
            continue;
        }

        const auto key = normalize_path_for_key(file);
        if (key.empty()) {
            continue;
        }

        SortingMetadataRecord rec;
        rec.canon = item.value("canon", "Unknown");
        rec.period = item.value("period", "Unknown Period");
        rec.origin = item.value("origin", "Unknown Origin");

        if (item.contains("traditions") && item["traditions"].is_array()) {
            for (const auto& t : item["traditions"]) {
                if (t.is_string()) {
                    rec.traditions.push_back(t.get<std::string>());
                }
            }
        }
        if (rec.traditions.empty()) {
            rec.traditions.push_back("Unknown Tradition");
        }

        records_[key] = std::move(rec);
    }

    if (records_.empty()) {
        error = "Sorting data loaded but no usable records were found.";
        return false;
    }

    return true;
}

const SortingMetadataRecord* SortingMetadataIndex::lookup(
    const std::filesystem::path& xml_file,
    const std::filesystem::path& input_root,
    bool input_is_dir
) const {
    const auto key = make_file_key(xml_file, input_root, input_is_dir);
    const auto it = records_.find(key);
    if (it == records_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool SortingMetadataIndex::match(
    const std::filesystem::path& xml_file,
    const std::filesystem::path& input_root,
    bool input_is_dir,
    const SortingFilters& filters
) const {
    const auto* rec = lookup(xml_file, input_root, input_is_dir);
    if (rec == nullptr) {
        return false;
    }

    const auto canon_filters = make_filter_set(filters.canon);
    const auto tradition_filters = make_filter_set(filters.tradition);
    const auto period_filters = make_filter_set(filters.period);
    const auto origin_filters = make_filter_set(filters.origin);

    return match_scalar(rec->canon, canon_filters)
        && match_traditions(rec->traditions, tradition_filters)
        && match_scalar(rec->period, period_filters)
        && match_scalar(rec->origin, origin_filters);
}
