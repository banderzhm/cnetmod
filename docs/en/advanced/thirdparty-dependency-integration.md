# Third-party Dependency Integration

This guide describes how to integrate cnetmod into a host project that already
uses some of the same third-party libraries as cnetmod.

## Problem

A downstream project may already define targets such as:

- `pugixml::pugixml`
- `pugixml-static`
- `leveldb`
- `leveldb::leveldb`

If cnetmod blindly calls `add_subdirectory(3rdparty/...)` for the same libraries,
CMake can fail with duplicate target errors. Header-only dependencies have a
similar concern when the host project wants to use its own checkout path.

## Preferred Model

The host project should own shared dependencies first, then add cnetmod:

1. Create or import dependency targets in the host project.
2. Set cnetmod cache variables for header-only dependency paths when needed.
3. Call `add_subdirectory(cnetmod)`.
4. Link the application against `cnetmod_core`.

cnetmod reuses existing dependency targets when they are already present:

- `pugixml::pugixml`, `pugixml-static`, or `pugixml`
- `leveldb`, `leveldb::leveldb`, or `LevelDB::LevelDB`

For header-only dependencies, the host can override:

- `CNETMOD_STDEXEC_INCLUDE_DIR`
- `CNETMOD_JWT_CPP_INCLUDE_DIR`
- `CNETMOD_JSON_INCLUDE_DIR`
- `CNETMOD_JSON_MODULE`

## vcpkg

The repository includes a `vcpkg.json` manifest. When a host configures with the
vcpkg toolchain file, cnetmod prefers dependencies discovered through that
toolchain before falling back to bundled `3rdparty` directories:

```bash
cmake -B build-vcpkg \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-vcpkg --target cnetmod_build_all
```

When multiple Visual Studio versions are installed, use the included overlay
triplet to force Visual Studio 2026. This is the verified Windows command set:

```bat
set VCPKG_ROOT=<path-to-vcpkg>
set VCPKG_VISUAL_STUDIO_PATH=<path-to-Visual-Studio-2026>

:: Optional: move vcpkg caches off the C drive when space is limited.
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

`X_VCPKG_REGISTRIES_CACHE` and `VCPKG_DOWNLOADS` are optional. They are useful
when the default user-wide vcpkg cache on the C drive does not have enough free
space.

`pugixml` remains a normal Git submodule; package-manager builds
should prefer the package target and only fall back to that submodule when no
system package is available.

## Conan

The repository includes a Conan 2 `conanfile.py`. It generates `CMakeDeps` and
`CMakeToolchain` files and maps Conan package names to the targets cnetmod
understands:

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

For Visual Studio 2026, use Conan 2.30+ and CMake 4.2+ so MSVC 195 and the
`Visual Studio 18 2026` generator are recognized:

```bat
:: Optional: move Conan cache and temp files off the C drive when space is limited.
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

:: Optional: validate recipe export, isolated build, and packaging
conan create . --build=missing -pr:h vs2026 -pr:b vs2026
```

If you use a profile, the VS 2026 essentials are `compiler=msvc`,
`compiler.version=195`, `compiler.cppstd=23`, and
`tools.cmake.cmaketoolchain:generator=Visual Studio 18 2026` in `[conf]`.

`stdexec` is not assumed to exist in every Conan remote. By default cnetmod uses
`3rdparty/stdexec`; set `with_stdexec_package=True` only when your remote
provides the upstream `p2300` recipe. `mimalloc` is enabled by default and can
be disabled with `-o cnetmod/*:with_mimalloc=False`.

## Example Project

The repository includes a standalone integration example:

```text
examples/integration/thirdparty_collision_project
```

It intentionally creates host-owned `pugixml` and `leveldb` targets
before adding cnetmod. If cnetmod creates duplicate targets, this project fails
during CMake configuration.

Build from the cnetmod repository root:

```bash
cmake -S examples/integration/thirdparty_collision_project \
      -B build-thirdparty-collision \
      -DCNETMOD_SOURCE_DIR=$PWD
cmake --build build-thirdparty-collision --target thirdparty_collision_app
```

On Windows PowerShell:

```powershell
cmake -S .\examples\integration\thirdparty_collision_project `
      -B .\build-thirdparty-collision `
      -DCNETMOD_SOURCE_DIR="$PWD"
cmake --build .\build-thirdparty-collision --target thirdparty_collision_app --config Release
```

Expected output:

```text
third-party collision integration OK
pugixml root=cnetmod
leveldb version=1.23
io_context ready=true
```
