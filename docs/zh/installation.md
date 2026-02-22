# 安装指南

本指南介绍如何在 Windows、Linux 和 macOS 上安装 cnetmod。

## 系统要求

### 编译器要求

cnetmod 需要支持完整模块功能的 C++23 编译器：

| 平台 | 编译器 | 最低版本 |
|----------|----------|-----------------|
| Windows | MSVC | Visual Studio 2022 17.12+ |
| Linux | Clang + libc++ | clang-21+ |
| macOS | Homebrew LLVM | llvm@21+ |

### 构建工具

- **CMake**: 4.0 或更高版本（C++23 模块支持所需）
- **Ninja**（推荐）或 Make
- **Git**: 用于克隆仓库

### 平台特定依赖

#### Windows
- Visual Studio 2022 with "Desktop development with C++" 工作负载
- Windows SDK 10.0.22000.0 或更高版本

#### Linux
- `libc++-21-dev` 和 `libc++abi-21-dev`
- `liburing-dev`（用于 io_uring 支持）
- `libssl-dev`（可选，用于 SSL/TLS）
- `zlib1g-dev`（可选，用于压缩）

#### macOS
- Xcode Command Line Tools
- Homebrew LLVM（系统 clang 不支持 C++23 模块）

## 安装步骤

### Windows (MSVC)

#### 1. 安装 Visual Studio 2022

下载并安装 [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/)（Community、Professional 或 Enterprise）。

安装时选择：
- "Desktop development with C++"
- "C++ CMake tools for Windows"
- "C++ Modules for v143 build tools"

#### 2. 克隆仓库

```cmd
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod
git submodule update --init --recursive
```

#### 3. 配置和构建

打开 "Developer Command Prompt for VS 2022"：

```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCNETMOD_BUILD_EXAMPLES=ON ^
  -DCNETMOD_BUILD_TESTS=ON

cmake --build build --config Release
```

#### 4. 运行示例

```cmd
build\Release\examples\echo_server.exe
```

### Linux (Ubuntu/Debian)

#### 1. 安装 LLVM 和依赖项

```bash
# 添加 LLVM 仓库
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 21 all

# 安装依赖项
sudo apt update
sudo apt install -y \
  clang-21 \
  libc++-21-dev \
  libc++abi-21-dev \
  liburing-dev \
  libssl-dev \
  zlib1g-dev \
  cmake \
  ninja-build \
  git
```

#### 2. 克隆仓库

```bash
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod
git submodule update --init --recursive
```

#### 3. 配置和构建

```bash
cmake -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++-21 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCNETMOD_BUILD_EXAMPLES=ON \
  -DCNETMOD_BUILD_TESTS=ON

cmake --build build
```

#### 4. 运行示例

```bash
./build/examples/echo_server
```

### macOS

#### 1. 安装 Homebrew LLVM

```bash
# 如果尚未安装 Homebrew，先安装
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装 LLVM 和依赖项
brew install llvm ninja cmake git
```

#### 2. 设置环境

添加到你的 `~/.zshrc` 或 `~/.bash_profile`：

```bash
# Apple Silicon (M1/M2/M3)
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
export LDFLAGS="-L/opt/homebrew/opt/llvm/lib"
export CPPFLAGS="-I/opt/homebrew/opt/llvm/include"

# Intel Mac
# export PATH="/usr/local/opt/llvm/bin:$PATH"
# export LDFLAGS="-L/usr/local/opt/llvm/lib"
# export CPPFLAGS="-I/usr/local/opt/llvm/include"
```

重新加载 shell：
```bash
source ~/.zshrc  # 或 source ~/.bash_profile
```

#### 3. 克隆仓库

```bash
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod
git submodule update --init --recursive
```

#### 4. 配置和构建

```bash
cmake -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCNETMOD_BUILD_EXAMPLES=ON \
  -DCNETMOD_BUILD_TESTS=ON

cmake --build build
```

#### 5. 运行示例

```bash
./build/examples/echo_server
```

## 构建选项

使用这些 CMake 选项配置 cnetmod：

| 选项 | 默认值 | 描述 |
|--------|---------|-------------|
| `CNETMOD_BUILD_EXAMPLES` | `ON` | 构建示例程序 |
| `CNETMOD_BUILD_TESTS` | `OFF` | 构建单元测试 |
| `CNETMOD_ENABLE_SSL` | `ON` | 启用 SSL/TLS 支持（需要 OpenSSL） |
| `LIBCXX_MODULE_DIRS` | 自动检测 | 标准库模块路径 |
| `LIBCXX_INCLUDE_DIRS` | 自动检测 | 标准库头文件路径 |

自定义选项示例：

```bash
cmake -B build \
  -DCNETMOD_BUILD_EXAMPLES=OFF \
  -DCNETMOD_BUILD_TESTS=ON \
  -DCNETMOD_ENABLE_SSL=OFF
```

## 手动配置模块路径

如果 CMake 无法自动检测标准库模块路径，请手动指定：

### Linux

```bash
cmake -B build \
  -DLIBCXX_MODULE_DIRS=/usr/lib/llvm-21/share/libc++/v1 \
  -DLIBCXX_INCLUDE_DIRS=/usr/lib/llvm-21/include/c++/v1
```

### macOS

```bash
# Apple Silicon
cmake -B build \
  -DLIBCXX_MODULE_DIRS=/opt/homebrew/opt/llvm/share/libc++/v1 \
  -DLIBCXX_INCLUDE_DIRS=/opt/homebrew/opt/llvm/include/c++/v1

# Intel Mac
cmake -B build \
  -DLIBCXX_MODULE_DIRS=/usr/local/opt/llvm/share/libc++/v1 \
  -DLIBCXX_INCLUDE_DIRS=/usr/local/opt/llvm/include/c++/v1
```

### Windows

```cmd
cmake -B build ^
  -DLIBCXX_MODULE_DIRS="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/modules"
```

## 验证安装

### 运行测试

```bash
# Linux/macOS
./build/testing/tests/cnetmod_tests

# Windows
build\Release\testing\tests\cnetmod_tests.exe
```

### 运行基准测试

```bash
# Linux/macOS
./build/testing/bench/cnetmod_bench

# Windows
build\Release\testing\bench\cnetmod_bench.exe
```

## 故障排除

### CMake 找不到编译器

**问题**: `CMake Error: CMAKE_CXX_COMPILER not set`

**解决方案**: 显式指定编译器：
```bash
cmake -B build -DCMAKE_CXX_COMPILER=clang++-21
```

### 找不到模块路径

**问题**: `fatal error: module 'std' not found`

**解决方案**: 参见上面的[手动配置模块路径](#手动配置模块路径)。

### MSVC 错误 C1605（目标文件过大）

**问题**: `fatal error C1605: object file size exceeds 4 GB limit`

**解决方案**: 使用 Release 构建或参见 [MSVC_C1605_Issue.md](../MSVC_C1605_Issue.md) 了解解决方法。

### Linux: 找不到 io_uring

**问题**: `Could not find io_uring`

**解决方案**: 安装 liburing：
```bash
sudo apt install liburing-dev
```

### macOS: 使用了系统 Clang 而不是 Homebrew LLVM

**问题**: CMake 使用 `/usr/bin/clang` 而不是 Homebrew LLVM

**解决方案**: 确保 Homebrew LLVM 在 PATH 中：
```bash
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
cmake -B build -DCMAKE_CXX_COMPILER=clang++
```

## 下一步

- **[快速入门指南](getting-started.md)** - 编写你的第一个 cnetmod 程序
- **[架构概述](architecture.md)** - 了解 cnetmod 的工作原理
- **[示例](examples.md)** - 浏览完整的示例应用

## 获取帮助

- **GitHub Issues**: [报告错误或提问](https://github.com/banderzhm/cnetmod/issues)
- **文档**: 浏览 `docs/` 目录中的完整文档
- **示例**: 查看 `examples/` 目录中的可运行代码
