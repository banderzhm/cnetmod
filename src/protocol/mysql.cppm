export module cnetmod.protocol.mysql;

export import :diagnostics;
export import :types;
export import :error_codes;
export import :format_sql;
export import :connection_client;
export import :pool;
export import :pipeline;
export import :transaction;
// ORM is exposed through cnetmod.orm and application repository factories.
// The protocol module exports only MySQL transport and wire primitives.
#ifdef CNETMOD_HAS_ORM
export import :orm_mysql_result_adapter;
export import :orm_stream_cursor;
#endif
// Internal wire/auth partitions are consumed by the connection implementation.
import :protocol;
import :auth;
import :wire_deserialization;
import :wire_serialization;
