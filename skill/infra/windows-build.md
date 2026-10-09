# Windows 构建与 bundled ICU

使用 Visual Studio 的 CMake generator 构建；Debug 与 Release 必须分别构建，不能混用产物。
默认配置是轻量 SDK 核心，不开启协议、测试、示例、benchmark、C API 或 Python Binding。

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target cnetmod_core -- /m:1
```

框架维护者需要完整验证时必须显式开启，并继续使用单 MSBuild 节点：

```powershell
cmake -S . -B build-full -G "Visual Studio 18 2026" -A x64 `
  -DCNETMOD_ENABLE_ALL_PROTOCOLS=ON `
  -DCNETMOD_BUILD_TESTS=ON `
  -DCNETMOD_BUILD_EXAMPLES=ON `
  -DCNETMOD_BUILD_BENCH=ON
cmake --build build-full --config Release --target cnetmod_build_all -- /m:1
```

OpenAI 测试源在非 MSVC 平台保持单目标；MSVC 配置会把它编译为七个测试分片，限制
单个 `cl.exe` 同时持有的 Glaze/OpenAI 模板语义图。不要为了恢复单一测试可执行文件而
移除分片，也不要用 `/Zm` 掩盖编译器堆耗尽。若新增重模板测试，应优先放入语义对应的
分片；单个测试翻译单元接近数千行时必须继续拆分。

## PostgreSQL 的 ICU 依赖

启用 `CNETMOD_ENABLE_POSTGRESQL=ON` 时，Windows 优先使用 `3rdparty/icu` 的 bundled ICU。ICU 的 Visual Studio 项目按当前 CMake 配置增量构建：

| 配置 | 导入库 | DLL |
|---|---|---|
| Debug | `icuucd.lib`、`icuind.lib` | `icuuc78d.dll`、`icuin78d.dll` |
| Release / RelWithDebInfo / MinSizeRel | `icuuc.lib`、`icuin.lib` | `icuuc78.dll`、`icuin78.dll` |

不要把 Release 的 ICU 库复制或映射给 Debug。这样会在链接测试或示例时出现 `LNK1104`，或引入运行库配置不匹配。若发生缺库，构建依赖目标 `cnetmod_icu`（或直接重建目标）即可由 ICU 的 `allinone.sln` 生成匹配配置的文件。
