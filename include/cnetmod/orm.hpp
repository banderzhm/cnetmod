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
