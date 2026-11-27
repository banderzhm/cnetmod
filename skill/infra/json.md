# JSON

> `cnetmod.json` is the compatibility interface for existing document and typed codec callers. The runtime uses the pinned simdjson source for parsing.

**import**: `import cnetmod.json;`
**macro header**: `#include <cnetmod/json.hpp>`
**sources**: `src/utils/json/json.cppm`, `src/utils/json/json.cpp`

## Boundary rules

1. Application and protocol code imports `cnetmod.json`; it never imports a parser library.
2. New internal JSON code uses native simdjson APIs in implementation units. Preserve existing size, depth and duplicate-key policies when migrating a caller.
3. `document`, errors, codecs, and DTO metadata contain only framework or standard-library types.
4. `tools/check_json_boundary.py` enforces the boundary in CTest.
5. `3rdparty/simdjson/singleheader/simdjson.cpp` is compiled once into the runtime. Keep parser-backed views within the lifetime of their parser and input, and return owned data across asynchronous boundaries.

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
members may be absent. `parse_allow_unknown<T>()` (or the compatibility name
`parse_lenient<T>()`) accepts unknown fields, while
`explicit_null_codec` emits disengaged optionals as JSON null.

`from_document_with_defaults<T>()` and `defaulted_codec` still reject unknown
fields but retain each mapped DTO member's C++ default when that field is
omitted. This policy propagates through nested objects and arrays and is used by
Application typed options.

Typed conversion supports nested mapped DTOs, optionals, enums, sequences, and
string-keyed associative containers. Numeric decoding checks integral sign and
range instead of silently narrowing.

`write()` and `write_explicit_nulls()` serialize mapped DTOs directly into the
output string without building a `document`. The output is byte-identical to
`write_document(to_document(value))`: object keys in name order, the same
string escapes, integers as written, doubles formatted by the backend, and
`serialization_failed` for strings that are not valid UTF-8. Unordered maps and
types with a custom `document_traits::encode` still go through a `document`.
Do not hand-write JSON strings for speed; this path is the fast one.

For a reusable response or connection buffer, use `write_to(output, value)`.
It clears the logical contents but retains and reuses `output`'s capacity, and
returns `expected<void, error_code>`. HTTP handlers can combine it with
`response::generate_body()` so serialization writes directly into the response
builder instead of allocating a temporary string:

```cpp
request.resp()
    .set_status(cnetmod::http::status::ok)
    .set_header("Content-Type", "application/json; charset=utf-8");
auto written = request.resp().generate_body([&](std::string& output) {
    return cnetmod::json::write_to(output, dto);
});
```

`generate_body()` updates `Content-Length`; on serialization failure it drops
the partial body and sets the length to zero. Prefer this path for repeated
dynamic JSON responses. `write()` remains appropriate when the serialized value
must be returned, cached, or passed independently of an HTTP response.

## ORM models and projections

Do not add `CNETMOD_JSON` to a type already declared with `CNETMOD_MODEL` or
`CNETMOD_PROJECTION`. ORM field metadata is also its JSON metadata:

```cpp
CNETMOD_MODEL(user_record, "users",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(name, "name", varchar))

auto wire = cnetmod::orm::write_json(user_record{42, "Ada"});
auto record = cnetmod::orm::parse_json<user_record>(*wire);
```

This shared mapping covers ORM scalar fields, `DATE`, `DATETIME`, `TIME`, UUID,
and nullable `DATETIME`. Database temporal values use their canonical
timezone-free SQL text representation. ORM JSON mapping is implemented with
native simdjson inside the ORM component.

## Embedding trusted JSON text

`cnetmod::json::raw_json` is a non-owning view of JSON text that a trusted
encoder has already produced, such as a PostgreSQL `json`/`jsonb` column. The
direct writer copies it once and drops whitespace outside strings, so the output
stays compact without building a document:

```cpp
struct item_row
{
    std::int64_t id{};
    cnetmod::json::raw_json tags;
};
CNETMOD_JSON(item_row, CNETMOD_JSON_FIELD(id), CNETMOD_JSON_FIELD(tags))

auto wire = cnetmod::json::write(item_row{.id = 7, .tags = {row[6].get_string()}});
```

The direct writer does not validate the text; never wrap request input or other
untrusted strings. Conversion to a document (`to_document`) parses the text and
reports invalid JSON. The view must outlive the write call.

## Application offload

For expensive JSON work, move owned input into an Application
`runtime.offload()` callback and return an owned result. Check request
cancellation before offloading and after resuming on the event loop.

## Codec SPI

A custom codec implements:

```cpp
template <typename T>
static auto decode(std::string_view) -> std::expected<T, std::error_code>;

template <typename T>
static auto encode(const T&) -> std::expected<std::string, std::error_code>;
```

and satisfies `cnetmod::json::codec_for<Codec, T>` for compatibility callers.
Redis value codecs use the same signatures and are supplied explicitly. Redis
handles the encoded string while the caller chooses the serialization library.

## Verification

```bash
python tools/check_json_boundary.py
ctest --test-dir build -R "json_backend_boundary|test_json_template|test_orm_json_result_map"
```
