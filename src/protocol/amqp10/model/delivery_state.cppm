module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:delivery_state;
import std;
import :primitive_value;

export namespace cnetmod::amqp10 {
enum class role : bool
{
    sender = false,
    receiver = true
};
enum class sender_settle_mode : std::uint8_t
{
    unsettled = 0,
    settled = 1,
    mixed = 2
};
enum class receiver_settle_mode : std::uint8_t
{
    first = 0,
    second = 1
};
enum class terminus_durability : std::uint32_t
{
    none = 0,
    configuration = 1,
    unsettled_state = 2
};
enum class expiry_policy
{
    link_detach,
    session_end,
    connection_close,
    never
};
enum class distribution_mode
{
    move,
    copy
};

struct error_condition
{
    symbol condition;
    std::string description;
    std::map<symbol, value, std::less<>> info;
};

struct source
{
    std::string address;
    terminus_durability durable = terminus_durability::none;
    expiry_policy expiry = expiry_policy::session_end;
    std::uint32_t timeout = 0;
    bool dynamic = false;
    distribution_mode distribution = distribution_mode::move;
    std::optional<value> filter;
    std::vector<symbol> outcomes;
};

struct target
{
    std::string address;
    terminus_durability durable = terminus_durability::none;
    expiry_policy expiry = expiry_policy::session_end;
    std::uint32_t timeout = 0;
    bool dynamic = false;
};
enum class outcome_kind
{
    accepted,
    rejected,
    released,
    modified,
    transactional
};

struct delivery_outcome
{
    outcome_kind kind = outcome_kind::accepted;
    std::optional<error_condition> error;
    bool delivery_failed = false;
    bool undeliverable_here = false;
    std::optional<binary> transaction_id;
    std::shared_ptr<delivery_outcome> transaction_outcome;
};
} // namespace cnetmod::amqp10
