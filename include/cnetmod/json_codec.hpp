#pragma once

/**
 * Makes the default Glaze JSON codec templates visible to C++ module consumers.
 *
 * Include this header before importing `cnetmod.json` when a consumer defines
 * application-owned reflected types. MSVC currently requires the concrete
 * codec templates in the global module fragment. Clang obtains them from the
 * named module and must not see the headers again before `import std`.
 */
#ifdef _MSC_VER
    #include <glaze/json/read.hpp>
    #include <glaze/json/generic.hpp>
    #include <glaze/json/write.hpp>
#endif
