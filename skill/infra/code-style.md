# 代码风格指南

> cnetmod 项目的代码格式与命名规范，基于 `.clang-format` 配置。

## 缩进与空白

- **缩进宽度**：4 空格（`IndentWidth: 4`）
- **Tab**：禁止使用，全部转换为空格（`UseTab: Never`）
- **Tab 显示宽度**：4（`TabWidth: 4`）
- **续行缩进**：4 空格（`ContinuationIndentWidth: 4`）
- **构造函数初始化列表缩进**：4 空格（`ConstructorInitializerIndentWidth: 4`）
- **访问修饰符缩进**：与类体对齐，不额外缩进（`AccessModifierOffset: -4`）

## 大括号风格

使用 **Allman 风格**（`BreakBeforeBraces: Custom`）——左大括号独占一行：

```cpp
if (condition)
{
    do_something();
}
else
{
    do_other();
}

class my_class
{
public:
    my_class()
    {
    }
};
```

具体规则（`BraceWrapping`）：

| 场景 | 行为 |
|------|------|
| `AfterClass` | 换行 |
| `AfterFunction` | 换行 |
| `AfterStruct` | 换行 |
| `AfterControlStatement` | Always（换行） |
| `AfterEnum` | 换行 |
| `AfterNamespace` | **不换行** |
| `BeforeCatch` | 换行 |
| `BeforeElse` | 换行 |
| `BeforeWhile` | **不换行**（do-while） |

## 命名约定

| 类别 | 风格 | 示例 |
|------|------|------|
| 类 / 结构体 | snake_case | `dynamic_buffer`, `buffer_reader` |
| 枚举类 | snake_case | `byte_order`, `open_mode` |
| 函数 | snake_case | `read_u16_be()`, `set_level()` |
| 变量 | snake_case | `read_pos_`, `initial_capacity` |
| 类成员变量 | snake_case + `_` 后缀 | `data_`, `size_`, `alignment_` |
| 命名空间 | snake_case | `cnetmod`, `detail`, `logger` |
| 宏 | UPPER_SNAKE_CASE | `CNETMOD_HAS_SSL`, `CNETMOD_PLATFORM_WINDOWS` |

```cpp
export class aligned_buffer
{
public:
    explicit aligned_buffer(std::size_t size,
        std::size_t alignment = 4096);

    [[nodiscard]] auto data() noexcept -> std::byte*;
    [[nodiscard]] auto size() const noexcept -> std::size_t;

private:
    std::byte* data_ = nullptr;       // 成员后缀 _
    std::size_t size_ = 0;
    std::size_t alignment_ = 0;
};
```

## `[[nodiscard]]` 标注

对以下函数**必须**添加 `[[nodiscard]]`：

- 返回资源句柄或指针的函数（`data()`, `writable()`, `readable()`）
- 可能失败的函数（返回 `bool`、`std::expected`、`std::optional`）
- 返回查询结果的 `const` 成员函数（`size()`, `remaining()`, `position()`）

```cpp
[[nodiscard]] auto data() noexcept -> std::byte*;
[[nodiscard]] auto size() const noexcept -> std::size_t;
[[nodiscard]] auto prepare(std::size_t n) -> mutable_buffer;
[[nodiscard]] auto remaining() const noexcept -> std::size_t;
```

## 类结构顺序

按 `public → protected → private` 排列，访问修饰符与类体同级缩进：

```cpp
export class dynamic_buffer
{
public:
    explicit dynamic_buffer(std::size_t initial_capacity = 4096);
    [[nodiscard]] auto prepare(std::size_t n) -> mutable_buffer;
    void commit(std::size_t n) noexcept;
    [[nodiscard]] auto data() const noexcept -> const_buffer;
    void consume(std::size_t n) noexcept;
    [[nodiscard]] auto readable_bytes() const noexcept -> std::size_t;

private:
    std::vector<std::byte> data_;
    std::size_t read_pos_ = 0;
    std::size_t write_pos_ = 0;
};
```

## 指针与引用对齐

- **指针**：左对齐（`PointerAlignment: Left`）

```cpp
std::byte* data_ = nullptr;
const void* data = nullptr;
```

## 行宽限制

- **ColumnLimit: 0** — clang-format 不强制行宽限制
- 建议保持合理的行宽（约 100-120 字符），以提高可读性

## import / include 顺序

1. `module;` 全局片段中的 `#include`（仅 config.hpp 和平台头）
2. `export module` 声明
3. `import std;`
4. `export import :分区;` 或其他模块导入
5. 代码正文

```cpp
module;

#include <cnetmod/config.hpp>          // 1. 配置头

export module cnetmod.core.error;

import std;                             // 3. 标准库

namespace cnetmod {
// 5. 代码正文
}
```

- `IncludeBlocks: Preserve` — 保持原有 include 分组
- `SortIncludes: CaseSensitive` — include 按大小写敏感排序

## 其他格式规则

| 规则 | 值 | 说明 |
|------|------|------|
| `AllowShortFunctionsOnASingleLine` | Empty | 仅空函数可单行 |
| `AllowShortBlocksOnASingleLine` | Empty | 仅空块可单行 |
| `AllowShortIfStatementsOnASingleLine` | Never | if 不允许单行 |
| `AllowShortLoopsOnASingleLine` | false | 循环不允许单行 |
| `BinPackArguments` | true | 函数参数尽量紧凑 |
| `BinPackParameters` | true | 函数形参尽量紧凑 |
| `MaxEmptyLinesToKeep` | 1 | 最多保留 1 个空行 |
| `SeparateDefinitionBlocks` | Always | 定义块之间用空行分隔 |
| `BreakBeforeTernaryOperators` | true | 三元运算符前换行 |
| `BreakConstructorInitializers` | BeforeColon | 初始化列表在冒号前换行 |
| `NamespaceIndentation` | Inner | 命名空间内部缩进 |
| `FixNamespaceComments` | true | 自动添加命名空间结束注释 |

## 返回类型风格

项目使用**后置返回类型**（trailing return type）风格：

```cpp
auto data() noexcept -> std::byte*;
auto read_u16_be() noexcept -> std::optional<std::uint16_t>;
auto prepare(std::size_t n) -> mutable_buffer;
```

简单函数也可直接声明返回类型：

```cpp
void commit(std::size_t n) noexcept;
bool skip(std::size_t n) noexcept;
```

## 参考源码
- `.clang-format` — 完整的格式化配置
- `src/core/buffer.cppm` — 类结构、命名、`[[nodiscard]]` 示例
- `src/core/log.cppm` — 命名空间、函数签名示例
- `src/core/error.cppm` — 枚举定义、全局片段示例
