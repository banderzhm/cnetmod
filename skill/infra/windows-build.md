# Windows 构建与 bundled ICU

使用 Visual Studio 的 CMake generator 构建；Debug 与 Release 必须分别构建，不能混用产物。

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug --target cnetmod_build_all
cmake --build build --config Release --target cnetmod_build_all
```

## PostgreSQL 的 ICU 依赖

启用 `CNETMOD_ENABLE_POSTGRESQL=ON` 时，Windows 优先使用 `3rdparty/icu` 的 bundled ICU。ICU 的 Visual Studio 项目按当前 CMake 配置增量构建：

| 配置 | 导入库 | DLL |
|---|---|---|
| Debug | `icuucd.lib`、`icuind.lib` | `icuuc78d.dll`、`icuin78d.dll` |
| Release / RelWithDebInfo / MinSizeRel | `icuuc.lib`、`icuin.lib` | `icuuc78.dll`、`icuin78.dll` |

不要把 Release 的 ICU 库复制或映射给 Debug。这样会在链接测试或示例时出现 `LNK1104`，或引入运行库配置不匹配。若发生缺库，构建依赖目标 `cnetmod_icu`（或直接重建目标）即可由 ICU 的 `allinone.sln` 生成匹配配置的文件。
