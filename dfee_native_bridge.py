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
        cuda_status=_parse_cuda_status(dict(payload.get("cuda_status", {}))),
        timings=timings,
        metadata_json=str(payload.get("metadata_json", "")),
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
