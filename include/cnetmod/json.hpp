#pragma once

/**
 * @brief Declares backend-neutral JSON fields for a plain DTO.
 *
 * Import `cnetmod.json` before using these macros.
 */
#define CNETMOD_JSON(TYPE, ...)                                      \
    template <>                                                      \
    struct ::cnetmod::json::document_traits<TYPE>                    \
    {                                                                \
        using _cnetmod_json_type = TYPE;                             \
        static constexpr auto fields()                              \
        {                                                            \
            return std::tuple{__VA_ARGS__};                          \
        }                                                            \
    };

#define CNETMOD_JSON_FIELD(MEMBER)                                   \
    ::cnetmod::json::field(#MEMBER, &_cnetmod_json_type::MEMBER)
