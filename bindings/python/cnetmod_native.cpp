#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cnetmod/c_api.h>

#include <optional>
#include <string>
#include <string_view>

namespace {

struct sync_request_state
{
    cnetmod_runtime* runtime{};
    cnetmod_http_response* response{};
    int error_code{};
    std::string error_message;
    bool completed{};
};

void complete(void* raw, cnetmod_http_response* response, int error_code,
    const char* error_message)
{
    auto& state = *static_cast<sync_request_state*>(raw);
    state.response = response;
    state.error_code = error_code;
    if (error_message)
        state.error_message = error_message;
    state.completed = true;
    // A synchronous Python call drives exactly one request. Waking/stopping
    // the caller's run_one() prevents a completed callback from waiting for a
    // second IOCP/epoll event.
    cnetmod_runtime_stop(state.runtime);
}

auto parse_method(std::string_view method) -> cnetmod_http_method
{
    if (method == "GET")
        return CNETMOD_HTTP_GET;
    if (method == "POST")
        return CNETMOD_HTTP_POST;
    if (method == "PUT")
        return CNETMOD_HTTP_PUT;
    if (method == "DELETE")
        return CNETMOD_HTTP_DELETE;
    if (method == "PATCH")
        return CNETMOD_HTTP_PATCH;
    if (method == "HEAD")
        return CNETMOD_HTTP_HEAD;
    if (method == "OPTIONS")
        return CNETMOD_HTTP_OPTIONS;
    return static_cast<cnetmod_http_method>(-1);
}

auto parse_preference(std::string_view value)
    -> std::optional<cnetmod_http_version_preference>
{
    if (value == "auto")
        return CNETMOD_HTTP_2_PREFERRED;
    if (value == "http1")
        return CNETMOD_HTTP_1_ONLY;
    if (value == "http2")
        return CNETMOD_HTTP_2_ONLY;
    if (value == "http3")
        return CNETMOD_HTTP_3_ONLY;
    if (value == "http1_preferred")
        return CNETMOD_HTTP_1_PREFERRED;
    if (value == "http3_preferred")
        return CNETMOD_HTTP_3_PREFERRED;
    return std::nullopt;
}

auto request(PyObject*, PyObject* args, PyObject* keywords) -> PyObject*
{
    const char* method{};
    const char* url{};
    Py_buffer body{};
    int timeout_ms{30000};
    const char* version{"auto"};
    static constexpr const char* names[]{"method", "url", "body",
        "timeout_ms", "version", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, keywords, "ss|y*is",
            const_cast<char**>(names), &method, &url, &body, &timeout_ms,
            &version))
        return nullptr;

    const auto native_method = parse_method(method);
    if (static_cast<int>(native_method) < 0)
    {
        PyBuffer_Release(&body);
        PyErr_SetString(PyExc_ValueError, "unsupported HTTP method");
        return nullptr;
    }
    if (timeout_ms <= 0)
    {
        PyBuffer_Release(&body);
        PyErr_SetString(PyExc_ValueError, "timeout_ms must be positive");
        return nullptr;
    }
    const auto native_preference = parse_preference(version);
    if (!native_preference)
    {
        PyBuffer_Release(&body);
        PyErr_SetString(PyExc_ValueError, "unsupported HTTP version preference");
        return nullptr;
    }

    cnetmod_runtime* runtime = cnetmod_runtime_create();
    if (!runtime)
    {
        PyBuffer_Release(&body);
        return PyErr_NoMemory();
    }
    cnetmod_http_client_options options{};
    cnetmod_http_client_options_default(&options);
    options.request_timeout_ms = static_cast<uint32_t>(timeout_ms);
    options.version_preference = *native_preference;
    cnetmod_http_client* client = cnetmod_http_client_create(runtime, &options);
    if (!client)
    {
        cnetmod_runtime_destroy(runtime);
        PyBuffer_Release(&body);
        return PyErr_NoMemory();
    }

    sync_request_state state{.runtime = runtime};
    auto* pending = cnetmod_http_request_start(client, native_method, url,
        static_cast<const std::uint8_t*>(body.buf), body.len, complete, &state);
    PyBuffer_Release(&body);
    if (!pending)
    {
        cnetmod_http_client_destroy(client);
        cnetmod_runtime_destroy(runtime);
        PyErr_SetString(PyExc_ValueError, "invalid request arguments or URL");
        return nullptr;
    }

    // The native callback uses only C++ memory and is safe while Python's GIL
    // is released. This lets other Python threads progress during I/O.
    Py_BEGIN_ALLOW_THREADS while (!state.completed)
    {
        cnetmod_runtime_run_one(runtime);
        if (!state.completed)
            cnetmod_runtime_restart(runtime);
    }
    Py_END_ALLOW_THREADS

        cnetmod_http_request_destroy(pending);
    cnetmod_http_client_destroy(client);
    cnetmod_runtime_destroy(runtime);

    if (!state.response)
    {
        PyErr_SetString(PyExc_ConnectionError,
            state.error_message.empty() ? "cnetmod request failed"
                                        : state.error_message.c_str());
        return nullptr;
    }
    size_t size{};
    const auto* data = cnetmod_http_response_body(state.response, &size);
    const int status = cnetmod_http_response_status(state.response);
    PyObject* result = Py_BuildValue("(iN)", status,
        PyBytes_FromStringAndSize(reinterpret_cast<const char*>(data),
            static_cast<Py_ssize_t>(size)));
    cnetmod_http_response_free(state.response);
    return result;
}

auto version(PyObject*, PyObject*) -> PyObject*
{
    return PyUnicode_FromString("2.0.0");
}

PyMethodDef methods[]{
    {"request", reinterpret_cast<PyCFunction>(request), METH_VARARGS | METH_KEYWORDS,
        "request(method, url, body=b'', timeout_ms=30000, version='auto') -> (status, body)"},
    {"version", version, METH_NOARGS, "Return the cnetmod ABI version."},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef module{
    PyModuleDef_HEAD_INIT,
    "cnetmod_native",
    "Native cnetmod HTTP client binding.",
    -1,
    methods,
};

} // namespace

PyMODINIT_FUNC PyInit_cnetmod_native()
{
    return PyModule_Create(&module);
}
