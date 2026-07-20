# 第三方依赖集成

本文说明如何把 cnetmod 集成到一个已经使用了相同第三方库的宿主项目中。

## 问题

下游项目可能已经定义了这些 target：

- `pugixml::pugixml`
- `pugixml-static`
- `leveldb`
- `leveldb::leveldb`

如果 cnetmod 仍然无条件对同一份库执行 `add_subdirectory(3rdparty/...)`，CMake 会因为重复 target 报错。对于 header-only 依赖，如果宿主项目想使用自己的 checkout 路径，也需要显式覆盖。

## 推荐模型

宿主项目先拥有共享依赖，再添加 cnetmod：

1. 宿主项目创建或导入依赖 target。
2. 如有需要，通过 cache 变量设置 cnetmod 的 header-only 依赖路径。
3. 调用 `add_subdirectory(cnetmod)`。
4. 应用程序链接 `cnetmod_core`。

cnetmod 会优先复用已经存在的依赖 target：

- `pugixml::pugixml`、`pugixml-static` 或 `pugixml`
- `leveldb`、`leveldb::leveldb` 或 `LevelDB::LevelDB`

对于 header-only 依赖，宿主项目可以覆盖：

- `CNETMOD_STDEXEC_INCLUDE_DIR`
- `CNETMOD_JWT_CPP_INCLUDE_DIR`
- `CNETMOD_JSON_INCLUDE_DIR`
- `CNETMOD_JSON_MODULE`

## vcpkg

仓库已包含 `vcpkg.json` manifest。宿主项目使用 vcpkg toolchain 配置时，
cnetmod 会优先复用 toolchain 找到的依赖，再回退到已有的 `3rdparty` 目录：

```bash
cmake -B build-vcpkg \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-vcpkg --target cnetmod_build_all
```

如果同时安装了多个 Visual Studio，可以用仓库自带的 overlay triplet 强制
使用 Visual Studio 2026。下面是 Windows 上验证过的完整命令：

```bat
set VCPKG_ROOT=<path-to-vcpkg>
set VCPKG_VISUAL_STUDIO_PATH=<path-to-Visual-Studio-2026>

:: 可选：C 盘空间不足时，把 vcpkg 缓存放到其它盘
set X_VCPKG_REGISTRIES_CACHE=%USERPROFILE%\.cache\vcpkg\registries
set VCPKG_DOWNLOADS=%USERPROFILE%\.cache\vcpkg\downloads
set TEMP=%USERPROFILE%\.cache\build-tmp
set TMP=%USERPROFILE%\.cache\build-tmp

%VCPKG_ROOT%\vcpkg.exe install --triplet x64-windows-vs2026 ^
      --overlay-triplets=cmake\vcpkg-triplets

cmake -S . -B build-vcpkg-vs2026 -G"Visual Studio 18 2026" ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
      -DVCPKG_TARGET_TRIPLET=x64-windows-vs2026 ^
      -DVCPKG_OVERLAY_TRIPLETS=cmake/vcpkg-triplets

cmake --build build-vcpkg-vs2026 --config Release --target cnetmod_build_all
```

`X_VCPKG_REGISTRIES_CACHE` 和 `VCPKG_DOWNLOADS` 不是必须项；如果 C 盘空间不足，
建议像上面一样把 vcpkg registry cache、下载和构建缓存放到其它盘。

`pugixml` 在本仓库里保持为
正常 Git submodule；包管理器构建应优先使用 package target，只在没有系统包
时回退到该 submodule。

## Conan

仓库包含 Conan 2 `conanfile.py`。它会生成 `CMakeDeps` 和
`CMakeToolchain` 文件，并把 Conan 包名映射到 cnetmod 可识别的 target：

- `jwt-cpp` -> `jwt-cpp::jwt-cpp`
- `nlohmann_json` -> `nlohmann_json::nlohmann_json`
- `pugixml` -> `pugixml::pugixml`
- `leveldb` -> `leveldb::leveldb`

```bash
conan install . --output-folder=build-conan --build=missing \
      -s build_type=Release -s compiler.cppstd=23
cmake --preset conan-default
cmake --build --preset conan-release --target cnetmod_core
```

Visual Studio 2026 需要 Conan 2.30+ 和 CMake 4.2+，这样才能识别 MSVC
195 和 `Visual Studio 18 2026` 生成器：

```bat
:: 可选：C 盘空间不足时，把 Conan cache 和临时目录放到其它盘
set CONAN_HOME=%USERPROFILE%\.conan2-vs2026
set TEMP=%USERPROFILE%\.cache\build-tmp
set TMP=%USERPROFILE%\.cache\build-tmp

conan --version
conan install . --output-folder=build-conan-vs2026 --build=missing ^
      -s build_type=Release ^
      -s compiler=msvc -s compiler.version=195 ^
      -s compiler.runtime=dynamic -s compiler.runtime_type=Release ^
      -s compiler.cppstd=23 ^
      -c tools.cmake.cmaketoolchain:generator="Visual Studio 18 2026"

cmake --preset conan-default
cmake --build --preset conan-release --target cnetmod_core

:: 可选：验证 recipe 的导出、隔离构建和打包流程
conan create . --build=missing -pr:h vs2026 -pr:b vs2026
```

如果使用 profile，VS 2026 的核心设置是 `compiler=msvc`、`compiler.version=195`、
`compiler.cppstd=23`，并在 `[conf]` 中设置
`tools.cmake.cmaketoolchain:generator=Visual Studio 18 2026`。

`stdexec` 不假设每个 Conan remote 都有。默认使用 `3rdparty/stdexec`；
只有当你的 remote 提供上游 `p2300` recipe 时，才需要开启
`with_stdexec_package=True`。`mimalloc` 默认启用；如需关闭可传
`-o cnetmod/*:with_mimalloc=False`。

## 示例项目

仓库中包含一个独立的宿主项目示例：

```text
examples/integration/thirdparty_collision_project
```

它会先创建宿主拥有的 `pugixml` 和 `leveldb` target，再添加 cnetmod。如果 cnetmod 重复创建 target，这个项目会在 CMake 配置阶段失败。

在 cnetmod 仓库根目录构建：

```bash
cmake -S examples/integration/thirdparty_collision_project \
      -B build-thirdparty-collision \
      -DCNETMOD_SOURCE_DIR=$PWD
cmake --build build-thirdparty-collision --target thirdparty_collision_app
```

Windows PowerShell：

```powershell
cmake -S .\examples\integration\thirdparty_collision_project `
      -B .\build-thirdparty-collision `
      -DCNETMOD_SOURCE_DIR="$PWD"
cmake --build .\build-thirdparty-collision --target thirdparty_collision_app --config Release
```

预期输出：

```text
third-party collision integration OK
pugixml root=cnetmod
leveldb version=1.23
io_context ready=true
```
