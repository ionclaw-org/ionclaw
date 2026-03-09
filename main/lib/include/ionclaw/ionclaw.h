#ifndef IONCLAW_H
#define IONCLAW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // initializes a new project from embedded template at the given path.
    // returns a JSON string: {"success":true/false,"error":"..."}
    // caller must free the returned string with ionclaw_free().
    const char *ionclaw_project_init(const char *path);

    // starts the server.
    // returns a JSON string: {"host":"...","port":N,"success":true/false,"error":"..."}
    // caller must free the returned string with ionclaw_free().
    const char *ionclaw_server_start(const char *project_path, const char *host, int port, const char *root_path, const char *web_path);

    // stops the server.
    // returns a JSON string: {"success":true/false,"error":"..."}
    // caller must free the returned string with ionclaw_free().
    const char *ionclaw_server_stop(void);

    // async platform handler callback type.
    // receives a request_id, function_name and params_json.
    // the host processes the request and calls ionclaw_platform_respond() with the result.
    typedef void (*ionclaw_platform_callback_t)(int64_t request_id, const char *function_name, const char *params_json);

    // registers an async platform handler from the host application (via FFI).
    // the callback will be invoked when the invoke_platform tool is used.
    // the C++ side blocks until ionclaw_platform_respond() is called with the matching request_id.
    // timeout_seconds controls how long the C++ side waits (0 = default 30s).
    void ionclaw_set_platform_handler(ionclaw_platform_callback_t callback, int timeout_seconds);

    // delivers the response for an async platform handler request.
    // must be called exactly once per callback invocation.
    void ionclaw_platform_respond(int64_t request_id, const char *result);

    // frees a string returned by any ionclaw_* function.
    void ionclaw_free(const char *ptr);

#ifdef __cplusplus
}
#endif

#endif
