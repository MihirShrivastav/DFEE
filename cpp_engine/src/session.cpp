#include "dfee/session.hpp"

#include "dfee/bridge_utils.hpp"
#include "dfee/native_error.hpp"
#include "dfee/raw_metadata.hpp"
#include "dfee/version.hpp"

#include <algorithm>
#include <filesystem>

namespace dfee {
namespace {

NativeEngineMetadata build_engine_metadata() {
    NativeEngineMetadata metadata;
    metadata.engine_version = kEngineVersion;
    metadata.cuda_status = query_cuda_status();
#if DFEE_HAS_LIBRAW
    metadata.libraw_enabled = true;
    metadata.libraw_version = "enabled";
#else
    metadata.libraw_enabled = false;
#endif
    return metadata;
}

void finalize_engine_metadata(NativeEngineMetadata& metadata) {
    metadata.metadata_json = serialize_native_engine_metadata_json(metadata);
}

Image resize_image_to_max_edge(const Image& source, const int max_edge) {
    if (source.empty() || max_edge <= 0) {
        return source;
    }

    const int current_max = std::max(source.width, source.height);
    if (current_max <= max_edge) {
        return source;
    }

    const float scale = static_cast<float>(max_edge) / static_cast<float>(current_max);
    const int target_width = std::max(1, static_cast<int>(source.width * scale));
    const int target_height = std::max(1, static_cast<int>(source.height * scale));
    Image resized(target_width, target_height, source.channels);

    for (int y = 0; y < target_height; ++y) {
        const int source_y = std::min(source.height - 1, static_cast<int>(static_cast<float>(y) / scale));
        for (int x = 0; x < target_width; ++x) {
            const int source_x = std::min(source.width - 1, static_cast<int>(static_cast<float>(x) / scale));
            for (int channel = 0; channel < source.channels; ++channel) {
                resized.at(x, y, channel) = source.at(source_x, source_y, channel);
            }
        }
    }

    return resized;
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
            if (selected_filename_ != request.filename) {
                clear_decode_caches();
            }
            selected_filename_ = request.filename;
        }
    }

    result.ok = true;
    result.status = "selected";
    result.message = "Native session selected the RAW file and reset any stale decode caches.";
    finalize_engine_metadata(result.engine);
    return result;
}

NativeRawMetadataResponse EngineSession::read_raw_metadata(const NativeRawMetadataRequest& request) const {
    NativeRawMetadataResponse response;
    response.filename = request.filename;
    response.engine = build_engine_metadata();

    {
        ScopedStageTimer total(response.engine, "read_raw_metadata_total");
        {
            ScopedStageTimer stage(response.engine, "read_raw_metadata_file");
            const auto file_response = read_raw_metadata_from_file({
                .filename = (raw_dir_ / request.filename).string(),
            });
            response.ok = file_response.ok;
            response.status = file_response.status;
            response.metadata = file_response.metadata;
            response.error = file_response.error;
        }
    }

    finalize_engine_metadata(response.engine);
    return response;
}

NativeRawDecodeResponse EngineSession::decode_raw(const NativeRawDecodeRequest& request) {
    NativeRawDecodeResponse response;
    response.filename = resolve_filename(request.filename);
    response.engine = build_engine_metadata();

    {
        ScopedStageTimer total(response.engine, request.draft_mode ? "decode_raw_draft_total" : "decode_raw_full_total");
        {
            ScopedStageTimer stage(response.engine, "decode_raw_resolve_session");
            if (response.filename.empty()) {
                response.status = "error";
                response.error = {
                    .code = "RAW_FILENAME_MISSING",
                    .user_message = "Select a RAW file before continuing.",
                    .detail = "decode_raw received an empty filename and no session file is currently selected.",
                };
                finalize_engine_metadata(response.engine);
                return response;
            }
        }
        {
            ScopedStageTimer stage(response.engine, "decode_raw_cache_lookup");
            const auto& cache = request.draft_mode ? draft_decode_cache_ : full_decode_cache_;
            if (cache.has_value() && cache->filename == response.filename) {
                response.ok = true;
                response.status = "cached";
                response.summary = cache->decoded.summary;
                response.metadata = cache->decoded.metadata;
                finalize_engine_metadata(response.engine);
                return response;
            }
        }
        {
            ScopedStageTimer stage(response.engine, "decode_raw_file");
            const auto file_response = decode_raw_image_from_file({
                .filename = (raw_dir_ / response.filename).string(),
                .draft_mode = request.draft_mode,
            });
            response.ok = file_response.ok;
            response.status = file_response.status;
            response.summary = file_response.decoded.summary;
            response.metadata = file_response.decoded.metadata;
            response.error = file_response.error;
            if (file_response.ok) {
                CachedDecode cache_entry{
                    .filename = response.filename,
                    .draft_mode = request.draft_mode,
                    .decoded = file_response.decoded,
                };
                if (request.draft_mode) {
                    draft_decode_cache_ = std::move(cache_entry);
                } else {
                    full_decode_cache_ = std::move(cache_entry);
                }
            }
        }
        {
            ScopedStageTimer stage(response.engine, "decode_raw_refresh_preview_cache");
            if (response.ok && request.draft_mode) {
                refresh_preview_cache_from_draft();
            }
        }
    }

    finalize_engine_metadata(response.engine);
    return response;
}

NativeSessionCacheStateResponse EngineSession::cache_state() const {
    NativeSessionCacheStateResponse response;
    response.ok = true;
    response.engine = build_engine_metadata();
    response.cache.selected_filename = selected_filename_;
    if (draft_decode_cache_.has_value()) {
        response.cache.draft_decode_cached = true;
        response.cache.draft_width = draft_decode_cache_->decoded.summary.image_width;
        response.cache.draft_height = draft_decode_cache_->decoded.summary.image_height;
    }
    if (preview_cache_.has_value()) {
        response.cache.preview_cached = true;
        response.cache.preview_width = preview_cache_->rgb_linear.width;
        response.cache.preview_height = preview_cache_->rgb_linear.height;
    }
    if (full_decode_cache_.has_value()) {
        response.cache.full_decode_cached = true;
        response.cache.full_width = full_decode_cache_->decoded.summary.image_width;
        response.cache.full_height = full_decode_cache_->decoded.summary.image_height;
    }
    finalize_engine_metadata(response.engine);
    return response;
}

CudaStatus EngineSession::cuda_status() const noexcept {
    return query_cuda_status();
}

std::string EngineSession::resolve_filename(const std::string& filename) const {
    if (!filename.empty()) {
        return filename;
    }
    return selected_filename_;
}

void EngineSession::clear_decode_caches() {
    draft_decode_cache_.reset();
    full_decode_cache_.reset();
    preview_cache_.reset();
}

void EngineSession::refresh_preview_cache_from_draft() {
    if (!draft_decode_cache_.has_value()) {
        preview_cache_.reset();
        return;
    }

    CachedPreview preview;
    preview.filename = draft_decode_cache_->filename;
    preview.rgb_linear = resize_image_to_max_edge(draft_decode_cache_->decoded.rgb_linear, 1024);
    preview.luminance = compute_luminance(preview.rgb_linear);
    preview_cache_ = std::move(preview);
}

}  // namespace dfee
