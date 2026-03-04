#pragma once

// =============================================================================
// CNETMOD_MODEL / CNETMOD_FIELD Macros
// =============================================================================
//
// Usage (after import cnetmod.protocol.mysql;):
//
//   #include <cnetmod/orm.hpp>
//
//   struct User {
//       std::int64_t  id;
//       std::string   name;
//       std::string   email;
//   };
//
//   CNETMOD_MODEL(User, "users",
//       CNETMOD_FIELD(id,    "id",    bigint,  PK | AUTO_INC),
//       CNETMOD_FIELD(name,  "name",  varchar),
//       CNETMOD_FIELD(email, "email", varchar, NULLABLE)
//   )

// Flag aliases (for user convenience)
#define NONE                ::cnetmod::mysql::orm::col_flag::none
#define PK                  ::cnetmod::mysql::orm::col_flag::primary_key
#define AUTO_INC            ::cnetmod::mysql::orm::col_flag::auto_increment
#define NULLABLE            ::cnetmod::mysql::orm::col_flag::nullable
#define VERSION             ::cnetmod::mysql::orm::col_flag::version
#define LOGIC_DELETE        ::cnetmod::mysql::orm::col_flag::logic_delete
#define FILL_INSERT         ::cnetmod::mysql::orm::col_flag::fill_insert
#define FILL_INSERT_UPDATE  ::cnetmod::mysql::orm::col_flag::fill_insert_update
#define TENANT_ID           ::cnetmod::mysql::orm::col_flag::tenant_id

// ID strategy composite flag aliases
// UUID_PK   — Primary key + UUID strategy (C++ type uses orm::uuid, DDL generates CHAR(36))
// SNOWFLAKE_PK — Primary key + Snowflake strategy (C++ type uses int64_t, DDL generates BIGINT)
//
// Usage:
//   CNETMOD_FIELD(id, "id", char_,  UUID_PK)
//   CNETMOD_FIELD(id, "id", bigint, SNOWFLAKE_PK)

// Internal use: composite flags carrying strategy tag
// UUID_PK expands to CNETMOD_FIELD_5(member, col, type, PK, id_strategy::uuid)
// But designed as 4-parameter form for user simplicity, strategy expanded via macro
#define UUID_PK_FLAGS      ::cnetmod::mysql::orm::col_flag::primary_key
#define UUID_PK_STRATEGY   ::cnetmod::mysql::orm::id_strategy::uuid
#define SNOWFLAKE_PK_FLAGS ::cnetmod::mysql::orm::col_flag::primary_key
#define SNOWFLAKE_PK_STRATEGY ::cnetmod::mysql::orm::id_strategy::snowflake

// CNETMOD_FIELD(member, "col_name", column_type_suffix) — 3 参数
#define CNETMOD_FIELD_3(M, COL, CT) \
    ::cnetmod::mysql::orm::field_mapping<_cnetmod_model_type>{ \
        {#M, COL, ::cnetmod::mysql::column_type::CT, \
         ::cnetmod::mysql::orm::col_flag::none, \
         ::cnetmod::mysql::orm::id_strategy::none}, \
        [](auto& obj, const ::cnetmod::mysql::field_value& v) { \
            ::cnetmod::mysql::orm::detail::set_member(obj.M, v); \
        }, \
        [](const auto& obj) -> ::cnetmod::mysql::param_value { \
            return ::cnetmod::mysql::orm::detail::get_member(obj.M); \
        } \
    }

// CNETMOD_FIELD(member, "col_name", column_type_suffix, flags) — 4 参数
#define CNETMOD_FIELD_4(M, COL, CT, FLAGS) \
    ::cnetmod::mysql::orm::field_mapping<_cnetmod_model_type>{ \
        {#M, COL, ::cnetmod::mysql::column_type::CT, FLAGS, \
         ::cnetmod::mysql::orm::id_strategy::none}, \
        [](auto& obj, const ::cnetmod::mysql::field_value& v) { \
            ::cnetmod::mysql::orm::detail::set_member(obj.M, v); \
        }, \
        [](const auto& obj) -> ::cnetmod::mysql::param_value { \
            return ::cnetmod::mysql::orm::detail::get_member(obj.M); \
        } \
    }

// CNETMOD_FIELD(member, "col_name", column_type_suffix, flags, strategy) — 5 参数
#define CNETMOD_FIELD_5(M, COL, CT, FLAGS, STRATEGY) \
    ::cnetmod::mysql::orm::field_mapping<_cnetmod_model_type>{ \
        {#M, COL, ::cnetmod::mysql::column_type::CT, FLAGS, STRATEGY}, \
        [](auto& obj, const ::cnetmod::mysql::field_value& v) { \
            ::cnetmod::mysql::orm::detail::set_member(obj.M, v); \
        }, \
        [](const auto& obj) -> ::cnetmod::mysql::param_value { \
            return ::cnetmod::mysql::orm::detail::get_member(obj.M); \
        } \
    }

// Simplified: UUID_PK / SNOWFLAKE_PK used as 4th parameter of CNETMOD_FIELD
// Actually expands to CNETMOD_FIELD_5, but transparent to users
#define CNETMOD_FIELD_UUID_PK(M, COL, CT) \
    CNETMOD_FIELD_5(M, COL, CT, UUID_PK_FLAGS, UUID_PK_STRATEGY)
#define CNETMOD_FIELD_SNOWFLAKE_PK(M, COL, CT) \
    CNETMOD_FIELD_5(M, COL, CT, SNOWFLAKE_PK_FLAGS, SNOWFLAKE_PK_STRATEGY)

// Select 3 / 4 / 5 parameter version
#define CNETMOD_FIELD_SELECT(_1, _2, _3, _4, _5, NAME, ...) NAME
#define CNETMOD_FIELD(...) \
    CNETMOD_FIELD_SELECT(__VA_ARGS__, CNETMOD_FIELD_5, CNETMOD_FIELD_4, CNETMOD_FIELD_3)(__VA_ARGS__)

// CNETMOD_MODEL(Type, "table_name", CNETMOD_FIELD(...), ...)
#define CNETMOD_MODEL(TYPE, TABLE, ...) \
    template<> \
    struct ::cnetmod::mysql::orm::model_traits<TYPE> { \
        using _cnetmod_model_type = TYPE; \
        static auto meta() -> const ::cnetmod::mysql::orm::table_meta<TYPE>& { \
            static const ::cnetmod::mysql::orm::field_mapping<TYPE> fields_[] = { \
                __VA_ARGS__ \
            }; \
            static const ::cnetmod::mysql::orm::table_meta<TYPE> m{ \
                TABLE, \
                std::span<const ::cnetmod::mysql::orm::field_mapping<TYPE>>( \
                    fields_, sizeof(fields_) / sizeof(fields_[0])) \
            }; \
            return m; \
        } \
    };

// Helper macro to define field name constants
// Usage after CNETMOD_MODEL:
//   CNETMOD_FIELDS(AiPrompt, id, name, prompt_template, is_default)
#define CNETMOD_FIELDS(TYPE, ...) \
    namespace orm_fields { \
        struct TYPE { \
            CNETMOD_FIELD_CONSTANTS(__VA_ARGS__) \
        }; \
    }

#define CNETMOD_FIELD_CONSTANTS(...) \
    CNETMOD_FIELD_CONST_EACH(__VA_ARGS__)

// Generate: static constexpr std::string_view field_name = "field_name";
#define CNETMOD_FIELD_CONST(NAME) \
    static constexpr std::string_view NAME = #NAME;

// Variadic expansion for up to 20 fields
#define CNETMOD_FIELD_CONST_EACH(...) \
    CNETMOD_FIELD_CONST_EXPAND(CNETMOD_FIELD_CONST, __VA_ARGS__)

#define CNETMOD_FIELD_CONST_EXPAND(MACRO, ...) \
    CNETMOD_FIELD_CONST_IMPL(MACRO, __VA_ARGS__, \
        ~, ~, ~, ~, ~, ~, ~, ~, ~, ~, ~, ~, ~, ~, ~, ~, ~, ~, ~, ~)

#define CNETMOD_FIELD_CONST_IMPL(M, \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, \
    _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, ...) \
    CNETMOD_FIELD_CONST_APPLY(M, _1) \
    CNETMOD_FIELD_CONST_APPLY(M, _2) \
    CNETMOD_FIELD_CONST_APPLY(M, _3) \
    CNETMOD_FIELD_CONST_APPLY(M, _4) \
    CNETMOD_FIELD_CONST_APPLY(M, _5) \
    CNETMOD_FIELD_CONST_APPLY(M, _6) \
    CNETMOD_FIELD_CONST_APPLY(M, _7) \
    CNETMOD_FIELD_CONST_APPLY(M, _8) \
    CNETMOD_FIELD_CONST_APPLY(M, _9) \
    CNETMOD_FIELD_CONST_APPLY(M, _10) \
    CNETMOD_FIELD_CONST_APPLY(M, _11) \
    CNETMOD_FIELD_CONST_APPLY(M, _12) \
    CNETMOD_FIELD_CONST_APPLY(M, _13) \
    CNETMOD_FIELD_CONST_APPLY(M, _14) \
    CNETMOD_FIELD_CONST_APPLY(M, _15) \
    CNETMOD_FIELD_CONST_APPLY(M, _16) \
    CNETMOD_FIELD_CONST_APPLY(M, _17) \
    CNETMOD_FIELD_CONST_APPLY(M, _18) \
    CNETMOD_FIELD_CONST_APPLY(M, _19) \
    CNETMOD_FIELD_CONST_APPLY(M, _20)

#define CNETMOD_FIELD_CONST_APPLY(M, X) \
    CNETMOD_FIELD_CONST_IF_NOT_TILDE(X, M(X))

#define CNETMOD_FIELD_CONST_IF_NOT_TILDE(X, ...) \
    CNETMOD_FIELD_CONST_CHECK(CNETMOD_FIELD_CONST_IS_TILDE(X), __VA_ARGS__)

#define CNETMOD_FIELD_CONST_CHECK(COND, ...) \
    CNETMOD_FIELD_CONST_CHECK_##COND(__VA_ARGS__)

#define CNETMOD_FIELD_CONST_CHECK_0(...) __VA_ARGS__
#define CNETMOD_FIELD_CONST_CHECK_1(...)

#define CNETMOD_FIELD_CONST_IS_TILDE(X) \
    CNETMOD_FIELD_CONST_IS_TILDE_##X

#define CNETMOD_FIELD_CONST_IS_TILDE_~ 1

