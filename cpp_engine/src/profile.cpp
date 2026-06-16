#include "dfee/profile.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace dfee {
namespace {

[[nodiscard]] std::string trim(std::string value) {
    const auto first = std::ranges::find_if_not(value, [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

[[nodiscard]] std::string join_path(const std::string& prefix, const std::string& key) {
    if (prefix.empty()) {
        return key;
    }
    return prefix + "." + key;
}

[[nodiscard]] std::string scalar_to_string(const YAML::Node& node) {
    return trim(node.as<std::string>());
}

[[nodiscard]] bool try_parse_double(const std::string& value, double& out) {
    try {
        size_t consumed = 0;
        out = std::stod(value, &consumed);
        return consumed == value.size();
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::vector<double> parse_numeric_sequence(const YAML::Node& node) {
    std::vector<double> values;
    values.reserve(node.size());
    for (const auto& item : node) {
        if (!item.IsScalar()) {
            throw std::runtime_error("Expected a scalar numeric sequence item.");
        }
        values.push_back(item.as<double>());
    }
    return values;
}

template <typename TProfile>
void flatten_yaml_node(
    const YAML::Node& node,
    const std::string& prefix,
    TProfile& profile) {
    if (node.IsMap()) {
        for (const auto& entry : node) {
            if (!entry.first.IsScalar()) {
                throw std::runtime_error("Profile keys must be scalar strings.");
            }
            flatten_yaml_node(entry.second, join_path(prefix, scalar_to_string(entry.first)), profile);
        }
        return;
    }

    if (node.IsSequence()) {
        if (node.size() == 0) {
            profile.string_values[prefix] = "[]";
            return;
        }
        profile.numeric_arrays[prefix] = parse_numeric_sequence(node);
        return;
    }

    if (node.IsScalar()) {
        const std::string value = scalar_to_string(node);
        double number = 0.0;
        if (try_parse_double(value, number)) {
            profile.numeric_values[prefix] = number;
        } else {
            profile.string_values[prefix] = value;
        }
        return;
    }

    if (node.IsNull()) {
        profile.string_values[prefix] = "null";
        return;
    }

    throw std::runtime_error("Unsupported YAML node type in profile.");
}

[[nodiscard]] YAML::Node load_yaml_document(const std::filesystem::path& path, const std::string& kind) {
    try {
        const YAML::Node root = YAML::LoadFile(path.string());
        if (!root || root.IsNull()) {
            throw std::runtime_error(kind + " profile must contain a YAML mapping at the top level.");
        }
        if (!root.IsMap()) {
            throw std::runtime_error(kind + " profile must contain a YAML mapping at the top level.");
        }
        return root;
    } catch (const YAML::Exception& ex) {
        throw std::runtime_error("Failed to parse YAML file " + path.string() + ": " + ex.what());
    }
}

void require_mapping_key(const YAML::Node& root, const std::string& key, const std::filesystem::path& path, const std::string& kind) {
    const YAML::Node node = root[key];
    if (!node || node.IsNull()) {
        throw std::runtime_error(kind + " profile " + path.string() + " is missing required section: '" + key + "'");
    }
}

void validate_film_stock_profile_schema(const YAML::Node& root, const std::filesystem::path& path) {
    static constexpr const char* kRequiredKeys[] = {
        "stock_id",
        "stock_name",
        "stock_type",
        "adaptation",
        "tone_response",
        "color_response",
        "hue_saturation_response",
        "grain",
        "halation",
    };
    for (const char* key : kRequiredKeys) {
        require_mapping_key(root, key, path, "Film stock");
    }
    if (!root["stock_id"].IsScalar()) {
        throw std::runtime_error("Film stock profile " + path.string() + " has a non-scalar stock_id.");
    }
    if (!root["stock_name"].IsScalar()) {
        throw std::runtime_error("Film stock profile " + path.string() + " has a non-scalar stock_name.");
    }
    if (!root["stock_type"].IsScalar()) {
        throw std::runtime_error("Film stock profile " + path.string() + " has a non-scalar stock_type.");
    }
    if (!root["adaptation"].IsMap() || !root["tone_response"].IsMap() || !root["color_response"].IsMap() ||
        !root["hue_saturation_response"].IsMap() || !root["grain"].IsMap() || !root["halation"].IsMap()) {
        throw std::runtime_error("Film stock profile " + path.string() + " has one or more required sections with the wrong YAML type.");
    }
}

void validate_print_stock_profile_schema(const YAML::Node& root, const std::filesystem::path& path) {
    static constexpr const char* kRequiredKeys[] = {
        "print_stock_id",
        "print_stock_name",
        "tone",
        "color",
        "grain",
    };
    for (const char* key : kRequiredKeys) {
        require_mapping_key(root, key, path, "Print stock");
    }
    if (!root["print_stock_id"].IsScalar()) {
        throw std::runtime_error("Print stock profile " + path.string() + " has a non-scalar print_stock_id.");
    }
    if (!root["print_stock_name"].IsScalar()) {
        throw std::runtime_error("Print stock profile " + path.string() + " has a non-scalar print_stock_name.");
    }
    if (!root["tone"].IsMap() || !root["color"].IsMap() || !root["grain"].IsMap()) {
        throw std::runtime_error("Print stock profile " + path.string() + " has one or more required sections with the wrong YAML type.");
    }
}

template <typename TProfile>
void flatten_profile_document(const YAML::Node& root, TProfile& profile) {
    for (const auto& entry : root) {
        if (!entry.first.IsScalar()) {
            throw std::runtime_error("Profile keys must be scalar strings.");
        }
        flatten_yaml_node(entry.second, scalar_to_string(entry.first), profile);
    }
}

template <typename TProfile>
std::vector<TProfile> list_profiles_from_directory(
    const std::filesystem::path& directory,
    TProfile (*loader)(const std::filesystem::path&)) {
    std::vector<TProfile> profiles;
    if (!std::filesystem::is_directory(directory)) {
        return profiles;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".yaml") {
            continue;
        }
        try {
            profiles.push_back(loader(entry.path()));
        } catch (...) {
            continue;
        }
    }
    return profiles;
}

}  // namespace

std::string to_string(const StockType stock_type) {
    switch (stock_type) {
        case StockType::ColorNegative:
            return "color_negative";
        case StockType::ColorReversal:
            return "color_reversal";
        case StockType::Monochrome:
            return "monochrome";
    }
    return "color_negative";
}

StockType parse_stock_type(const std::string& value) {
    if (value == "color_negative") {
        return StockType::ColorNegative;
    }
    if (value == "color_reversal") {
        return StockType::ColorReversal;
    }
    if (value == "monochrome" || value == "black_and_white") {
        return StockType::Monochrome;
    }
    throw std::runtime_error("Invalid stock_type: " + value);
}

FilmStockProfile load_film_stock_profile(const std::filesystem::path& path) {
    const YAML::Node root = load_yaml_document(path, "Film stock");
    validate_film_stock_profile_schema(root, path);

    FilmStockProfile profile;
    profile.path = path;
    profile.stock_id = root["stock_id"].as<std::string>();
    profile.stock_name = root["stock_name"].as<std::string>();
    profile.stock_type = parse_stock_type(root["stock_type"].as<std::string>());
    flatten_profile_document(root, profile);
    return profile;
}

PrintStockProfile load_print_stock_profile(const std::filesystem::path& path) {
    const YAML::Node root = load_yaml_document(path, "Print stock");
    validate_print_stock_profile_schema(root, path);

    PrintStockProfile profile;
    profile.path = path;
    profile.print_stock_id = root["print_stock_id"].as<std::string>();
    profile.print_stock_name = root["print_stock_name"].as<std::string>();
    flatten_profile_document(root, profile);
    return profile;
}

std::vector<FilmStockProfile> list_film_stock_profiles(const std::filesystem::path& directory) {
    auto profiles = list_profiles_from_directory(directory, load_film_stock_profile);
    std::ranges::sort(profiles, {}, &FilmStockProfile::stock_name);
    return profiles;
}

std::vector<PrintStockProfile> list_print_stock_profiles(const std::filesystem::path& directory) {
    auto profiles = list_profiles_from_directory(directory, load_print_stock_profile);
    std::ranges::sort(profiles, {}, &PrintStockProfile::print_stock_name);
    return profiles;
}

}  // namespace dfee
