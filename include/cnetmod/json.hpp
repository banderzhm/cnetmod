#pragma once

/**
 * @brief Declares JSON fields for a plain DTO.
 *
 * Import `cnetmod.json` before using these macros. The generated specialization
 * contains only field descriptors; parsing and serialization remain in the
 * compiled cnetmod JSON backend.
 */
#define CNETMOD_JSON(TYPE, ...)                   \
    template <>                                   \
    struct ::cnetmod::json::document_traits<TYPE> \
    {                                             \
        using _cnetmod_json_type = TYPE;          \
        static constexpr auto fields()            \
        {                                         \
            return std::tuple{__VA_ARGS__};       \
        }                                         \
    };

/**
 * @brief Maps a JSON member name to the identically named DTO member.
 */
#define CNETMOD_JSON_FIELD(MEMBER) \
    ::cnetmod::json::field(#MEMBER, &_cnetmod_json_type::MEMBER)
