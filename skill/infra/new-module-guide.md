# 新增模块指南

> 在 cnetmod 项目中添加核心模块、协议模块、示例和测试的完整步骤。

## 新增核心模块

核心模块位于 `src/core/`、`src/coro/`、`src/io/`、`src/executor/` 等目录下。

### 步骤 1：创建模块接口文件

```cpp
// src/core/my_feature.cppm
module;

#include <cnetmod/config.hpp>

export module cnetmod.core.my_feature;

import std;

namespace cnetmod {

export class my_feature
{
public:
    explicit my_feature(std::string name);
    [[nodiscard]] auto name() const noexcept -> std::string_view;

private:
    std::string name_;
};

} // namespace cnetmod
```

### 步骤 2：创建实现文件

```cpp
// src/core/my_feature_impl.cpp
module cnetmod.core.my_feature;

import std;

namespace cnetmod {

my_feature::my_feature(std::string name)
    : name_(std::move(name))
{
}

auto my_feature::name() const noexcept -> std::string_view
{
    return name_;
}

} // namespace cnetmod
```

### 步骤 3：注册到聚合模块

编辑 `src/core.cppm`，添加 `export import` 行：

```cpp
export module cnetmod.core;

export import cnetmod.core.error;
// ... 已有子模块 ...
export import cnetmod.core.my_feature;   // ← 新增
```

### 步骤 4：CMake 自动收集

根 `CMakeLists.txt` 使用 `file(GLOB_RECURSE ... "src/*.cppm")` 和 `file(GLOB_RECURSE ... "src/*.cpp")` 自动收集源文件，**无需手动注册**。重新 configure 即可识别新文件。

## 新增协议模块

协议模块需要额外的 CMake 注册和条件编译配置。

### 步骤 1：在 Protocols.cmake 注册

编辑 `cmake/Protocols.cmake`，在 `CNETMOD_PROTOCOLS` 列表中添加协议名：

```cmake
set(CNETMOD_PROTOCOLS
    # ... 已有协议 ...
    MY_PROTOCOL              # ← 新增
)
```

设置目录映射和依赖：

```cmake
set(CNETMOD_PROTOCOL_MY_PROTOCOL_DIRECTORY "my_protocol")
# 可选依赖声明：
# set(CNETMOD_PROTOCOL_MY_PROTOCOL_DEPENDS HTTP)
```

这一步自动生成：
- CMake option `CNETMOD_ENABLE_MY_PROTOCOL`
- 编译宏 `CNETMOD_HAS_PROTOCOL_MY_PROTOCOL`

### 步骤 2：创建协议目录与文件

```
src/protocol/my_protocol/
├── my_protocol.cppm          # 协议接口
├── my_protocol_impl.cpp      # 协议实现
└── client.cppm               # 客户端/服务端子模块（可选）
```

接口文件格式与核心模块一致（`module;` → `#include <cnetmod/config.hpp>` → `export module` → `import std;`），额外 import 所依赖的聚合模块（`cnetmod.core`、`cnetmod.coro`、`cnetmod.io`）。

### 步骤 3：创建聚合模块（可选）

如果协议有多个子模块，在 `src/protocol/` 下创建聚合文件：

```cpp
// src/protocol/my_protocol.cppm
export module cnetmod.protocol.my_protocol;

export import cnetmod.protocol.my_protocol.client;
export import cnetmod.protocol.my_protocol.server;
```

### 步骤 4：条件编译

`cnetmod_filter_disabled_protocol_sources()` 会自动过滤被禁用协议的源文件。当 `CNETMOD_ENABLE_MY_PROTOCOL=OFF` 时，该目录下所有文件不参与编译。

可选依赖的链接在 `cmake/3rdparty/ThirdPartyDependencies.cmake` 中配置。

## 添加示例

示例程序放在 `examples/` 目录下，按类别分子目录。

### 创建示例文件

```cpp
// examples/my_protocol/my_demo.cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.my_protocol;

auto main() -> int
{
    std::println("my_protocol demo");
    return 0;
}
```

### 在 examples/CMakeLists.txt 注册

在条件块中创建新列表，并加入 `ALL_EXAMPLES`：

```cmake
set(MY_PROTOCOL_EXAMPLES)
if(CNETMOD_ENABLE_MY_PROTOCOL)
    list(APPEND MY_PROTOCOL_EXAMPLES my_protocol/my_demo)
endif()

set(ALL_EXAMPLES
    ${CORE_EXAMPLES}
    # ... 已有列表 ...
    ${MY_PROTOCOL_EXAMPLES}     # ← 新增
)
```

CMake 自动为每个示例创建 `example_<name>` 目标，链接 `cnetmod_core`。

## 添加测试

测试放在 `testing/tests/`、`testing/bench/`、`testing/messaging/`、`testing/database/` 等目录下。

### 测试文件示例

```cpp
// testing/tests/test_my_feature.cpp
import std;
import cnetmod.core;

auto main() -> int
{
    auto feature = cnetmod::my_feature("test");
    assert(feature.name() == "test");
    std::println("test_my_feature: PASSED");
    return 0;
}
```

### CMake 注册

在对应子目录的 `CMakeLists.txt` 中添加：

```cmake
add_executable(test_my_feature test_my_feature.cpp)
target_link_libraries(test_my_feature PRIVATE cnetmod_core)
set_property(TARGET test_my_feature PROPERTY CXX_MODULE_GENERATION_MODE "SEPARATE")
add_test(NAME test_my_feature COMMAND test_my_feature)
list(APPEND CNETMOD_TEST_TARGETS test_my_feature)
set(CNETMOD_TEST_TARGETS ${CNETMOD_TEST_TARGETS} PARENT_SCOPE)
```

## CMake 注册细节总结

| 操作 | 手动注册 | 说明 |
|------|----------|------|
| 新增 core/coro/io/executor 子模块 | 否（GLOB 自动收集） | 需更新聚合模块 `export import` |
| 新增协议模块 | 是（`Protocols.cmake`） | 自动生成 option 和编译宏 |
| 新增示例 | 是（`examples/CMakeLists.txt`） | 按类别分组，条件编译 |
| 新增测试 | 是（`testing/` 子目录） | 追加到 `CNETMOD_TEST_TARGETS` |

### 构建验证

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build --target cnetmod_core
cmake --build build --target example_my_demo
ctest --test-dir build -R test_my_feature
```

## 参考源码
- `CMakeLists.txt` — 根构建文件（GLOB 自动收集、目标定义）
- `cmake/Protocols.cmake` — 18 个协议注册、依赖声明、源文件过滤
- `src/core.cppm` — 聚合模块（添加子模块的 export import）
- `examples/CMakeLists.txt` — 示例注册（按类别分组、条件编译）
- `testing/CMakeLists.txt` — 测试顶层配置
