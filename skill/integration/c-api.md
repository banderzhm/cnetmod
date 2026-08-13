# C ABI：Rust / Python 原生扩展边界

`cnetmod_c` 是不依赖 C++ module BMI 的静态 C ABI 库。它应被 Rust crate、
CPython extension 或其他原生扩展链接到最终宿主进程中；不要把完整的模块核心
包装成 Windows DLL 后再通过 `ctypes` 动态加载。

公共头文件：`include/cnetmod/c_api.h`。

## 运行模型

调用方创建 `cnetmod_runtime`，并在所属线程持续调用：

```c
cnetmod_runtime_poll(runtime);    /* 不阻塞，适合外部 event loop */
/* 或 */
cnetmod_runtime_run_one(runtime); /* 等待一个 I/O 或已投递任务 */
```

HTTP 完成回调一定运行在该 runtime 所在线程。除
`cnetmod_http_request_cancel()` 外，ABI 调用必须由同一线程发起；取消可由任意
线程调用，并会传播到 DNS/TCP/TLS/HTTP/2/HTTP/3 的可取消 I/O。

`request_cancel()` 不是仅设置一个逻辑标记：它会请求终止正在等待的底层 I/O，并且
仍然只触发一次 completion（失败时 `response == NULL`）。调用者可在 completion
之后销毁 request/client/runtime；正在执行的请求内部保留所需状态，不会悬空访问。

## 所有权

- `runtime/client/request` 都通过各自 `*_destroy()` 释放；已开始的请求内部持有
  所需状态，因此可以先释放外层 request handle。
- 回调成功时收到的 `cnetmod_http_response*` 由回调拥有，必须调用
  `cnetmod_http_response_free()`。
- 回调失败时 `response == NULL`；`error_message` 仅在该次回调期间有效，若要
  保存必须复制。
- `body` 是字节序列，使用 `cnetmod_http_response_body()` 返回的长度处理，不能
  假设 NUL 结尾。

## C 风格最小请求

```c
static void completed(void* user, cnetmod_http_response* response,
                      int error_code, const char* error_message) {
    if (response) {
        size_t size = 0;
        const uint8_t* bytes = cnetmod_http_response_body(response, &size);
        /* consume bytes[0..size) */
        cnetmod_http_response_free(response);
        return;
    }
    /* copy error_message here if it is needed after this callback */
}

cnetmod_runtime* runtime = cnetmod_runtime_create();
cnetmod_http_client_options options;
cnetmod_http_client_options_default(&options);
cnetmod_http_client* client = cnetmod_http_client_create(runtime, &options);
cnetmod_http_request* request = cnetmod_http_request_start(
    client, CNETMOD_HTTP_GET, "https://example.com/", NULL, 0,
    completed, NULL);
```

驱动 runtime 直到回调完成后，依次销毁 request、client、runtime。
