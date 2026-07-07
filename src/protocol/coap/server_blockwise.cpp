module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :server_blockwise;

import std;
import :types;
import :codec;
import :server;

namespace cnetmod::coap {

auto udp_server::apply_block1(const endpoint& peer, inbound_request& req) -> block1_result {
    if (!cfg_.enable_blockwise) {
        return block1_result::none;
    }
    auto block = req.block1();
    if (!block) {
        return block1_result::none;
    }

    block1_key key{
        .peer = peer.to_string(),
        .token = req.request.token,
        .path = normalize_path(req.path),
    };

    std::scoped_lock lock(block1_mutex_);
    const auto now = std::chrono::steady_clock::now();
    for (auto it = block1_.begin(); it != block1_.end();) {
        if (now - it->second.updated_at > cfg_.blockwise_transfer_timeout) {
            it = block1_.erase(it);
        } else {
            ++it;
        }
    }

    auto& transfer = block1_[key];
    transfer.updated_at = now;
    const auto offset = static_cast<std::size_t>(block->number) * block->block_size();
    if (offset < transfer.payload.size()) {
        const auto end = offset + req.request.payload.size();
        if (end > transfer.payload.size() ||
            !std::ranges::equal(req.request.payload,
                std::span<const std::byte>{
                    transfer.payload.data() + static_cast<std::ptrdiff_t>(offset),
                    req.request.payload.size()})) {
            block1_.erase(key);
            return block1_result::incomplete;
        }
        if (block->more) {
            return block1_result::continue_;
        }
        req.request.payload = std::move(transfer.payload);
        block1_.erase(key);
        return block1_result::complete;
    }
    if (offset != transfer.payload.size()) {
        block1_.erase(key);
        return block1_result::incomplete;
    }

    if (auto size1 = req.request.first_option(option_number::size1)) {
        auto declared = option_uint_value(size1->value);
        if (!declared) {
            block1_.erase(key);
            return block1_result::incomplete;
        }
        if (*declared > cfg_.max_datagram_size * 1024) {
            block1_.erase(key);
            return block1_result::too_large;
        }
        if (!block->more && offset + req.request.payload.size() != *declared) {
            block1_.erase(key);
            return block1_result::incomplete;
        }
    }

    if (transfer.payload.size() < offset + req.request.payload.size()) {
        transfer.payload.resize(offset + req.request.payload.size());
    }
    std::ranges::copy(req.request.payload, transfer.payload.begin() + static_cast<std::ptrdiff_t>(offset));

    if (block->more) {
        return block1_result::continue_;
    }

    req.request.payload = std::move(transfer.payload);
    block1_.erase(key);
    return block1_result::complete;
}

void udp_server::apply_block2(const inbound_request& req, message& resp) {
    if (!cfg_.enable_blockwise || resp.payload.empty()) {
        return;
    }

    auto requested = req.block2();
    auto block = requested.value_or(block_option{
        .number = 0,
        .more = false,
        .size_exponent = cfg_.blockwise_size_exponent,
    });
    block.size_exponent = std::min<std::uint8_t>(block.size_exponent, 6);
    const auto block_size = block.block_size();
    const auto offset = static_cast<std::size_t>(block.number) * block_size;
    if (offset >= resp.payload.size()) {
        resp.payload.clear();
        block.more = false;
        resp.add_option(option_number::block2, encode_block_option(block));
        return;
    }

    const auto remaining = resp.payload.size() - offset;
    if (!requested && serialize_message(resp).transform([](const auto& raw) { return raw.size(); }).value_or(0) <= cfg_.max_datagram_size) {
        return;
    }

    const auto count = std::min(block_size, remaining);
    std::vector<std::byte> sliced{
        resp.payload.begin() + static_cast<std::ptrdiff_t>(offset),
        resp.payload.begin() + static_cast<std::ptrdiff_t>(offset + count),
    };
    block.more = offset + count < resp.payload.size();
    resp.payload = std::move(sliced);
    resp.add_option(option_number::block2, encode_block_option(block));
    resp.add_uint_option(option_number::size2, static_cast<std::uint32_t>(offset + remaining));
}

} // namespace cnetmod::coap
