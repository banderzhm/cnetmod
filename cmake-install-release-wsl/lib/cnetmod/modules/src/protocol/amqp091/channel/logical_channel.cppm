module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp091:logical_channel;
import std;
import cnetmod.coro.task;
import :protocol_constants;
import :field_table_codec;
import :channel_options;
import :message_delivery;
import :publisher_confirm;

export namespace cnetmod::amqp091 {
class protocol_connection;

class logical_channel final
{
public:
    ~logical_channel();
    logical_channel(const logical_channel&) = delete;
    auto operator=(const logical_channel&) -> logical_channel& = delete;
    [[nodiscard]] auto number() const noexcept -> std::uint16_t;
    [[nodiscard]] auto is_open() const noexcept -> bool;
    auto async_close(std::string reply_text = "client channel close")
        -> task<result<void>>;
    auto async_declare_exchange(exchange_declare_options options,
        field_table arguments = {}) -> task<result<void>>;
    auto async_delete_exchange(std::string name, bool if_unused = false,
        bool no_wait = false) -> task<result<void>>;
    auto async_declare_queue(queue_declare_options options,
        field_table arguments = {})
        -> task<result<queue_declare_result>>;
    auto async_delete_queue(std::string name, bool if_unused = false,
        bool if_empty = false, bool no_wait = false)
        -> task<result<std::uint32_t>>;
    auto async_purge_queue(std::string name, bool no_wait = false)
        -> task<result<std::uint32_t>>;
    auto async_bind_queue(binding_options options, field_table arguments = {})
        -> task<result<void>>;
    auto async_unbind_queue(binding_options options, field_table arguments = {})
        -> task<result<void>>;
    auto async_set_qos(qos_options options) -> task<result<void>>;
    auto async_publish(publish_options options, message message)
        -> task<result<std::uint64_t>>;
    auto async_consume(consume_options options, delivery_handler handler,
        field_table arguments = {}) -> task<result<std::string>>;
    auto async_cancel_consumer(std::string consumer_tag, bool no_wait = false)
        -> task<result<void>>;
    auto async_ack(std::uint64_t delivery_tag, bool multiple = false)
        -> task<result<void>>;
    auto async_nack(std::uint64_t delivery_tag, bool multiple = false,
        bool requeue = true) -> task<result<void>>;
    auto async_reject(std::uint64_t delivery_tag, bool requeue = true)
        -> task<result<void>>;
    auto async_recover(bool requeue = true) -> task<result<void>>;
    auto async_enable_confirms(bool no_wait = false) -> task<result<void>>;
    void observe_confirms(std::weak_ptr<publisher_confirm_observer> observer);
    auto async_select_transaction() -> task<result<void>>;
    auto async_commit_transaction() -> task<result<void>>;
    auto async_rollback_transaction() -> task<result<void>>;

private:
    friend class protocol_connection;
    logical_channel(std::shared_ptr<protocol_connection> connection,
        std::uint16_t number);
    struct impl;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::amqp091
