#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace dfee {

enum class StockType {
    ColorNegative,
    ColorReversal,
    Monochrome,
};

struct FilmStockProfile {
    std::filesystem::path path;
    std::string stock_id;
    std::string stock_name;
    StockType stock_type = StockType::ColorNegative;
    std::unordered_map<std::string, double> numeric_values;
    std::unordered_map<std::string, std::vector<double>> numeric_arrays;
    std::unordered_map<std::string, std::string> string_values;
};

struct PrintStockProfile {
    std::filesystem::path path;
    std::string print_stock_id;
    std::string print_stock_name;
    std::unordered_map<std::string, double> numeric_values;
    std::unordered_map<std::string, std::vector<double>> numeric_arrays;
    std::unordered_map<std::string, std::string> string_values;
};

[[nodiscard]] std::string to_string(StockType stock_type);
[[nodiscard]] StockType parse_stock_type(const std::string& value);

[[nodiscard]] FilmStockProfile load_film_stock_profile(const std::filesystem::path& path);
[[nodiscard]] PrintStockProfile load_print_stock_profile(const std::filesystem::path& path);

[[nodiscard]] std::vector<FilmStockProfile> list_film_stock_profiles(const std::filesystem::path& directory);
[[nodiscard]] std::vector<PrintStockProfile> list_print_stock_profiles(const std::filesystem::path& directory);

}  // namespace dfee
