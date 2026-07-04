# Third-party Dependency Integration

This guide describes how to integrate cnetmod into a host project that already
uses some of the same third-party libraries as cnetmod.

## Problem

A downstream project may already define targets such as:

- `pugixml::pugixml`
- `pugixml-static`
- `nghttp2_static`
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
- `nghttp2_static` or `nghttp2`
- `leveldb`, `leveldb::leveldb`, or `LevelDB::LevelDB`

For header-only dependencies, the host can override:

- `CNETMOD_STDEXEC_INCLUDE_DIR`
- `CNETMOD_JWT_CPP_INCLUDE_DIR`
- `CNETMOD_JSON_INCLUDE_DIR`
- `CNETMOD_JSON_MODULE`

## Example Project

The repository includes a standalone integration example:

```text
examples/integration/thirdparty_collision_project
```

It intentionally creates host-owned `pugixml`, `nghttp2`, and `leveldb` targets
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
