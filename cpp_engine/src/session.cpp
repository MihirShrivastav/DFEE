#include "dfee/session.hpp"

#include "dfee/bridge_utils.hpp"
#include "dfee/native_error.hpp"
#include "dfee/version.hpp"

#include <filesystem>

namespace dfee {
namespace {

NativeEngineMetadata build_engine_metadata() {
    NativeEngineMetadata metadata;
    metadata.engine_version = kEngineVersion;
    metadata.cuda_status = query_cuda_status();
    return metadata;
}

void finalize_engine_metadata(NativeEngineMetadata& metadata) {
    metadata.metadata_json = serialize_native_engine_metadata_json(metadata);
}

}  // namespace

EngineSession::EngineSession(std::filesystem::path project_root)
    : project_root_(std::filesystem::absolute(std::move(project_root))),
      raw_dir_(project_root_ / "raw_files"),
      stocks_dir_(project_root_ / "profiles" / "stocks"),
      print_stocks_dir_(project_root_ / "profiles" / "print_stocks") {
    if (!std::filesystem::is_directory(project_root_)) {
        throw make_native_exception(
            "PROJECT_ROOT_NOT_FOUND",
            "The DFEE project root could not be found.",
            "Expected project root directory does not exist: " + project_root_.string());
    }
}

const std::filesystem::path& EngineSession::project_root() const noexcept {
    return project_root_;
}

NativeProfilesResponse EngineSession::list_profiles() const {
    NativeProfilesResponse response;
    response.engine = build_engine_metadata();

    {
        ScopedStageTimer total(response.engine, "list_profiles_total");
        {
            ScopedStageTimer stage(response.engine, "list_stock_profiles");
            for (const auto& stock : list_film_stock_profiles(stocks_dir_)) {
                response.stocks.push_back({
                    .stock_id = stock.stock_id,
                    .stock_name = stock.stock_name,
                    .stock_type = to_string(stock.stock_type),
                    .path = stock.path,
                });
            }
        }

        {
            ScopedStageTimer stage(response.engine, "list_print_stock_profiles");
            for (const auto& print_stock : list_print_stock_profiles(print_stocks_dir_)) {
                response.print_stocks.push_back({
                    .print_stock_id = print_stock.print_stock_id,
                    .print_stock_name = print_stock.print_stock_name,
                    .path = print_stock.path,
                });
            }
        }
    }
    finalize_engine_metadata(response.engine);
    return response;
}

NativeSelectResponse EngineSession::select_file(const NativeSelectRequest& request) {
    NativeSelectResponse result;
    result.filename = request.filename;
    result.engine = build_engine_metadata();

    {
        ScopedStageTimer total(result.engine, "select_file_total");
        const std::filesystem::path raw_path = raw_dir_ / request.filename;
        {
            ScopedStageTimer stage(result.engine, "select_validate_request");
            if (request.filename.empty()) {
                result.status = "error";
                result.message = "No RAW filename was provided.";
                result.error = {
                    .code = "RAW_FILENAME_MISSING",
                    .user_message = "Select a RAW file before continuing.",
                    .detail = "select_file received an empty filename.",
                };
                finalize_engine_metadata(result.engine);
                return result;
            }
        }
        {
            ScopedStageTimer stage(result.engine, "select_locate_raw_file");
            if (!std::filesystem::is_regular_file(raw_path)) {
                result.status = "not_found";
                result.message = "RAW file was not found under raw_files.";
                result.error = {
                    .code = "RAW_FILE_NOT_FOUND",
                    .user_message = "The requested RAW file was not found.",
                    .detail = "Expected RAW file at: " + raw_path.string(),
                };
                finalize_engine_metadata(result.engine);
                return result;
            }
        }
        {
            ScopedStageTimer stage(result.engine, "select_cache_session_state");
            selected_filename_ = request.filename;
        }
    }

    result.ok = true;
    result.status = "selected";
    result.message = "Native session selected the RAW file; LibRaw decode is scheduled for the next migration slice.";
    finalize_engine_metadata(result.engine);
    return result;
}

CudaStatus EngineSession::cuda_status() const noexcept {
    return query_cuda_status();
}

}  // namespace dfee
