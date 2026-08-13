#ifndef CNETMOD_C_API_H
#define CNETMOD_C_API_H

/*
 * Stable, module-free ABI for languages which cannot import C++23 modules.
 * All text inputs are UTF-8 byte strings. Calls other than request_cancel()
 * must be made by the runtime-owning thread; drive that thread with
 * cnetmod_runtime_poll() or cnetmod_runtime_run_one().
 */

#include <stddef.h>
#include <stdint.h>

#if defined(CNETMOD_C_API_STATIC)
    #define CNETMOD_C_API
#elif defined(_WIN32)
    #if defined(CNETMOD_C_API_BUILD)
        #define CNETMOD_C_API __declspec(dllexport)
    #else
        #define CNETMOD_C_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define CNETMOD_C_API __attribute__((visibility("default")))
#else
    #define CNETMOD_C_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cnetmod_runtime cnetmod_runtime;
typedef struct cnetmod_http_client cnetmod_http_client;
typedef struct cnetmod_http_request cnetmod_http_request;
typedef struct cnetmod_http_response cnetmod_http_response;

typedef enum cnetmod_http_method {
    CNETMOD_HTTP_GET,
    CNETMOD_HTTP_POST,
    CNETMOD_HTTP_PUT,
    CNETMOD_HTTP_DELETE,
    CNETMOD_HTTP_PATCH,
    CNETMOD_HTTP_HEAD,
    CNETMOD_HTTP_OPTIONS
} cnetmod_http_method;

typedef enum cnetmod_http_version_preference {
    CNETMOD_HTTP_1_ONLY,
    CNETMOD_HTTP_2_ONLY,
    CNETMOD_HTTP_2_PREFERRED,
    CNETMOD_HTTP_1_PREFERRED,
    CNETMOD_HTTP_3_ONLY,
    CNETMOD_HTTP_3_PREFERRED
} cnetmod_http_version_preference;

typedef struct cnetmod_http_client_options {
    /* Set to sizeof(cnetmod_http_client_options). */
    uint32_t struct_size;
    uint32_t connect_timeout_ms;
    uint32_t request_timeout_ms;
    uint32_t max_redirects;
    uint8_t follow_redirects;
    uint8_t verify_peer;
    uint8_t keep_alive;
    uint8_t enable_cookies;
    cnetmod_http_version_preference version_preference;
    const char* user_agent;
} cnetmod_http_client_options;

/* response is owned by the callback and must be released with response_free. */
typedef void (*cnetmod_http_completion)(void* user_data,
    cnetmod_http_response* response, int error_code,
    const char* error_message);

CNETMOD_C_API cnetmod_runtime* cnetmod_runtime_create(void);
CNETMOD_C_API void cnetmod_runtime_destroy(cnetmod_runtime* runtime);
CNETMOD_C_API size_t cnetmod_runtime_poll(cnetmod_runtime* runtime);
CNETMOD_C_API size_t cnetmod_runtime_run_one(cnetmod_runtime* runtime);
CNETMOD_C_API void cnetmod_runtime_stop(cnetmod_runtime* runtime);
CNETMOD_C_API void cnetmod_runtime_restart(cnetmod_runtime* runtime);

CNETMOD_C_API void cnetmod_http_client_options_default(
    cnetmod_http_client_options* options);
CNETMOD_C_API cnetmod_http_client* cnetmod_http_client_create(
    cnetmod_runtime* runtime, const cnetmod_http_client_options* options);
CNETMOD_C_API void cnetmod_http_client_destroy(cnetmod_http_client* client);

/* Returns NULL for invalid input or allocation failure. Completion is always
 * delivered on the runtime-owning thread. request_cancel is thread-safe. */
CNETMOD_C_API cnetmod_http_request* cnetmod_http_request_start(
    cnetmod_http_client* client, cnetmod_http_method method, const char* url,
    const uint8_t* body, size_t body_size, cnetmod_http_completion completion,
    void* user_data);
CNETMOD_C_API void cnetmod_http_request_cancel(cnetmod_http_request* request);
CNETMOD_C_API void cnetmod_http_request_destroy(cnetmod_http_request* request);

CNETMOD_C_API int cnetmod_http_response_status(
    const cnetmod_http_response* response);
CNETMOD_C_API const uint8_t* cnetmod_http_response_body(
    const cnetmod_http_response* response, size_t* body_size);
CNETMOD_C_API void cnetmod_http_response_free(cnetmod_http_response* response);

#ifdef __cplusplus
}
#endif

#endif /* CNETMOD_C_API_H */
