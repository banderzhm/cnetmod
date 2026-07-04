# 第三方依赖集成

本文说明如何把 cnetmod 集成到一个已经使用了相同第三方库的宿主项目中。

## 问题

下游项目可能已经定义了这些 target：

- `pugixml::pugixml`
- `pugixml-static`
- `nghttp2_static`
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
- `nghttp2_static` 或 `nghttp2`
- `leveldb`、`leveldb::leveldb` 或 `LevelDB::LevelDB`

对于 header-only 依赖，宿主项目可以覆盖：

- `CNETMOD_STDEXEC_INCLUDE_DIR`
- `CNETMOD_JWT_CPP_INCLUDE_DIR`
- `CNETMOD_JSON_INCLUDE_DIR`
- `CNETMOD_JSON_MODULE`

## 示例项目

仓库中包含一个独立的宿主项目示例：

```text
examples/integration/thirdparty_collision_project
```

它会先创建宿主拥有的 `pugixml`、`nghttp2` 和 `leveldb` target，再添加 cnetmod。如果 cnetmod 重复创建 target，这个项目会在 CMake 配置阶段失败。

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
