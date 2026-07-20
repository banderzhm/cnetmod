export module cnetmod.protocol.mysql:orm;

// =============================================================================
// Core ORM functionality
// =============================================================================
export import :orm_id_gen;
export import :orm_meta;
export import :orm_mapper;
export import :orm_query;
export import :orm_crud;
export import :orm_migrate;
export import :orm_reflect;
export import :orm_enum;
export import :orm_type_handler;

// =============================================================================
// XML Mapper support
// =============================================================================
export import :orm_expr;
export import :orm_xml_parser;
export import :orm_dynamic_sql;
export import :orm_xml_mapper;
export import :orm_xml_crud;

// =============================================================================
// MyBatis-Plus style features
// =============================================================================
export import :orm_base_mapper;
export import :orm_wrapper;
export import :orm_page;
export import :orm_logical_delete;
export import :orm_auto_fill;
export import :orm_optimistic_lock;
export import :orm_result_map;
export import :orm_cache;
export import :orm_multi_tenant;
export import :orm_performance;
export import :orm_generator;

