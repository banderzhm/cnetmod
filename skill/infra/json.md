# JSON

> `cnetmod.json` is a Glaze-only C++23 module. It exposes Glaze's native typed codec and dynamic document without an intermediate DOM or a switchable backend layer.

**import**: `import cnetmod.json;`
**sources**: `src/utils/json/json.cppm`, `src/utils/json/json.cpp`

## Design contract

1. Glaze is the only JSON engine. There is no backend registry, virtual JSON interface, structural validator, or conversion through another DOM.
2. `document` is `glz::generic_u64`, preserving unsigned 64-bit integers in dynamic JSON.
3. `parse<T>()` and `write<T>()` invoke Glaze directly for ordinary C++ types. Registered ORM records use their existing field metadata to produce Glaze's native document so database temporal and identifier wire forms remain stable.
4. Only `src/utils/json/json.cppm` and `src/utils/json/json.cpp` may include Glaze headers or spell `glz::*` names. Other framework code imports `cnetmod.json`.
5. Glaze headers are placed in the module's global module fragment. Consumers import the compiled module rather than including Glaze themselves.
6. The bundled Glaze headers are installed because the public module interface owns a native Glaze document type.

## Dynamic documents

```cpp
import std;
import cnetmod.json;

auto parsed = cnetmod::json::parse_document(
    R"({"service":"orders","replicas":3})");
if (!parsed)
    co_return std::unexpected(parsed.error());

const auto service = parsed->at("service").get<std::string>();
const auto replicas = parsed->at("replicas").as<std::uint32_t>();
parsed->operator[]("ready") = true;

auto wire = cnetmod::json::write_document(*parsed);
```

Use the module helpers instead of emulating nlohmann APIs:

| Operation | API |
|---|---|
| Empty object | `cnetmod::json::object()` |
| Empty array | `cnetmod::json::array()` |
| Object lookup | `cnetmod::json::find(document, key)` |
| Member with fallback | `cnetmod::json::value_or(document, key, fallback)` |
| Array iteration | `document.get_array()` |
| Object iteration | `document.get_object()` |
| Numeric conversion | `document.as<T>()` |
| Exact stored scalar | `document.get<T>()` |
| Semantic comparison | `cnetmod::json::equivalent(left, right)` |

`get<T>()` is for an exact Glaze variant alternative. Parsed integral values
are stored as `std::uint64_t` or `std::int64_t`; use `as<T>()` when narrowing to
an application integer type.

## Typed aggregates

Glaze reflects ordinary public aggregates directly. DTOs do not need generated
traits, registration tables, or field macros when JSON names match member names:

```cpp
import std;
import cnetmod.json;

struct user_view
{
    std::uint64_t id{};
    std::string name;
    std::optional<std::string> nickname;
};

auto encoded = cnetmod::json::write(
    user_view{42, "Ada", std::nullopt});
auto decoded = cnetmod::json::parse<user_view>(*encoded);
```

The default policy rejects unknown fields and missing required fields.
Use `parse_lenient<T>()` when unknown object members are acceptable and
`write_explicit_nulls<T>()` when nullable members must remain on the wire.
Both are fixed Glaze policies; the public parse/write API has no pluggable
backend or caller-supplied JSON codec.

## ORM models and projections

`CNETMOD_MODEL` and `CNETMOD_PROJECTION` metadata remains the ORM source of
truth. The framework installs this bridge automatically: application DTOs do
not declare JSON traits. ORM JSON conversion produces and consumes the same
native `document`; it does not build a second JSON mapping registry.

```cpp
CNETMOD_MODEL(user_record, "users",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(name, "name", varchar))

auto wire = cnetmod::json::write(user_record{42, "Ada"});
```

## Application offload

Use `cnetmod::application::json_template` when parsing or serialization should
run on the Application-managed CPU pool. It calls the same Glaze-only codec,
supports cancellation, and resumes on the application execution context.

## Boundary verification

```bash
python tools/check_json_boundary.py
ctest --test-dir build -R "json_backend_boundary|test_json_template|test_orm_json_result_map"
```

The boundary check rejects nlohmann usage everywhere and rejects direct Glaze
usage outside the two `cnetmod.json` implementation files.
