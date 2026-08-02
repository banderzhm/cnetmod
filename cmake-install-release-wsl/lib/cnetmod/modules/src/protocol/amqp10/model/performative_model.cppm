module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.amqp10:performative_model;

import std;
import :primitive_value;
import :delivery_state;

export namespace cnetmod::amqp10 {

struct open
{
    std::string container_id;
    std::string hostname;
    std::uint32_t max_frame_size = 262144;
    std::uint16_t channel_max = 65535;
    std::chrono::milliseconds idle_timeout{};
    std::vector<symbol> offered_capabilities;
    std::map<symbol, value, std::less<>> properties;
};

struct begin
{
    std::optional<std::uint16_t> remote_channel;
    std::uint32_t next_outgoing_id = 1;
    std::uint32_t incoming_window = 2048;
    std::uint32_t outgoing_window = 2048;
    std::uint32_t handle_max = 65535;
};

struct attach
{
    std::string name;
    std::uint32_t handle = 0;
    role link_role = role::sender;
    sender_settle_mode snd_settle = sender_settle_mode::mixed;
    receiver_settle_mode rcv_settle = receiver_settle_mode::first;
    std::optional<source> source_terminus;
    std::optional<target> target_terminus;
    bool transaction_coordinator = false;
    std::vector<std::pair<binary, std::optional<delivery_outcome>>> unsettled;
    bool incomplete_unsettled = false;
    std::optional<std::uint32_t> initial_delivery_count;
    std::map<symbol, value, std::less<>> properties;
};

struct flow
{
    std::optional<std::uint32_t> next_incoming_id;
    std::uint32_t incoming_window = 0;
    std::uint32_t next_outgoing_id = 0;
    std::uint32_t outgoing_window = 0;
    std::optional<std::uint32_t> handle;
    std::optional<std::uint32_t> delivery_count;
    std::optional<std::uint32_t> link_credit;
    bool drain = false;
    bool echo = false;
};

struct transfer
{
    std::uint32_t handle = 0;
    std::optional<std::uint32_t> delivery_id;
    binary delivery_tag;
    std::optional<std::uint32_t> message_format;
    bool settled = false;
    bool more = false;
    std::optional<delivery_outcome> state;
    bool resume = false;
    bool aborted = false;
    bool batchable = false;
    binary payload;
};

struct disposition
{
    role disposition_role = role::receiver;
    std::uint32_t first = 0;
    std::optional<std::uint32_t> last;
    bool settled = false;
    std::optional<delivery_outcome> state;
    bool batchable = false;
};

struct detach
{
    std::uint32_t handle = 0;
    bool closed = false;
    std::optional<error_condition> error;
};

struct end
{
    std::optional<error_condition> error;
};

struct close
{
    std::optional<error_condition> error;
};

struct coordinator
{
    std::vector<symbol> capabilities;
};

struct declare
{
    std::optional<binary> global_id;
};

struct discharge
{
    binary transaction_id;
    bool fail = false;
};

struct declared
{
    binary transaction_id;
};

using performative =
    std::variant<open, begin, attach, flow, transfer, disposition, detach, end,
        close, coordinator, declare, discharge, declared>;

} // namespace cnetmod::amqp10
