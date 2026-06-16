#include "dfee/session.hpp"
#include "dfee/version.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void print_usage() {
    std::cout << "DFEE native CLI " << dfee::kEngineVersion << "\n"
              << "Usage:\n"
              << "  dfee_cli [--project-root PATH] --list-profiles\n"
              << "  dfee_cli [--project-root PATH] --cuda-status\n"
              << "  dfee_cli [--project-root PATH] --select RAW_FILENAME\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path project_root = DFEE_REPO_ROOT;
    std::string command;
    std::string select_filename;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project-root" && i + 1 < argc) {
            project_root = argv[++i];
        } else if (arg == "--list-profiles" || arg == "--cuda-status") {
            command = arg;
        } else if (arg == "--select" && i + 1 < argc) {
            command = arg;
            select_filename = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage();
            return 2;
        }
    }

    if (command.empty()) {
        print_usage();
        return 2;
    }

    try {
        dfee::EngineSession session(project_root);
        if (command == "--list-profiles") {
            const auto profiles = session.list_profiles();
            std::cout << "Project root: " << session.project_root().string() << "\n";
            std::cout << "Film stocks: " << profiles.stocks.size() << "\n";
            for (const auto& stock : profiles.stocks) {
                std::cout << "  " << stock.stock_id << " | " << stock.stock_name << " | " << stock.stock_type << "\n";
            }
            std::cout << "Print stocks: " << profiles.print_stocks.size() << "\n";
            for (const auto& print : profiles.print_stocks) {
                std::cout << "  " << print.print_stock_id << " | " << print.print_stock_name << "\n";
            }
            return 0;
        }

        if (command == "--cuda-status") {
            const auto status = session.cuda_status();
            std::cout << "mode=" << status.mode << "\n";
            std::cout << "compiled=" << (status.compiled ? "true" : "false") << "\n";
            std::cout << "available=" << (status.available ? "true" : "false") << "\n";
            std::cout << "active=" << (status.active ? "true" : "false") << "\n";
            std::cout << "device_count=" << status.device_count << "\n";
            if (!status.device_name.empty()) {
                std::cout << "device_name=" << status.device_name << "\n";
            }
            if (!status.fallback_reason.empty()) {
                std::cout << "fallback_reason=" << status.fallback_reason << "\n";
            }
            return 0;
        }

        if (command == "--select") {
            const auto result = session.select_file({.filename = select_filename});
            std::cout << "ok=" << (result.ok ? "true" : "false") << "\n";
            std::cout << "status=" << result.status << "\n";
            std::cout << "filename=" << result.filename << "\n";
            std::cout << "message=" << result.message << "\n";
            return result.ok ? 0 : 1;
        }
    } catch (const std::exception& ex) {
        std::cerr << "dfee_cli failed: " << ex.what() << "\n";
        return 1;
    }

    return 2;
}
