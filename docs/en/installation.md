# Installation Guide

This guide covers installing cnetmod on Windows, Linux, and macOS.

## System Requirements

### Compiler Requirements

cnetmod requires a C++23 compiler with full module support:

| Platform | Compiler | Minimum Version |
|----------|----------|-----------------|
| Windows | MSVC | Visual Studio 2022 17.12+ |
| Linux | Clang + libc++ | clang-21+ |
| macOS | Homebrew LLVM | llvm@21+ |

### Build Tools

- **CMake**: 4.0 or later (required for C++23 module support)
- **Ninja** (recommended) or Make
- **Git**: For cloning the repository

### Platform-Specific Dependencies

#### Windows
- Visual Studio 2022 with "Desktop development with C++" workload
- Windows SDK 10.0.22000.0 or later

#### Linux
- `libc++-21-dev` and `libc++abi-21-dev`
- `liburing-dev` (for io_uring support)
- `libssl-dev` (optional, for SSL/TLS)
- `zlib1g-dev` (optional, for compression)

#### macOS
- Xcode Command Line Tools
- Homebrew LLVM (system clang doesn't support C++23 modules)

## Installation Steps

### Windows (MSVC)

#### 1. Install Visual Studio 2022

Download and install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) (Community, Professional, or Enterprise).

During installation, select:
- "Desktop development with C++"
- "C++ CMake tools for Windows"
- "C++ Modules for v143 build tools"

#### 2. Clone the Repository

```cmd
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod
git submodule update --init --recursive
```

#### 3. Configure and Build

Open "Developer Command Prompt for VS 2022":

```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCNETMOD_BUILD_EXAMPLES=ON ^
  -DCNETMOD_BUILD_TESTS=ON

cmake --build build --config Release
```

#### 4. Run Examples

```cmd
build\Release\examples\echo_server.exe
```

### Linux (Ubuntu/Debian)

#### 1. Install LLVM and Dependencies

```bash
# Add LLVM repository
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 21 all

# Install dependencies
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

#### 2. Clone the Repository

```bash
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod
git submodule update --init --recursive
```

#### 3. Configure and Build

```bash
cmake -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++-21 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCNETMOD_BUILD_EXAMPLES=ON \
  -DCNETMOD_BUILD_TESTS=ON

cmake --build build
```

#### 4. Run Examples

```bash
./build/examples/echo_server
```

### macOS

#### 1. Install Homebrew LLVM

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install LLVM and dependencies
brew install llvm ninja cmake git
```

#### 2. Set Up Environment

Add to your `~/.zshrc` or `~/.bash_profile`:

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

Reload your shell:
```bash
source ~/.zshrc  # or source ~/.bash_profile
```

#### 3. Clone the Repository

```bash
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod
git submodule update --init --recursive
```

#### 4. Configure and Build

```bash
cmake -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCNETMOD_BUILD_EXAMPLES=ON \
  -DCNETMOD_BUILD_TESTS=ON

cmake --build build
```

#### 5. Run Examples

```bash
./build/examples/echo_server
```

## Build Options

Configure cnetmod with these CMake options:

| Option | Default | Description |
|--------|---------|-------------|
| `CNETMOD_BUILD_EXAMPLES` | `ON` | Build example programs |
| `CNETMOD_BUILD_TESTS` | `OFF` | Build unit tests |
| `CNETMOD_ENABLE_SSL` | `ON` | Enable SSL/TLS support (requires OpenSSL) |
| `LIBCXX_MODULE_DIRS` | Auto-detected | Path to standard library modules |
| `LIBCXX_INCLUDE_DIRS` | Auto-detected | Path to standard library headers |

Example with custom options:

```bash
cmake -B build \
  -DCNETMOD_BUILD_EXAMPLES=OFF \
  -DCNETMOD_BUILD_TESTS=ON \
  -DCNETMOD_ENABLE_SSL=OFF
```

## Manual Module Path Configuration

If CMake fails to auto-detect standard library module paths, specify them manually:

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

## Verifying Installation

### Run Tests

```bash
# Linux/macOS
./build/testing/tests/cnetmod_tests

# Windows
build\Release\testing\tests\cnetmod_tests.exe
```

### Run Benchmarks

```bash
# Linux/macOS
./build/testing/bench/cnetmod_bench

# Windows
build\Release\testing\bench\cnetmod_bench.exe
```

## Troubleshooting

### CMake Cannot Find Compiler

**Problem**: `CMake Error: CMAKE_CXX_COMPILER not set`

**Solution**: Explicitly specify the compiler:
```bash
cmake -B build -DCMAKE_CXX_COMPILER=clang++-21
```

### Module Path Not Found

**Problem**: `fatal error: module 'std' not found`

**Solution**: See [Manual Module Path Configuration](#manual-module-path-configuration) above.

### MSVC Error C1605 (Object File Too Large)

**Problem**: `fatal error C1605: object file size exceeds 4 GB limit`

**Solution**: Use Release build or see [MSVC_C1605_Issue.md](../MSVC_C1605_Issue.md) for workarounds.

### Linux: io_uring Not Found

**Problem**: `Could not find io_uring`

**Solution**: Install liburing:
```bash
sudo apt install liburing-dev
```

### macOS: System Clang Used Instead of Homebrew LLVM

**Problem**: CMake uses `/usr/bin/clang` instead of Homebrew LLVM

**Solution**: Ensure Homebrew LLVM is in PATH:
```bash
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
cmake -B build -DCMAKE_CXX_COMPILER=clang++
```

## Next Steps

- **[Quick Start Guide](getting-started.md)** - Write your first cnetmod program
- **[Architecture Overview](architecture.md)** - Understand how cnetmod works
- **[Examples](examples.md)** - Browse complete example applications

## Getting Help

- **GitHub Issues**: [Report bugs or ask questions](https://github.com/banderzhm/cnetmod/issues)
- **Documentation**: Browse the full docs at `docs/`
- **Examples**: Check `examples/` directory for working code
