module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :types;

import std;

namespace cnetmod::coap {

auto option::as_string() const -> std::string {
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

auto block_option::block_size() const -> std::size_t {
    return std::size_t{1} << (size_exponent + 4);
}

auto message::is_request() const noexcept -> bool {
    return code >= 1 && code <= 31;
}

auto message::is_response() const noexcept -> bool {
    return code >= 64;
}

auto message::method_code() const noexcept -> method {
    return static_cast<method>(code);
}

auto message::response() const noexcept -> response_code {
    return static_cast<response_code>(code);
}

void message::set_method(method m) noexcept {
    code = static_cast<std::uint8_t>(m);
}

void message::set_response(response_code c) noexcept {
    code = static_cast<std::uint8_t>(c);
}

void message::add_option(std::uint16_t number, std::span<const std::byte> bytes) {
    options.push_back(option{number, {bytes.begin(), bytes.end()}});
}

void message::add_option(option_number number, std::span<const std::byte> bytes) {
    add_option(static_cast<std::uint16_t>(number), bytes);
}

void message::add_string_option(option_number number, std::string_view text) {
    auto* ptr = reinterpret_cast<const std::byte*>(text.data());
    add_option(number, std::span<const std::byte>{ptr, text.size()});
}

void message::add_uint_option(option_number number, std::uint32_t value) {
    auto bytes = encode_uint_value(value);
    add_option(number, bytes);
}

auto message::find_options(option_number number) const -> std::vector<option> {
    std::vector<option> result;
    const auto n = static_cast<std::uint16_t>(number);
    for (const auto& opt : options) {
        if (opt.number == n) {
            result.push_back(opt);
        }
    }
    return result;
}

auto message::first_option(option_number number) const -> std::optional<option> {
    const auto n = static_cast<std::uint16_t>(number);
    for (const auto& opt : options) {
        if (opt.number == n) {
            return opt;
        }
    }
    return std::nullopt;
}

auto inbound_request::observe_value() const -> std::optional<std::uint32_t> {
    auto opt = request.first_option(option_number::observe);
    if (!opt) {
        return std::nullopt;
    }
    auto value = option_uint_value(opt->value);
    if (!value) {
        return std::nullopt;
    }
    return *value;
}

auto inbound_request::is_observe_register() const -> bool {
    return request.method_code() == method::get && observe_value() == 0;
}

auto inbound_request::is_observe_deregister() const -> bool {
    return request.method_code() == method::get && observe_value() == 1;
}

auto inbound_request::block1() const -> std::optional<block_option> {
    auto opt = request.first_option(option_number::block1);
    if (!opt) {
        return std::nullopt;
    }
    auto block = decode_block_option(opt->value);
    if (!block) {
        return std::nullopt;
    }
    return *block;
}

auto inbound_request::block2() const -> std::optional<block_option> {
    auto opt = request.first_option(option_number::block2);
    if (!opt) {
        return std::nullopt;
    }
    auto block = decode_block_option(opt->value);
    if (!block) {
        return std::nullopt;
    }
    return *block;
}

auto make_code(std::uint8_t klass, std::uint8_t detail) -> std::uint8_t {
    return static_cast<std::uint8_t>((klass << 5) | (detail & 0x1f));
}

auto code_to_string(std::uint8_t code) -> std::string {
    return std::format("{}.{}", code >> 5, code & 0x1f);
}

auto option_uint_value(std::span<const std::byte> value)
    -> std::expected<std::uint32_t, std::error_code>
{
    if (value.size() > 4) {
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }
    std::uint32_t result = 0;
    for (auto b : value) {
        result = (result << 8) | std::to_integer<std::uint8_t>(b);
    }
    return result;
}

auto encode_uint_value(std::uint32_t value) -> std::vector<std::byte> {
    if (value == 0) {
        return {};
    }
    std::vector<std::byte> result;
    bool started = false;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const auto b = static_cast<std::uint8_t>((value >> shift) & 0xff);
        if (b != 0 || started) {
            result.push_back(static_cast<std::byte>(b));
            started = true;
        }
    }
    return result;
}

auto encode_block_option(block_option block) -> std::vector<std::byte> {
    const auto raw = (block.number << 4) |
                     (static_cast<std::uint32_t>(block.more ? 1 : 0) << 3) |
                     (block.size_exponent & 0x07);
    return encode_uint_value(raw);
}

auto decode_block_option(std::span<const std::byte> value)
    -> std::expected<block_option, std::error_code>
{
    auto raw = option_uint_value(value);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    block_option block;
    block.number = *raw >> 4;
    block.more = ((*raw >> 3) & 0x01) != 0;
    block.size_exponent = static_cast<std::uint8_t>(*raw & 0x07);
    if (block.size_exponent > 6) {
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }
    return block;
}

auto make_response(const message& req, response_code code) -> message {
    message resp;
    resp.type = req.type == message_type::confirmable
        ? message_type::acknowledgement
        : message_type::non_confirmable;
    resp.message_id = req.message_id;
    resp.token = req.token;
    resp.set_response(code);
    return resp;
}

} // namespace cnetmod::coap
