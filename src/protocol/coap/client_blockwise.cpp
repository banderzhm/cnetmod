module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :client_blockwise;

import std;
import :types;
import :codec;
import :client;
import cnetmod.core.address;
import cnetmod.coro.task;

namespace cnetmod::coap {

auto udp_client::get_blockwise(const endpoint& remote, std::string path,
                               std::uint8_t size_exponent)
    -> task<std::expected<message, std::error_code>>
{
    message assembled;
    std::vector<std::byte> payload;
    std::uint32_t block_number = 0;

    for (;;) {
        auto req = make_request(request_options{
            .method_code = method::get,
            .path = path,
        });
        req.add_option(option_number::block2,
            encode_block_option(block_option{
                .number = block_number,
                .more = false,
                .size_exponent = size_exponent,
            }));

        auto resp = co_await request(remote, std::move(req));
        if (!resp) {
            co_return std::unexpected(resp.error());
        }
        if (!resp->is_response() || (resp->code >> 5) != 2) {
            co_return std::move(*resp);
        }

        if (block_number == 0) {
            assembled = *resp;
            assembled.payload.clear();
        }
        payload.insert(payload.end(), resp->payload.begin(), resp->payload.end());

        auto block_opt = resp->first_option(option_number::block2);
        if (!block_opt) {
            assembled = std::move(*resp);
            co_return assembled;
        }
        auto block = decode_block_option(block_opt->value);
        if (!block) {
            co_return std::unexpected(block.error());
        }
        if (!block->more) {
            assembled.payload = std::move(payload);
            co_return assembled;
        }
        block_number = block->number + 1;
        size_exponent = block->size_exponent;
    }
}

auto udp_client::post_blockwise(const endpoint& remote, std::string path,
                                std::vector<std::byte> payload,
                                content_format format, std::uint8_t size_exponent)
    -> task<std::expected<message, std::error_code>>
{
    co_return co_await upload_blockwise(remote, method::post, std::move(path),
        std::move(payload), format, size_exponent);
}

auto udp_client::put_blockwise(const endpoint& remote, std::string path,
                               std::vector<std::byte> payload,
                               content_format format, std::uint8_t size_exponent)
    -> task<std::expected<message, std::error_code>>
{
    co_return co_await upload_blockwise(remote, method::put, std::move(path),
        std::move(payload), format, size_exponent);
}

auto udp_client::upload_blockwise(const endpoint& remote, method method_code, std::string path,
                                  std::vector<std::byte> payload, content_format format,
                                  std::uint8_t size_exponent)
    -> task<std::expected<message, std::error_code>>
{
    size_exponent = std::min<std::uint8_t>(size_exponent, 6);
    const auto block_size = std::size_t{1} << (size_exponent + 4);
    const auto token = next_token();
    const auto total = payload.size();

    for (std::uint32_t block_number = 0, offset = 0;; ++block_number) {
        const auto remaining = total - offset;
        const auto count = std::min(block_size, remaining);
        const auto more = offset + count < total;

        std::vector<std::byte> chunk;
        chunk.reserve(count);
        chunk.insert(chunk.end(),
            payload.begin() + static_cast<std::ptrdiff_t>(offset),
            payload.begin() + static_cast<std::ptrdiff_t>(offset + count));

        auto req = make_request(request_options{
            .method_code = method_code,
            .path = path,
            .content_type = format,
            .payload = std::move(chunk),
        });
        req.token = token;
        req.add_option(option_number::block1,
            encode_block_option(block_option{
                .number = block_number,
                .more = more,
                .size_exponent = size_exponent,
            }));
        if (block_number == 0) {
            req.add_uint_option(option_number::size1, static_cast<std::uint32_t>(total));
        }

        auto resp = co_await request(remote, std::move(req));
        if (!resp) {
            co_return std::unexpected(resp.error());
        }
        if (!more) {
            co_return std::move(*resp);
        }
        if (resp->response() != response_code::continue_) {
            co_return std::move(*resp);
        }

        offset += static_cast<std::uint32_t>(count);
    }
}

} // namespace cnetmod::coap
