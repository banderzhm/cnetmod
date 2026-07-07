/// cnetmod.protocol.coap:types - RFC 7252 CoAP types.

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.coap:types;

import std;

namespace cnetmod::coap {

export inline constexpr std::uint16_t default_port = 5683;
export inline constexpr std::uint16_t default_secure_port = 5684;
export inline constexpr std::size_t max_token_size = 8;
export inline constexpr std::size_t default_max_datagram_size = 1152;

export enum class message_type : std::uint8_t {
    confirmable = 0,
    non_confirmable = 1,
    acknowledgement = 2,
    reset = 3,
};

export enum class method : std::uint8_t {
    get = 1,
    post = 2,
    put = 3,
    delete_ = 4,
    fetch = 5,
    patch = 6,
    ipatch = 7,
};

export enum class response_code : std::uint8_t {
    created = 65,
    deleted = 66,
    valid = 67,
    changed = 68,
    content = 69,
    continue_ = 95,

    bad_request = 128,
    unauthorized = 129,
    bad_option = 130,
    forbidden = 131,
    not_found = 132,
    method_not_allowed = 133,
    not_acceptable = 134,
    request_entity_incomplete = 136,
    conflict = 137,
    precondition_failed = 140,
    request_entity_too_large = 141,
    unsupported_content_format = 143,

    internal_server_error = 160,
    not_implemented = 161,
    bad_gateway = 162,
    service_unavailable = 163,
    gateway_timeout = 164,
    proxying_not_supported = 165,
};

export enum class option_number : std::uint16_t {
    if_match = 1,
    uri_host = 3,
    etag = 4,
    if_none_match = 5,
    observe = 6,
    uri_port = 7,
    location_path = 8,
    uri_path = 11,
    content_format = 12,
    max_age = 14,
    uri_query = 15,
    accept = 17,
    location_query = 20,
    block2 = 23,
    block1 = 27,
    size2 = 28,
    proxy_uri = 35,
    proxy_scheme = 39,
    size1 = 60,
};

export enum class content_format : std::uint16_t {
    text_plain = 0,
    link_format = 40,
    xml = 41,
    octet_stream = 42,
    exi = 47,
    json = 50,
    cbor = 60,
};

export struct option {
    std::uint16_t number = 0;
    std::vector<std::byte> value;

    [[nodiscard]] auto as_string() const -> std::string;
};

export struct block_option {
    std::uint32_t number = 0;
    bool more = false;
    std::uint8_t size_exponent = 6; // 2 ** (size_exponent + 4), default 1024.

    [[nodiscard]] auto block_size() const -> std::size_t;
};

export struct message {
    message_type type = message_type::confirmable;
    std::uint8_t code = 0;
    std::uint16_t message_id = 0;
    std::vector<std::byte> token;
    std::vector<option> options;
    std::vector<std::byte> payload;

    [[nodiscard]] auto is_request() const noexcept -> bool;

    [[nodiscard]] auto is_response() const noexcept -> bool;

    [[nodiscard]] auto method_code() const noexcept -> method;

    [[nodiscard]] auto response() const noexcept -> response_code;

    void set_method(method m) noexcept;

    void set_response(response_code c) noexcept;

    void add_option(std::uint16_t number, std::span<const std::byte> bytes);

    void add_option(option_number number, std::span<const std::byte> bytes);

    void add_string_option(option_number number, std::string_view text);

    void add_uint_option(option_number number, std::uint32_t value);

    [[nodiscard]] auto find_options(option_number number) const -> std::vector<option>;

    [[nodiscard]] auto first_option(option_number number) const -> std::optional<option>;
};

export struct request_options {
    message_type type = message_type::confirmable;
    method method_code = method::get;
    std::string path;
    std::string query;
    std::optional<content_format> content_type;
    std::optional<content_format> accept;
    std::optional<std::uint32_t> observe;
    std::vector<std::byte> payload;
};

export struct client_config {
    std::chrono::milliseconds ack_timeout{2000};
    double ack_random_factor = 1.5;
    std::uint8_t max_retransmit = 4;
    std::chrono::milliseconds exchange_lifetime{247000};
    std::size_t max_datagram_size = default_max_datagram_size;
    std::size_t max_observe_notifications = 0;
};

export struct server_config {
    std::size_t max_datagram_size = default_max_datagram_size;
    bool send_reset_for_malformed_confirmable = true;
    bool enable_observe = true;
    bool enable_resource_discovery = true;
    bool enable_blockwise = true;
    bool enable_proxy = true;
    bool observe_confirmable_notifications = false;
    std::chrono::seconds observe_max_age{60};
    std::chrono::seconds blockwise_transfer_timeout{247};
    std::uint8_t blockwise_size_exponent = 6;
};

export struct inbound_request {
    message request;
    std::string path;
    std::string query;

    [[nodiscard]] auto observe_value() const -> std::optional<std::uint32_t>;
    [[nodiscard]] auto is_observe_register() const -> bool;
    [[nodiscard]] auto is_observe_deregister() const -> bool;
    [[nodiscard]] auto block1() const -> std::optional<block_option>;
    [[nodiscard]] auto block2() const -> std::optional<block_option>;
};

export auto make_code(std::uint8_t klass, std::uint8_t detail) -> std::uint8_t;
export auto code_to_string(std::uint8_t code) -> std::string;
export auto option_uint_value(std::span<const std::byte> value)
    -> std::expected<std::uint32_t, std::error_code>;
export auto encode_uint_value(std::uint32_t value) -> std::vector<std::byte>;
export auto encode_block_option(block_option block) -> std::vector<std::byte>;
export auto decode_block_option(std::span<const std::byte> value)
    -> std::expected<block_option, std::error_code>;
export auto make_response(const message& req, response_code code) -> message;

} // namespace cnetmod::coap
