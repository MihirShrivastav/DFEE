#define PY_SSIZE_T_CLEAN
#if defined(_MSC_VER) && defined(_DEBUG)
#pragma push_macro("_DEBUG")
#undef _DEBUG
#include <Python.h>
#pragma pop_macro("_DEBUG")
#else
#include <Python.h>
#endif

#include "dfee/bridge_types.hpp"
#include "dfee/session.hpp"
#include "dfee/version.hpp"

namespace {

constexpr const char* kCapsuleName = "dfee.EngineSession";
PyObject* kNativeErrorType = nullptr;

dfee::EngineSession* session_from_capsule(PyObject* capsule) {
    return static_cast<dfee::EngineSession*>(PyCapsule_GetPointer(capsule, kCapsuleName));
}

void session_capsule_destructor(PyObject* capsule) {
    delete session_from_capsule(capsule);
}

PyObject* native_error_to_dict(const dfee::NativeError& error) {
    PyObject* dict = PyDict_New();
    PyDict_SetItemString(dict, "code", PyUnicode_FromString(error.code.c_str()));
    PyDict_SetItemString(dict, "user_message", PyUnicode_FromString(error.user_message.c_str()));
    PyDict_SetItemString(dict, "detail", PyUnicode_FromString(error.detail.c_str()));
    return dict;
}

void set_python_native_exception(const dfee::NativeError& error) {
    if (kNativeErrorType == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, error.detail.c_str());
        return;
    }
    PyObject* args = Py_BuildValue("(s)", error.user_message.c_str());
    if (args == nullptr) {
        return;
    }
    PyObject* exc = PyObject_CallObject(kNativeErrorType, args);
    Py_DECREF(args);
    if (exc == nullptr) {
        return;
    }
    PyObject* code = PyUnicode_FromString(error.code.c_str());
    PyObject* user_message = PyUnicode_FromString(error.user_message.c_str());
    PyObject* detail = PyUnicode_FromString(error.detail.c_str());
    if (code != nullptr) {
        PyObject_SetAttrString(exc, "code", code);
        Py_DECREF(code);
    }
    if (user_message != nullptr) {
        PyObject_SetAttrString(exc, "user_message", user_message);
        Py_DECREF(user_message);
    }
    if (detail != nullptr) {
        PyObject_SetAttrString(exc, "detail", detail);
        Py_DECREF(detail);
    }
    PyErr_SetObject(kNativeErrorType, exc);
    Py_DECREF(exc);
}

void set_python_exception_from_current(const std::exception& ex) {
    if (const auto* native = dynamic_cast<const dfee::NativeException*>(&ex); native != nullptr) {
        set_python_native_exception(native->error());
        return;
    }
    PyErr_SetString(PyExc_RuntimeError, ex.what());
}

PyObject* py_engine_version(PyObject*, PyObject*) {
    return PyUnicode_FromString(dfee::kEngineVersion);
}

PyObject* cuda_status_to_dict(const dfee::CudaStatus& status) {
    PyObject* dict = PyDict_New();
    PyObject* mode = PyUnicode_FromString(status.mode.c_str());
    PyObject* device_name = PyUnicode_FromString(status.device_name.c_str());
    PyObject* fallback = PyUnicode_FromString(status.fallback_reason.c_str());
    PyDict_SetItemString(dict, "mode", mode);
    PyDict_SetItemString(dict, "compiled", status.compiled ? Py_True : Py_False);
    PyDict_SetItemString(dict, "available", status.available ? Py_True : Py_False);
    PyDict_SetItemString(dict, "active", status.active ? Py_True : Py_False);
    PyObject* device_count = PyLong_FromLong(status.device_count);
    PyDict_SetItemString(dict, "device_count", device_count);
    PyDict_SetItemString(dict, "device_name", device_name);
    PyDict_SetItemString(dict, "fallback_reason", fallback);
    Py_DECREF(mode);
    Py_DECREF(device_count);
    Py_DECREF(device_name);
    Py_DECREF(fallback);
    return dict;
}

PyObject* timings_to_list(const std::vector<dfee::NativeStageTiming>& timings) {
    PyObject* list = PyList_New(static_cast<Py_ssize_t>(timings.size()));
    for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(timings.size()); ++i) {
        PyObject* item = PyDict_New();
        PyDict_SetItemString(item, "stage", PyUnicode_FromString(timings[static_cast<size_t>(i)].stage.c_str()));
        PyDict_SetItemString(item, "milliseconds", PyFloat_FromDouble(timings[static_cast<size_t>(i)].milliseconds));
        PyList_SET_ITEM(list, i, item);
    }
    return list;
}

PyObject* raw_metadata_to_dict(const dfee::NativeRawMetadata& metadata) {
    PyObject* dict = PyDict_New();
    PyObject* wb = PyList_New(static_cast<Py_ssize_t>(metadata.white_balance_multipliers.size()));
    for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(metadata.white_balance_multipliers.size()); ++i) {
        PyList_SET_ITEM(wb, i, PyFloat_FromDouble(metadata.white_balance_multipliers[static_cast<size_t>(i)]));
    }

    PyDict_SetItemString(dict, "camera_make", PyUnicode_FromString(metadata.camera_make.c_str()));
    PyDict_SetItemString(dict, "camera_model", PyUnicode_FromString(metadata.camera_model.c_str()));
    PyDict_SetItemString(dict, "lens_model", PyUnicode_FromString(metadata.lens_model.c_str()));
    PyDict_SetItemString(dict, "iso", PyLong_FromLong(metadata.iso));
    PyDict_SetItemString(dict, "shutter_speed", PyFloat_FromDouble(metadata.shutter_speed));
    PyDict_SetItemString(dict, "shutter_speed_str", PyUnicode_FromString(metadata.shutter_speed_str.c_str()));
    PyDict_SetItemString(dict, "aperture", PyFloat_FromDouble(metadata.aperture));
    PyDict_SetItemString(dict, "focal_length", PyFloat_FromDouble(metadata.focal_length));
    PyDict_SetItemString(dict, "white_balance_multipliers", wb);
    PyDict_SetItemString(dict, "black_level", PyLong_FromLong(metadata.black_level));
    PyDict_SetItemString(dict, "white_level", PyLong_FromLong(metadata.white_level));
    PyDict_SetItemString(dict, "image_height", PyLong_FromLong(metadata.image_height));
    PyDict_SetItemString(dict, "image_width", PyLong_FromLong(metadata.image_width));
    PyDict_SetItemString(dict, "raw_height", PyLong_FromLong(metadata.raw_height));
    PyDict_SetItemString(dict, "raw_width", PyLong_FromLong(metadata.raw_width));
    PyDict_SetItemString(dict, "metadata_json", PyUnicode_FromString(metadata.metadata_json.c_str()));
    Py_DECREF(wb);
    return dict;
}

PyObject* raw_decode_summary_to_dict(const dfee::NativeRawDecodeSummary& summary) {
    PyObject* dict = PyDict_New();
    PyDict_SetItemString(dict, "image_width", PyLong_FromLong(summary.image_width));
    PyDict_SetItemString(dict, "image_height", PyLong_FromLong(summary.image_height));
    PyDict_SetItemString(dict, "channels", PyLong_FromLong(summary.channels));
    PyDict_SetItemString(dict, "min_value", PyFloat_FromDouble(summary.min_value));
    PyDict_SetItemString(dict, "max_value", PyFloat_FromDouble(summary.max_value));
    PyDict_SetItemString(dict, "clipping_ratio_r", PyFloat_FromDouble(summary.clipping_ratio_r));
    PyDict_SetItemString(dict, "clipping_ratio_g", PyFloat_FromDouble(summary.clipping_ratio_g));
    PyDict_SetItemString(dict, "clipping_ratio_b", PyFloat_FromDouble(summary.clipping_ratio_b));
    PyDict_SetItemString(dict, "raw_clipping_ratio", PyFloat_FromDouble(summary.raw_clipping_ratio));
    PyDict_SetItemString(dict, "summary_json", PyUnicode_FromString(summary.summary_json.c_str()));
    return dict;
}

PyObject* cache_state_to_dict(const dfee::NativeSessionCacheState& cache) {
    PyObject* dict = PyDict_New();
    PyDict_SetItemString(dict, "selected_filename", PyUnicode_FromString(cache.selected_filename.c_str()));
    PyDict_SetItemString(dict, "draft_decode_cached", cache.draft_decode_cached ? Py_True : Py_False);
    PyDict_SetItemString(dict, "draft_width", PyLong_FromLong(cache.draft_width));
    PyDict_SetItemString(dict, "draft_height", PyLong_FromLong(cache.draft_height));
    PyDict_SetItemString(dict, "preview_cached", cache.preview_cached ? Py_True : Py_False);
    PyDict_SetItemString(dict, "preview_width", PyLong_FromLong(cache.preview_width));
    PyDict_SetItemString(dict, "preview_height", PyLong_FromLong(cache.preview_height));
    PyDict_SetItemString(dict, "raw_preview_jpeg_cached", cache.raw_preview_jpeg_cached ? Py_True : Py_False);
    PyDict_SetItemString(dict, "raw_preview_jpeg_bytes", PyLong_FromSize_t(cache.raw_preview_jpeg_bytes));
    PyDict_SetItemString(dict, "full_decode_cached", cache.full_decode_cached ? Py_True : Py_False);
    PyDict_SetItemString(dict, "full_width", PyLong_FromLong(cache.full_width));
    PyDict_SetItemString(dict, "full_height", PyLong_FromLong(cache.full_height));
    return dict;
}

PyObject* engine_metadata_to_dict(const dfee::NativeEngineMetadata& metadata) {
    PyObject* dict = PyDict_New();
    PyObject* status = cuda_status_to_dict(metadata.cuda_status);
    PyObject* timings = timings_to_list(metadata.timings);
    PyDict_SetItemString(dict, "engine_version", PyUnicode_FromString(metadata.engine_version.c_str()));
    PyDict_SetItemString(dict, "libraw_enabled", metadata.libraw_enabled ? Py_True : Py_False);
    PyDict_SetItemString(dict, "libraw_version", PyUnicode_FromString(metadata.libraw_version.c_str()));
    PyDict_SetItemString(dict, "cuda_status", status);
    PyDict_SetItemString(dict, "timings", timings);
    PyDict_SetItemString(dict, "metadata_json", PyUnicode_FromString(metadata.metadata_json.c_str()));
    Py_DECREF(status);
    Py_DECREF(timings);
    return dict;
}

PyObject* py_cuda_status(PyObject*, PyObject*) {
    return cuda_status_to_dict(dfee::query_cuda_status());
}

PyObject* py_create_session(PyObject*, PyObject* args) {
    const char* project_root = nullptr;
    if (!PyArg_ParseTuple(args, "s", &project_root)) {
        return nullptr;
    }
    try {
        auto* session = new dfee::EngineSession(project_root);
        return PyCapsule_New(session, kCapsuleName, session_capsule_destructor);
    } catch (const std::exception& ex) {
        set_python_exception_from_current(ex);
        return nullptr;
    }
}

PyObject* stock_to_dict(const dfee::NativeStockSummary& stock) {
    PyObject* dict = PyDict_New();
    PyDict_SetItemString(dict, "stock_id", PyUnicode_FromString(stock.stock_id.c_str()));
    PyDict_SetItemString(dict, "stock_name", PyUnicode_FromString(stock.stock_name.c_str()));
    PyDict_SetItemString(dict, "stock_type", PyUnicode_FromString(stock.stock_type.c_str()));
    PyDict_SetItemString(dict, "path", PyUnicode_FromString(stock.path.string().c_str()));
    return dict;
}

PyObject* print_stock_to_dict(const dfee::NativePrintStockSummary& stock) {
    PyObject* dict = PyDict_New();
    PyDict_SetItemString(dict, "print_stock_id", PyUnicode_FromString(stock.print_stock_id.c_str()));
    PyDict_SetItemString(dict, "print_stock_name", PyUnicode_FromString(stock.print_stock_name.c_str()));
    PyDict_SetItemString(dict, "path", PyUnicode_FromString(stock.path.string().c_str()));
    return dict;
}

PyObject* py_list_profiles(PyObject*, PyObject* args) {
    PyObject* capsule = nullptr;
    if (!PyArg_ParseTuple(args, "O", &capsule)) {
        return nullptr;
    }
    auto* session = session_from_capsule(capsule);
    if (session == nullptr) {
        return nullptr;
    }

    try {
        const auto profiles = session->list_profiles();
        PyObject* stocks = PyList_New(static_cast<Py_ssize_t>(profiles.stocks.size()));
        for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(profiles.stocks.size()); ++i) {
            PyList_SET_ITEM(stocks, i, stock_to_dict(profiles.stocks[static_cast<size_t>(i)]));
        }

        PyObject* print_stocks = PyList_New(static_cast<Py_ssize_t>(profiles.print_stocks.size()));
        for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(profiles.print_stocks.size()); ++i) {
            PyList_SET_ITEM(print_stocks, i, print_stock_to_dict(profiles.print_stocks[static_cast<size_t>(i)]));
        }

        PyObject* dict = PyDict_New();
        PyDict_SetItemString(dict, "stocks", stocks);
        PyDict_SetItemString(dict, "print_stocks", print_stocks);
        PyObject* engine = engine_metadata_to_dict(profiles.engine);
        PyDict_SetItemString(dict, "engine", engine);
        Py_DECREF(stocks);
        Py_DECREF(print_stocks);
        Py_DECREF(engine);
        return dict;
    } catch (const std::exception& ex) {
        set_python_exception_from_current(ex);
        return nullptr;
    }
}

PyObject* py_select_file(PyObject*, PyObject* args) {
    PyObject* capsule = nullptr;
    const char* filename = nullptr;
    if (!PyArg_ParseTuple(args, "Os", &capsule, &filename)) {
        return nullptr;
    }
    auto* session = session_from_capsule(capsule);
    if (session == nullptr) {
        return nullptr;
    }

    try {
        const auto result = session->select_file({.filename = filename});
        PyObject* dict = PyDict_New();
        PyDict_SetItemString(dict, "ok", result.ok ? Py_True : Py_False);
        PyDict_SetItemString(dict, "filename", PyUnicode_FromString(result.filename.c_str()));
        PyDict_SetItemString(dict, "status", PyUnicode_FromString(result.status.c_str()));
        PyDict_SetItemString(dict, "message", PyUnicode_FromString(result.message.c_str()));
        if (!result.error.empty()) {
            PyObject* error = native_error_to_dict(result.error);
            PyDict_SetItemString(dict, "error", error);
            Py_DECREF(error);
        }
        PyObject* engine = engine_metadata_to_dict(result.engine);
        PyDict_SetItemString(dict, "engine", engine);
        Py_DECREF(engine);
        return dict;
    } catch (const std::exception& ex) {
        set_python_exception_from_current(ex);
        return nullptr;
    }
}

PyObject* py_read_raw_metadata(PyObject*, PyObject* args) {
    PyObject* capsule = nullptr;
    const char* filename = nullptr;
    if (!PyArg_ParseTuple(args, "Os", &capsule, &filename)) {
        return nullptr;
    }
    auto* session = session_from_capsule(capsule);
    if (session == nullptr) {
        return nullptr;
    }

    try {
        const auto result = session->read_raw_metadata({.filename = filename});
        PyObject* dict = PyDict_New();
        PyDict_SetItemString(dict, "ok", result.ok ? Py_True : Py_False);
        PyDict_SetItemString(dict, "filename", PyUnicode_FromString(result.filename.c_str()));
        PyDict_SetItemString(dict, "status", PyUnicode_FromString(result.status.c_str()));
        PyObject* metadata = raw_metadata_to_dict(result.metadata);
        PyDict_SetItemString(dict, "metadata", metadata);
        Py_DECREF(metadata);
        if (!result.error.empty()) {
            PyObject* error = native_error_to_dict(result.error);
            PyDict_SetItemString(dict, "error", error);
            Py_DECREF(error);
        }
        PyObject* engine = engine_metadata_to_dict(result.engine);
        PyDict_SetItemString(dict, "engine", engine);
        Py_DECREF(engine);
        return dict;
    } catch (const std::exception& ex) {
        set_python_exception_from_current(ex);
        return nullptr;
    }
}

PyObject* py_decode_raw(PyObject*, PyObject* args, PyObject* kwargs) {
    PyObject* capsule = nullptr;
    const char* filename = nullptr;
    int draft_mode = 1;
    static const char* const keywords[] = {"session", "filename", "draft_mode", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Os|p", const_cast<char**>(keywords), &capsule, &filename, &draft_mode)) {
        return nullptr;
    }
    auto* session = session_from_capsule(capsule);
    if (session == nullptr) {
        return nullptr;
    }

    try {
        const auto result = session->decode_raw({
            .filename = filename,
            .draft_mode = draft_mode != 0,
        });
        PyObject* dict = PyDict_New();
        PyDict_SetItemString(dict, "ok", result.ok ? Py_True : Py_False);
        PyDict_SetItemString(dict, "filename", PyUnicode_FromString(result.filename.c_str()));
        PyDict_SetItemString(dict, "status", PyUnicode_FromString(result.status.c_str()));
        PyObject* summary = raw_decode_summary_to_dict(result.summary);
        PyObject* metadata = raw_metadata_to_dict(result.metadata);
        PyDict_SetItemString(dict, "summary", summary);
        PyDict_SetItemString(dict, "metadata", metadata);
        Py_DECREF(summary);
        Py_DECREF(metadata);
        if (!result.error.empty()) {
            PyObject* error = native_error_to_dict(result.error);
            PyDict_SetItemString(dict, "error", error);
            Py_DECREF(error);
        }
        PyObject* engine = engine_metadata_to_dict(result.engine);
        PyDict_SetItemString(dict, "engine", engine);
        Py_DECREF(engine);
        return dict;
    } catch (const std::exception& ex) {
        set_python_exception_from_current(ex);
        return nullptr;
    }
}

PyObject* py_cache_state(PyObject*, PyObject* args) {
    PyObject* capsule = nullptr;
    if (!PyArg_ParseTuple(args, "O", &capsule)) {
        return nullptr;
    }
    auto* session = session_from_capsule(capsule);
    if (session == nullptr) {
        return nullptr;
    }

    try {
        const auto result = session->cache_state();
        PyObject* dict = PyDict_New();
        PyDict_SetItemString(dict, "ok", result.ok ? Py_True : Py_False);
        PyObject* cache = cache_state_to_dict(result.cache);
        PyDict_SetItemString(dict, "cache", cache);
        Py_DECREF(cache);
        PyObject* engine = engine_metadata_to_dict(result.engine);
        PyDict_SetItemString(dict, "engine", engine);
        Py_DECREF(engine);
        return dict;
    } catch (const std::exception& ex) {
        set_python_exception_from_current(ex);
        return nullptr;
    }
}

PyObject* py_raw_preview(PyObject*, PyObject* args, PyObject* kwargs) {
    PyObject* capsule = nullptr;
    const char* filename = "";
    int max_edge = 1024;
    static const char* const keywords[] = {"session", "filename", "max_edge", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|si", const_cast<char**>(keywords), &capsule, &filename, &max_edge)) {
        return nullptr;
    }
    auto* session = session_from_capsule(capsule);
    if (session == nullptr) {
        return nullptr;
    }

    try {
        const auto result = session->raw_preview({
            .filename = filename != nullptr ? filename : "",
            .max_edge = max_edge,
        });
        PyObject* dict = PyDict_New();
        PyDict_SetItemString(dict, "ok", result.ok ? Py_True : Py_False);
        PyDict_SetItemString(dict, "filename", PyUnicode_FromString(result.filename.c_str()));
        PyDict_SetItemString(dict, "status", PyUnicode_FromString(result.status.c_str()));
        PyDict_SetItemString(dict, "content_type", PyUnicode_FromString(result.content_type.c_str()));
        PyObject* jpeg_bytes = PyBytes_FromStringAndSize(
            reinterpret_cast<const char*>(result.jpeg_bytes.data()),
            static_cast<Py_ssize_t>(result.jpeg_bytes.size()));
        PyDict_SetItemString(dict, "jpeg_bytes", jpeg_bytes);
        Py_DECREF(jpeg_bytes);
        if (!result.error.empty()) {
            PyObject* error = native_error_to_dict(result.error);
            PyDict_SetItemString(dict, "error", error);
            Py_DECREF(error);
        }
        PyObject* engine = engine_metadata_to_dict(result.engine);
        PyDict_SetItemString(dict, "engine", engine);
        Py_DECREF(engine);
        return dict;
    } catch (const std::exception& ex) {
        set_python_exception_from_current(ex);
        return nullptr;
    }
}

PyMethodDef kMethods[] = {
    {"engine_version", py_engine_version, METH_NOARGS, "Return the native engine version."},
    {"cuda_status", py_cuda_status, METH_NOARGS, "Return CUDA build/runtime status."},
    {"create_session", py_create_session, METH_VARARGS, "Create a native DFEE engine session."},
    {"list_profiles", py_list_profiles, METH_VARARGS, "List stock and print profiles visible to a native session."},
    {"select_file", py_select_file, METH_VARARGS, "Select a RAW file for a native session."},
    {"read_raw_metadata", py_read_raw_metadata, METH_VARARGS, "Read RAW metadata through the native session."},
    {"decode_raw", reinterpret_cast<PyCFunction>(py_decode_raw), METH_VARARGS | METH_KEYWORDS, "Decode a RAW file through the native session."},
    {"cache_state", py_cache_state, METH_VARARGS, "Inspect native session cache ownership state."},
    {"raw_preview", reinterpret_cast<PyCFunction>(py_raw_preview), METH_VARARGS | METH_KEYWORDS, "Return cached native RAW preview JPEG bytes."},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef kModule = {
    PyModuleDef_HEAD_INIT,
    "dfee_native",
    "Native C++ engine bridge for DFEE.",
    -1,
    kMethods,
};

}  // namespace

PyMODINIT_FUNC PyInit_dfee_native() {
    PyObject* module = PyModule_Create(&kModule);
    if (module == nullptr) {
        return nullptr;
    }

    kNativeErrorType = PyErr_NewException("dfee_native.NativeError", PyExc_RuntimeError, nullptr);
    if (kNativeErrorType == nullptr) {
        Py_DECREF(module);
        return nullptr;
    }

    if (PyModule_AddObject(module, "NativeError", kNativeErrorType) < 0) {
        Py_DECREF(kNativeErrorType);
        Py_DECREF(module);
        return nullptr;
    }

    return module;
}
