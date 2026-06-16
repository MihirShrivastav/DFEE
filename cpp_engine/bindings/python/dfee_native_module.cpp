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

PyObject* engine_metadata_to_dict(const dfee::NativeEngineMetadata& metadata) {
    PyObject* dict = PyDict_New();
    PyObject* status = cuda_status_to_dict(metadata.cuda_status);
    PyObject* timings = timings_to_list(metadata.timings);
    PyDict_SetItemString(dict, "engine_version", PyUnicode_FromString(metadata.engine_version.c_str()));
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

PyMethodDef kMethods[] = {
    {"engine_version", py_engine_version, METH_NOARGS, "Return the native engine version."},
    {"cuda_status", py_cuda_status, METH_NOARGS, "Return CUDA build/runtime status."},
    {"create_session", py_create_session, METH_VARARGS, "Create a native DFEE engine session."},
    {"list_profiles", py_list_profiles, METH_VARARGS, "List stock and print profiles visible to a native session."},
    {"select_file", py_select_file, METH_VARARGS, "Select a RAW file for a native session."},
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
