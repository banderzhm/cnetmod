# CMake Utility Functions

This directory contains reusable CMake utility functions for the cnetmod project.

## EmbedMappers.cmake

Generates a C++ header file containing embedded XML mapper content as string literals.

### Function: `cnetmod_embed_mappers`

Converts XML mapper files into a C++ header with embedded string literals, allowing mappers to be compiled directly into the binary without requiring external XML files at runtime.

### Parameters

- `OUTPUT_FILE` (required) - Path to the generated header file
  - Example: `${CMAKE_CURRENT_BINARY_DIR}/embedded_mappers.hpp`
  
- `MAPPER_DIR` (required) - Directory containing XML mapper files
  - Example: `${CMAKE_CURRENT_SOURCE_DIR}/mappers`
  
- `NAMESPACE` (optional) - C++ namespace for the generated code
  - Default: `embedded_mappers`

### Usage Example

```cmake
# Include the utility
include(${CMAKE_SOURCE_DIR}/cmake/utils/EmbedMappers.cmake)

# Generate embedded mappers header
cnetmod_embed_mappers(
    OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/embedded_mappers.hpp
    MAPPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mappers
    NAMESPACE embedded_mappers
)

# Add the generated header to your target's include path
target_include_directories(my_target PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
```

### Generated Header Structure

The generated header file contains:

1. Individual `constexpr std::string_view` variables for each XML file
2. A `get_all_mappers()` function that returns a `std::flat_map<std::string, std::string_view>`
3. An `all_mappers` static instance for convenient access

Example generated code:

```cpp
// Auto-generated file - DO NOT EDIT
#pragma once

#include <string_view>
#include <flat_map>
#include <string>

namespace embedded_mappers {

inline constexpr std::string_view UserMapper = R"MAPPER_XML(
<?xml version="1.0" encoding="UTF-8"?>
<mapper namespace="UserMapper">
    <!-- XML content here -->
</mapper>
)MAPPER_XML";

inline auto get_all_mappers() -> std::flat_map<std::string, std::string_view> {
    std::flat_map<std::string, std::string_view> mappers;
    mappers.insert({"UserMapper.xml", UserMapper});
    return mappers;
}

inline const auto all_mappers = get_all_mappers();

} // namespace embedded_mappers
```

### Usage in C++ Code

```cpp
#include "embedded_mappers.hpp"
#include <cnetmod/orm.hpp>

// Load all embedded mappers
for (const auto& [name, content] : embedded_mappers::all_mappers) {
    auto result = mapper_registry.load_xml(content);
    if (!result) {
        logger::error("Failed to load mapper: {}", name);
    }
}
```

### Benefits

- **Zero runtime dependencies**: No need to ship XML files separately
- **Faster startup**: No file I/O at runtime
- **Deployment simplicity**: Single binary contains everything
- **Type safety**: Compile-time string literals with `constexpr`
- **Memory efficient**: Uses `std::string_view` to avoid copies

### Notes

- The function uses C++ raw string literals (`R"delimiter(...)delimiter"`) to avoid escaping issues
- All XML files in the mapper directory are automatically included
- The generated header is placed in the build directory, not the source directory
- CMake will regenerate the header if any XML files change
