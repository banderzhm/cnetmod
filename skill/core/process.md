# Process

> 提供跨平台、RAII 管理的子进程与标准输入输出管道，供 MCP、编译器工具和其他本地协议适配器复用。

**import**: `import cnetmod.core.process;`
**源码**: `src/core/process.cppm`, `src/core/process.cpp`

## 核心原则

- `child_process` 仅负责进程和阻塞式管道原语；协程调用方必须通过 `blocking_invoke` 与 `thread_pool` 接入。
- 平台实现封装在 `.cpp` 中，协议模块不得直接包含 Windows 或 POSIX 进程 API。
- 对象不可复制、可以移动；析构时关闭管道并终止仍在运行的子进程。
- 命令和参数使用 `std::filesystem::path`，Windows 直接使用原生宽字符路径。

## API 参考

```cpp
export struct process_options {
    std::filesystem::path executable;
    std::vector<std::filesystem::path> arguments;
    std::optional<std::filesystem::path> working_directory;
};

export class child_process {
public:
    static auto launch(process_options)
        -> std::expected<child_process, std::error_code>;
    auto write(std::string_view)
        -> std::expected<void, std::error_code>;
    auto read_line()
        -> std::expected<std::string, std::error_code>;
    void close_input() noexcept;
    void terminate() noexcept;
    auto running() const noexcept -> bool;
};
```

## 使用方式

在协程中不要直接调用阻塞式 `write` 或 `read_line`。使用 executor bridge：

```cpp
auto result = co_await cnetmod::blocking_invoke(pool, context,
    [&process]() -> std::expected<std::string, std::error_code> {
        auto written = process.write("request\n");
        if (!written)
            return std::unexpected(written.error());
        return process.read_line();
    });
```

## Do's & Don'ts

| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 在协议层复用 `child_process` | 在协议文件中直接调用 `CreateProcess` 或 `fork` |
| 通过 `blocking_invoke` 调用管道操作 | 在 I/O 协程中直接执行阻塞读取 |
| 使用 RAII 或显式 `terminate` 回收子进程 | 遗留后台进程和未关闭管道 |
