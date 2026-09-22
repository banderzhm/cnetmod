# JSON

> `cnetmod.json` is the backend-neutral JSON document and typed codec facade. Glaze is a private parser implementation compiled only by `src/json/json.cpp`.

**import**: `import cnetmod.json;`
**macro header**: `#include <cnetmod/json.hpp>`
**sources**: `src/json/json.cppm`, `src/json/json.cpp`

## Boundary rules

1. Application and protocol code imports `cnetmod.json`; it never imports a parser library.
2. Glaze headers and `glz::*` names are restricted to `src/json/json.cpp`.
3. `document`, errors, codecs, and DTO metadata contain only framework or standard-library types.
4. `tools/check_json_boundary.py` enforces the boundary in CTest.

## Dynamic documents

```cpp
import std;
import cnetmod.json;

auto parsed = cnetmod::json::parse_document(
    R"({"service":"orders","replicas":3})");
if (!parsed)
    co_return std::unexpected(parsed.error());

const auto service = parsed->at("service").get<std::string>();
parsed->operator[]("ready") = true;
auto wire = cnetmod::json::write_document(*parsed);
```

`document` supports null, booleans, signed and unsigned integers, floating-point
numbers, strings, arrays, and objects. Binary payloads must use an explicit
application representation such as a byte array or Base64 string because JSON
has no binary value type.

## Plain DTOs

C++23 has no general static reflection. A plain DTO declares its mapping once:

```cpp
#include <cnetmod/json.hpp>

import std;
import cnetmod.json;

struct user_view
{
    std::uint64_t id{};
    std::string name;
    std::optional<std::string> nickname;
};

CNETMOD_JSON(user_view,
    CNETMOD_JSON_FIELD(id),
    CNETMOD_JSON_FIELD(name),
    CNETMOD_JSON_FIELD(nickname))

auto encoded = cnetmod::json::write(user_view{42, "Ada", std::nullopt});
auto decoded = cnetmod::json::parse<user_view>(*encoded);
```

The default codec rejects unknown fields and missing required fields. Optional
members may be absent. `lenient_codec` accepts unknown fields and
`explicit_null_codec` emits disengaged optionals as JSON null.

Typed conversion supports nested mapped DTOs, optionals, enums, sequences, and
string-keyed associative containers. Numeric decoding checks integral sign and
range instead of silently narrowing.

## ORM models and projections

Do not add `CNETMOD_JSON` to a type already declared with `CNETMOD_MODEL` or
`CNETMOD_PROJECTION`. ORM field metadata is also its JSON metadata:

```cpp
CNETMOD_MODEL(user_record, "users",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(name, "name", varchar))

auto wire = cnetmod::json::write(user_record{42, "Ada"});
```

This shared mapping covers ORM scalar fields, `DATE`, `DATETIME`, `TIME`, UUID,
and nullable `DATETIME`. Database temporal values use their canonical
timezone-free SQL text representation.

## Application offload

Use `cnetmod::application::json_template` for parsing or writing on the
Application-managed CPU pool. Its typed operations use the same codec contract,
support cancellation, and resume on the application execution context.

## Codec SPI

A custom codec implements:

```cpp
template <typename T>
static auto decode(std::string_view) -> std::expected<T, std::error_code>;

template <typename T>
static auto encode(const T&) -> std::expected<std::string, std::error_code>;
```

and satisfies `cnetmod::json::codec_for<Codec, T>`. RedisTemplate and other
framework templates accept this SPI without exposing the parser backend.

## Verification

```bash
python tools/check_json_boundary.py
ctest --test-dir build -R "json_backend_boundary|test_json_template|test_orm_json_result_map"
```
