export module cnetmod.orm.repository;

import cnetmod.orm.repository_impl;
import cnetmod.orm.model_metadata;

export namespace cnetmod::orm {

/**
 * @brief Application persistence facade with MyBatis-Plus IService semantics.
 *
 * Repository owns application persistence orchestration while Mapper owns
 * model-to-SQL mapping. The gateway supplies connection leases; business
 * services should depend on this type and never on a database session.
 */
template <Model T, typename Gateway,
    typename StreamStrategy = session_stream_strategy>
using repository = repository_impl<T, Gateway, StreamStrategy>;

} // namespace cnetmod::orm
