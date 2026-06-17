from __future__ import annotations

from dataclasses import dataclass
from importlib import import_module
from pathlib import Path
from typing import Any


def _load_native_module():
    return import_module("dfee_native")


@dataclass(frozen=True)
class NativeCudaStatus:
    mode: str
    compiled: bool
    available: bool
    active: bool
    device_count: int
    device_name: str
    fallback_reason: str


@dataclass(frozen=True)
class NativeEngineInfo:
    engine_version: str
    libraw_enabled: bool
    libraw_version: str
    cuda_status: NativeCudaStatus
    timings: list["NativeStageTiming"]
    metadata_json: str


@dataclass(frozen=True)
class NativeStageTiming:
    stage: str
    milliseconds: float


@dataclass(frozen=True)
class NativeStockSummary:
    stock_id: str
    stock_name: str
    stock_type: str
    path: Path


@dataclass(frozen=True)
class NativePrintStockSummary:
    print_stock_id: str
    print_stock_name: str
    path: Path


@dataclass(frozen=True)
class NativeProfiles:
    stocks: list[NativeStockSummary]
    print_stocks: list[NativePrintStockSummary]
    engine: NativeEngineInfo


@dataclass(frozen=True)
class NativeSelectRequest:
    filename: str


@dataclass(frozen=True)
class NativeSelectResult:
    ok: bool
    filename: str
    status: str
    message: str
    engine: NativeEngineInfo


@dataclass(frozen=True)
class NativeRawMetadata:
    camera_make: str
    camera_model: str
    lens_model: str
    iso: int
    shutter_speed: float
    shutter_speed_str: str
    aperture: float
    focal_length: float
    white_balance_multipliers: list[float]
    black_level: int
    white_level: int
    image_height: int
    image_width: int
    raw_height: int
    raw_width: int
    metadata_json: str


@dataclass(frozen=True)
class NativeRawDecodeSummary:
    image_width: int
    image_height: int
    channels: int
    min_value: float
    max_value: float
    clipping_ratio_r: float
    clipping_ratio_g: float
    clipping_ratio_b: float
    raw_clipping_ratio: float
    summary_json: str


@dataclass(frozen=True)
class NativeSessionCacheState:
    selected_filename: str
    draft_decode_cached: bool
    draft_width: int
    draft_height: int
    preview_cached: bool
    preview_width: int
    preview_height: int
    raw_preview_jpeg_cached: bool
    raw_preview_jpeg_bytes: int
    full_decode_cached: bool
    full_width: int
    full_height: int


@dataclass(frozen=True)
class NativeRawPreview:
    filename: str
    status: str
    content_type: str
    jpeg_bytes: bytes
    engine: NativeEngineInfo


@dataclass(frozen=True)
class NativePreviewRenderRequest:
    filename: str
    stock: str
    exposure: float = 0.0
    highlights: float = 0.0
    shadows: float = 0.0
    blacks: float = 0.0
    whites: float = 0.0
    midtones: float = 0.0
    contrast: float = 0.0
    temp: float = 0.0
    tint: float = 0.0
    saturation: float = 0.0
    vibrance: float = 0.0
    curves: str = "[[0,0],[1,1]]"
    hsl_red_h: float = 0.0
    hsl_red_s: float = 0.0
    hsl_red_l: float = 0.0
    hsl_orange_h: float = 0.0
    hsl_orange_s: float = 0.0
    hsl_orange_l: float = 0.0
    hsl_yellow_h: float = 0.0
    hsl_yellow_s: float = 0.0
    hsl_yellow_l: float = 0.0
    hsl_green_h: float = 0.0
    hsl_green_s: float = 0.0
    hsl_green_l: float = 0.0
    hsl_aqua_h: float = 0.0
    hsl_aqua_s: float = 0.0
    hsl_aqua_l: float = 0.0
    hsl_blue_h: float = 0.0
    hsl_blue_s: float = 0.0
    hsl_blue_l: float = 0.0
    hsl_purple_h: float = 0.0
    hsl_purple_s: float = 0.0
    hsl_purple_l: float = 0.0
    hsl_magenta_h: float = 0.0
    hsl_magenta_s: float = 0.0
    hsl_magenta_l: float = 0.0
    clarity: float = 0.0
    texture: float = 0.0
    dehaze: float = 0.0
    sharpness: float = 0.0
    sharpness_mask: float = 0.5
    bloom: float = 0.0
    adaptation: float = 1.0
    grain: str = "Auto"
    grain_strength: float = -1.0
    grain_size: float = -1.0
    grain_roughness: float = -1.0
    halation: str = "Auto"
    film_color: float = 100.0
    print_stock: str = "none"
    print_strength: float = 1.0
    print_c: float = 0.0
    print_m: float = 0.0
    print_y: float = 0.0
    print_contrast: float = 0.0
    print_black_point: float = 0.0


@dataclass(frozen=True)
class NativeRenderedPreview:
    filename: str
    status: str
    content_type: str
    jpeg_bytes: bytes
    engine: NativeEngineInfo


@dataclass(frozen=True)
class NativeExportRequest(NativePreviewRenderRequest):
    export_format: str = "tiff"


@dataclass(frozen=True)
class NativeExportResult:
    filename: str
    status: str
    output_path: Path
    report_path: Path | None
    export_format: str
    format_label: str
    engine: NativeEngineInfo


@dataclass(frozen=True)
class NativeErrorInfo:
    code: str
    user_message: str
    detail: str


class NativeBridgeError(RuntimeError):
    def __init__(self, code: str, user_message: str, detail: str):
        super().__init__(user_message or detail or code)
        self.code = code
        self.user_message = user_message
        self.detail = detail


class NativeOperationError(NativeBridgeError):
    def __init__(self, code: str, user_message: str, detail: str, *, filename: str = "", status: str = ""):
        super().__init__(code, user_message, detail)
        self.filename = filename
        self.status = status


def _parse_cuda_status(payload: dict[str, Any]) -> NativeCudaStatus:
    return NativeCudaStatus(
        mode=str(payload.get("mode", "cpu")),
        compiled=bool(payload.get("compiled", False)),
        available=bool(payload.get("available", False)),
        active=bool(payload.get("active", False)),
        device_count=int(payload.get("device_count", 0)),
        device_name=str(payload.get("device_name", "")),
        fallback_reason=str(payload.get("fallback_reason", "")),
    )


def _parse_engine_info(payload: dict[str, Any]) -> NativeEngineInfo:
    timings = [
        NativeStageTiming(
            stage=str(item.get("stage", "")),
            milliseconds=float(item.get("milliseconds", 0.0)),
        )
        for item in payload.get("timings", [])
    ]
    return NativeEngineInfo(
        engine_version=str(payload.get("engine_version", "")),
        libraw_enabled=bool(payload.get("libraw_enabled", False)),
        libraw_version=str(payload.get("libraw_version", "")),
        cuda_status=_parse_cuda_status(dict(payload.get("cuda_status", {}))),
        timings=timings,
        metadata_json=str(payload.get("metadata_json", "")),
    )


def _parse_raw_metadata(payload: dict[str, Any]) -> NativeRawMetadata:
    return NativeRawMetadata(
        camera_make=str(payload.get("camera_make", "")),
        camera_model=str(payload.get("camera_model", "")),
        lens_model=str(payload.get("lens_model", "")),
        iso=int(payload.get("iso", 100)),
        shutter_speed=float(payload.get("shutter_speed", 1.0 / 125.0)),
        shutter_speed_str=str(payload.get("shutter_speed_str", "")),
        aperture=float(payload.get("aperture", 4.0)),
        focal_length=float(payload.get("focal_length", 0.0)),
        white_balance_multipliers=[float(v) for v in payload.get("white_balance_multipliers", [1.0, 1.0, 1.0, 1.0])],
        black_level=int(payload.get("black_level", 0)),
        white_level=int(payload.get("white_level", 0)),
        image_height=int(payload.get("image_height", 0)),
        image_width=int(payload.get("image_width", 0)),
        raw_height=int(payload.get("raw_height", 0)),
        raw_width=int(payload.get("raw_width", 0)),
        metadata_json=str(payload.get("metadata_json", "")),
    )


def _parse_raw_decode_summary(payload: dict[str, Any]) -> NativeRawDecodeSummary:
    return NativeRawDecodeSummary(
        image_width=int(payload.get("image_width", 0)),
        image_height=int(payload.get("image_height", 0)),
        channels=int(payload.get("channels", 3)),
        min_value=float(payload.get("min_value", 0.0)),
        max_value=float(payload.get("max_value", 0.0)),
        clipping_ratio_r=float(payload.get("clipping_ratio_r", 0.0)),
        clipping_ratio_g=float(payload.get("clipping_ratio_g", 0.0)),
        clipping_ratio_b=float(payload.get("clipping_ratio_b", 0.0)),
        raw_clipping_ratio=float(payload.get("raw_clipping_ratio", 0.0)),
        summary_json=str(payload.get("summary_json", "")),
    )


def _parse_session_cache_state(payload: dict[str, Any]) -> NativeSessionCacheState:
    return NativeSessionCacheState(
        selected_filename=str(payload.get("selected_filename", "")),
        draft_decode_cached=bool(payload.get("draft_decode_cached", False)),
        draft_width=int(payload.get("draft_width", 0)),
        draft_height=int(payload.get("draft_height", 0)),
        preview_cached=bool(payload.get("preview_cached", False)),
        preview_width=int(payload.get("preview_width", 0)),
        preview_height=int(payload.get("preview_height", 0)),
        raw_preview_jpeg_cached=bool(payload.get("raw_preview_jpeg_cached", False)),
        raw_preview_jpeg_bytes=int(payload.get("raw_preview_jpeg_bytes", 0)),
        full_decode_cached=bool(payload.get("full_decode_cached", False)),
        full_width=int(payload.get("full_width", 0)),
        full_height=int(payload.get("full_height", 0)),
    )


def _parse_error_info(payload: dict[str, Any]) -> NativeErrorInfo:
    return NativeErrorInfo(
        code=str(payload.get("code", "")),
        user_message=str(payload.get("user_message", "")),
        detail=str(payload.get("detail", "")),
    )


class NativeEngineSession:
    def __init__(self, native_module: Any, project_root: str | Path):
        self._native_module = native_module
        self._project_root = Path(project_root)
        try:
            self._handle = native_module.create_session(str(self._project_root))
        except Exception as exc:
            self._raise_bridge_error(exc)

    @property
    def project_root(self) -> Path:
        return self._project_root

    def list_profiles(self) -> NativeProfiles:
        payload = dict(self._native_module.list_profiles(self._handle))
        stocks = [
            NativeStockSummary(
                stock_id=str(item["stock_id"]),
                stock_name=str(item["stock_name"]),
                stock_type=str(item["stock_type"]),
                path=Path(item["path"]),
            )
            for item in payload.get("stocks", [])
        ]
        print_stocks = [
            NativePrintStockSummary(
                print_stock_id=str(item["print_stock_id"]),
                print_stock_name=str(item["print_stock_name"]),
                path=Path(item["path"]),
            )
            for item in payload.get("print_stocks", [])
        ]
        return NativeProfiles(
            stocks=stocks,
            print_stocks=print_stocks,
            engine=_parse_engine_info(dict(payload.get("engine", {}))),
        )

    def select_file(self, request: NativeSelectRequest | str) -> NativeSelectResult:
        if isinstance(request, str):
            request = NativeSelectRequest(filename=request)
        try:
            payload = dict(self._native_module.select_file(self._handle, request.filename))
        except Exception as exc:
            self._raise_bridge_error(exc)

        result = NativeSelectResult(
            ok=bool(payload.get("ok", False)),
            filename=str(payload.get("filename", "")),
            status=str(payload.get("status", "")),
            message=str(payload.get("message", "")),
            engine=_parse_engine_info(dict(payload.get("engine", {}))),
        )
        if "error" in payload:
            error = _parse_error_info(dict(payload["error"]))
            raise NativeOperationError(
                error.code,
                error.user_message,
                error.detail,
                filename=result.filename,
                status=result.status,
            )
        return result

    def read_raw_metadata(self, filename: str) -> NativeRawMetadata:
        try:
            payload = dict(self._native_module.read_raw_metadata(self._handle, filename))
        except Exception as exc:
            self._raise_bridge_error(exc)

        if "error" in payload:
            error = _parse_error_info(dict(payload["error"]))
            raise NativeOperationError(
                error.code,
                error.user_message,
                error.detail,
                filename=str(payload.get("filename", "")),
                status=str(payload.get("status", "")),
            )
        return _parse_raw_metadata(dict(payload.get("metadata", {})))

    def decode_raw(self, filename: str, *, draft_mode: bool = True) -> tuple[NativeRawDecodeSummary, NativeRawMetadata]:
        try:
            payload = dict(self._native_module.decode_raw(self._handle, filename, draft_mode))
        except Exception as exc:
            self._raise_bridge_error(exc)

        if "error" in payload:
            error = _parse_error_info(dict(payload["error"]))
            raise NativeOperationError(
                error.code,
                error.user_message,
                error.detail,
                filename=str(payload.get("filename", "")),
                status=str(payload.get("status", "")),
            )

        summary = _parse_raw_decode_summary(dict(payload.get("summary", {})))
        metadata = _parse_raw_metadata(dict(payload.get("metadata", {})))
        return summary, metadata

    def cache_state(self) -> NativeSessionCacheState:
        try:
            payload = dict(self._native_module.cache_state(self._handle))
        except Exception as exc:
            self._raise_bridge_error(exc)

        return _parse_session_cache_state(dict(payload.get("cache", {})))

    def raw_preview(self, filename: str = "", *, max_edge: int = 1024) -> NativeRawPreview:
        try:
            payload = dict(self._native_module.raw_preview(self._handle, filename, max_edge))
        except Exception as exc:
            self._raise_bridge_error(exc)

        if "error" in payload:
            error = _parse_error_info(dict(payload["error"]))
            raise NativeOperationError(
                error.code,
                error.user_message,
                error.detail,
                filename=str(payload.get("filename", "")),
                status=str(payload.get("status", "")),
            )

        return NativeRawPreview(
            filename=str(payload.get("filename", "")),
            status=str(payload.get("status", "")),
            content_type=str(payload.get("content_type", "image/jpeg")),
            jpeg_bytes=bytes(payload.get("jpeg_bytes", b"")),
            engine=_parse_engine_info(dict(payload.get("engine", {}))),
        )

    def render_preview(self, request: NativePreviewRenderRequest) -> NativeRenderedPreview:
        try:
            payload = dict(self._native_module.render_preview(self._handle, request.__dict__))
        except Exception as exc:
            self._raise_bridge_error(exc)

        if "error" in payload:
            error = _parse_error_info(dict(payload["error"]))
            raise NativeOperationError(
                error.code,
                error.user_message,
                error.detail,
                filename=str(payload.get("filename", "")),
                status=str(payload.get("status", "")),
            )

        return NativeRenderedPreview(
            filename=str(payload.get("filename", "")),
            status=str(payload.get("status", "")),
            content_type=str(payload.get("content_type", "image/jpeg")),
            jpeg_bytes=bytes(payload.get("jpeg_bytes", b"")),
            engine=_parse_engine_info(dict(payload.get("engine", {}))),
        )

    def export_image(self, request: NativeExportRequest) -> NativeExportResult:
        try:
            payload = dict(self._native_module.export_image(self._handle, request.__dict__))
        except Exception as exc:
            self._raise_bridge_error(exc)

        if "error" in payload:
            error = _parse_error_info(dict(payload["error"]))
            raise NativeOperationError(
                error.code,
                error.user_message,
                error.detail,
                filename=str(payload.get("filename", "")),
                status=str(payload.get("status", "")),
            )

        report_path_raw = str(payload.get("report_path", "") or "")
        return NativeExportResult(
            filename=str(payload.get("filename", "")),
            status=str(payload.get("status", "")),
            output_path=Path(str(payload.get("output_path", ""))),
            report_path=Path(report_path_raw) if report_path_raw else None,
            export_format=str(payload.get("export_format", "")),
            format_label=str(payload.get("format_label", "")),
            engine=_parse_engine_info(dict(payload.get("engine", {}))),
        )

    def _raise_bridge_error(self, exc: Exception) -> None:
        code = str(getattr(exc, "code", "") or "NATIVE_BRIDGE_ERROR")
        user_message = str(getattr(exc, "user_message", "") or str(exc))
        detail = str(getattr(exc, "detail", "") or str(exc))
        raise NativeBridgeError(code, user_message, detail) from exc


def engine_version() -> str:
    return str(_load_native_module().engine_version())


def cuda_status() -> NativeCudaStatus:
    return _parse_cuda_status(dict(_load_native_module().cuda_status()))


def create_session(project_root: str | Path) -> NativeEngineSession:
    return NativeEngineSession(_load_native_module(), project_root)
