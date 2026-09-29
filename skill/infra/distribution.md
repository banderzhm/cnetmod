# SDK 分发与版本治理

> 用 GitHub Releases 分发经过真实消费门禁的 cnetmod C++23 Modules SDK，并保持源码构建与二进制消费边界清晰。

## 核心原则

1. 根目录 `VERSION` 是项目版本的唯一事实来源。CMake、Conan、vcpkg、Rust、Python 与发布标签必须一致；提交前运行 `python tools/release/check_version.py`。
2. `main` 是稳定主分支，`develop` 是功能完备性集成分支，`release/<major>.<minor>` 是版本维护分支，正式标签使用不可变的 `v<major>.<minor>.<patch>`。
3. 预构建包按精确 ABI 目标分发。编译器大版本、标准库、运行库、CPU 架构、系统基线或构建类型不匹配时，禁止静默回退到相近产物。
4. 发布物是一个目标平台上的完整 SDK；消费者使用 `find_package(cnetmod CONFIG REQUIRED COMPONENTS ...)` 声明所需能力。`cnetmod::core`、`cnetmod::http` 等组件目标是稳定消费接口，当前共同链接兼容目标 `cnetmod::cnetmod_core`。
5. 不分发 BMI/IFC/PCM。C++ Modules 的编译产物与具体编译器和参数强绑定；SDK 分发模块接口源码、静态库、公开头和 CMake 元数据，由消费端编译自己的模块图。
6. `system`、`vcpkg`、`conan` 仍是三种源码依赖模式；预构建 SDK 是构建输出，不是第四种依赖模式。
7. 已发布资产不覆盖。发布流水线先创建草稿、上传校验通过的全部资产，再一次性发布；同名 Release 已存在时直接失败。

## 分支和门禁

| 分支/标签 | 用途 | 必须通过 |
|---|---|---|
| `develop` | 小平台功能完备性集成 | Linux 上的 `core`、`backend`、`complete` 三个配置 |
| `release/2.1` | 2.1 版本线稳定与补丁 | Windows、Linux、macOS 全量构建与测试 |
| `main` | 随时可发布的稳定主线 | 受保护合并，禁止直接推送 |
| `v2.1.0` | 不可变正式发布身份 | 版本契约、三平台 SDK、干净消费项目、校验和、SBOM、构建证明 |

分支保护的具体策略维护在 `.github/BRANCHING.md`。版本标签必须从对应的
`release/<major>.<minor>` 合并到 `main` 后创建。

## 发布物布局

每个 ZIP 都必须包含：

```text
include/cnetmod/                         C/C++ 公开头
lib/cnetmod/modules/                     C++23 模块接口与必要实现片段
lib/cmake/cnetmod/                       find_package 配置与导出目标
lib/                                    静态库
bin/                                    必要的目标平台运行库
share/cnetmod/cnetmod-package-manifest.json
share/cnetmod/licenses/LICENSE
```

包清单记录版本、目标 ID、ABI、系统、架构、编译器、标准库、运行库、平台基线、
构建类型、链接方式、依赖模式和可用组件。Release 还必须包含版本目录 JSON、
`SHA256SUMS`、SPDX JSON SBOM 与 GitHub artifact attestation。

## 下载精确目标

脚手架可以复制 `cmake/cnetmod-fetch.cmake`，然后显式指定版本和目标：

```powershell
cmake `
  -DCNETMOD_VERSION=2.1.0 `
  -DCNETMOD_TARGET=windows-x86_64-msvc-v145-md-release `
  -DCNETMOD_PREFIX="$PWD/3rdparty/cnetmod" `
  -P cmake/cnetmod-fetch.cmake
```

下载器先获取版本目录，再按目标 ID 选择唯一资产并使用目录中的 SHA-256 校验。
目标目录已存在但没有 cnetmod 包清单时，下载器拒绝覆盖。应用的构建脚本必须先完成
这一预构建/下载步骤，再把 `CMAKE_PREFIX_PATH` 指向该目录。

## CMake 消费

```cmake
find_package(cnetmod 2.1 CONFIG REQUIRED COMPONENTS
    core application http mysql redis orm openai)

target_link_libraries(my_backend PRIVATE
    cnetmod::core
    cnetmod::application
    cnetmod::http
    cnetmod::mysql
    cnetmod::redis
    cnetmod::orm
    cnetmod::openai)
```

请求包中不存在的组件必须在配置阶段失败，不能等到链接阶段。消费者仍须使用与目标
ID 一致的工具链和 ABI；系统依赖模式的 Unix SDK 还要求目标机器具有对应系统库。

## 发布操作

1. 在 `develop` 完成功能和三配置门禁。
2. 合并到 `release/<major>.<minor>`，完成三平台全量验证。
3. 更新 `VERSION`，运行版本契约检查，合并到 `main`。
4. 在 `main` 创建签名或受保护标签 `v<version>`。
5. `release-sdk.yml` 构建并安装 SDK，用仓库外的干净消费项目执行
   `find_package`、编译和运行，再生成目录、校验和、SBOM 和构建证明。
6. 发布后验证 GitHub Release 的不可变状态；任何修复使用新补丁版本，禁止替换资产。

## 禁止事项

- 禁止手工修改生成的 `include/cnetmod/version.hpp` 或在源码中硬编码项目版本。
- 禁止把不同 MSVC 运行库、不同 libc++/libstdc++ 或不同系统基线标成同一个目标。
- 禁止仅验证“库能编译”；发布门禁必须从安装目录完成一次干净消费。
- 禁止用 `--clobber` 覆盖已有 Release 资产。
- 禁止把临时构建目录、下载缓存、BMI 或本机绝对路径打入 SDK。
