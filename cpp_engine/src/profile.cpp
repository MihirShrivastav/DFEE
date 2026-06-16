#include "dfee/profile.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
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

[[nodiscard]] std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
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

[[nodiscard]] std::vector<double> parse_number_array(std::string value) {
    value = trim(std::move(value));
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        return {};
    }
    value = value.substr(1, value.size() - 2);
    std::vector<double> values;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        double parsed = 0.0;
        item = trim(item);
        if (!item.empty() && try_parse_double(item, parsed)) {
            values.push_back(parsed);
        }
    }
    return values;
}

[[nodiscard]] int indentation(const std::string& line) {
    int count = 0;
    for (const char c : line) {
        if (c == ' ') {
            ++count;
            continue;
        }
        break;
    }
    return count;
}

[[nodiscard]] std::string join_path(const std::vector<std::string>& sections, const std::string& key) {
    std::string path;
    for (const auto& section : sections) {
        if (!path.empty()) {
            path += ".";
        }
        path += section;
    }
    if (!path.empty()) {
        path += ".";
    }
    path += key;
    return path;
}

struct ParsedYaml {
    std::unordered_map<std::string, double> numbers;
    std::unordered_map<std::string, std::vector<double>> arrays;
    std::unordered_map<std::string, std::string> strings;
};

[[nodiscard]] ParsedYaml parse_profile_yaml(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Unable to open profile: " + path.string());
    }

    ParsedYaml parsed;
    std::vector<std::string> sections;
    std::vector<int> indents;
    std::string line;
    std::string pending_list_key;
    while (std::getline(file, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        if (trim(line).empty()) {
            continue;
        }

        const int indent = indentation(line);
        std::string content = trim(line);
        while (!indents.empty() && indent <= indents.back()) {
            indents.pop_back();
            sections.pop_back();
        }

        if (content.starts_with("- ") && !pending_list_key.empty()) {
            parsed.strings[pending_list_key] += (parsed.strings[pending_list_key].empty() ? "" : "\n") + unquote(content.substr(2));
            continue;
        }

        const auto colon = content.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const std::string key = trim(content.substr(0, colon));
        const std::string value = trim(content.substr(colon + 1));
        if (key.empty()) {
            continue;
        }

        if (value.empty()) {
            sections.push_back(key);
            indents.push_back(indent);
            pending_list_key = join_path(sections, "");
            if (!pending_list_key.empty() && pending_list_key.back() == '.') {
                pending_list_key.pop_back();
            }
            continue;
        }

        const std::string full_key = join_path(sections, key);
        pending_list_key = full_key;
        if (value.front() == '[') {
            parsed.arrays[full_key] = parse_number_array(value);
            continue;
        }

        double number = 0.0;
        const std::string clean_value = unquote(value);
        if (try_parse_double(clean_value, number)) {
            parsed.numbers[full_key] = number;
        } else {
            parsed.strings[full_key] = clean_value;
        }
    }
    return parsed;
}

template <typename T>
void copy_common_profile_values(T& profile, const ParsedYaml& yaml) {
    profile.numeric_values = yaml.numbers;
    profile.numeric_arrays = yaml.arrays;
    profile.string_values = yaml.strings;
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
    if (value == "color_reversal") {
        return StockType::ColorReversal;
    }
    if (value == "monochrome" || value == "black_and_white") {
        return StockType::Monochrome;
    }
    return StockType::ColorNegative;
}

FilmStockProfile load_film_stock_profile(const std::filesystem::path& path) {
    const auto yaml = parse_profile_yaml(path);
    FilmStockProfile profile;
    profile.path = path;
    copy_common_profile_values(profile, yaml);
    profile.stock_id = yaml.strings.contains("stock_id") ? yaml.strings.at("stock_id") : path.stem().string();
    profile.stock_name = yaml.strings.contains("stock_name") ? yaml.strings.at("stock_name") : profile.stock_id;
    profile.stock_type = parse_stock_type(yaml.strings.contains("stock_type") ? yaml.strings.at("stock_type") : "color_negative");
    return profile;
}

PrintStockProfile load_print_stock_profile(const std::filesystem::path& path) {
    const auto yaml = parse_profile_yaml(path);
    PrintStockProfile profile;
    profile.path = path;
    copy_common_profile_values(profile, yaml);
    profile.print_stock_id = yaml.strings.contains("print_stock_id") ? yaml.strings.at("print_stock_id") : path.stem().string();
    profile.print_stock_name = yaml.strings.contains("print_stock_name") ? yaml.strings.at("print_stock_name") : profile.print_stock_id;
    return profile;
}

std::vector<FilmStockProfile> list_film_stock_profiles(const std::filesystem::path& directory) {
    std::vector<FilmStockProfile> profiles;
    if (!std::filesystem::is_directory(directory)) {
        return profiles;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".yaml") {
            profiles.push_back(load_film_stock_profile(entry.path()));
        }
    }
    std::ranges::sort(profiles, {}, &FilmStockProfile::stock_name);
    return profiles;
}

std::vector<PrintStockProfile> list_print_stock_profiles(const std::filesystem::path& directory) {
    std::vector<PrintStockProfile> profiles;
    if (!std::filesystem::is_directory(directory)) {
        return profiles;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".yaml") {
            profiles.push_back(load_print_stock_profile(entry.path()));
        }
    }
    std::ranges::sort(profiles, {}, &PrintStockProfile::print_stock_name);
    return profiles;
}

}  // namespace dfee
