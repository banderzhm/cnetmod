export module cnetmod.protocol.postgresql;

export import :connection_options;
export import :query_result;
export import :connection;
export import :connection_pool;
#ifdef CNETMOD_HAS_ORM
// ORM repositories use cnetmod.orm plus the application PostgreSQL gateway.
// The former protocol-local ORM facade is intentionally not exported.
#endif

export namespace cnetmod {
namespace pgsql = postgresql;
}
