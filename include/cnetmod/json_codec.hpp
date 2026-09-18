#pragma once

/**
 * Makes the default Glaze JSON codec templates visible to C++ module consumers.
 *
 * Include this header before importing `cnetmod.json` when a consumer defines
 * application-owned reflected types. This compatibility boundary keeps Glaze
 * headers out of cnetmod's public module declarations while supporting MSVC's
 * current template visibility requirements.
 */
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
