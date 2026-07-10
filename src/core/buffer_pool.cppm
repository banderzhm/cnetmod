module;

#include <cnetmod/config.hpp>

export module cnetmod.core.buffer_pool;

import std;
import cnetmod.core.buffer;

namespace cnetmod {

export class buffer_pool;

/// RAII handle for a block leased from buffer_pool.
export class pooled_buffer {
public:
    pooled_buffer() noexcept = default;
    ~pooled_buffer();

    pooled_buffer(const pooled_buffer&) = delete;
    auto operator=(const pooled_buffer&) -> pooled_buffer& = delete;
    pooled_buffer(pooled_buffer&& other) noexcept;
    auto operator=(pooled_buffer&& other) noexcept -> pooled_buffer&;

    [[nodiscard]] auto data() noexcept -> void*;
    [[nodiscard]] auto data() const noexcept -> const void*;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto valid() const noexcept -> bool;
    operator mutable_buffer() noexcept;
    operator const_buffer() const noexcept;
    void release() noexcept;

private:
    friend class buffer_pool;
    pooled_buffer(buffer_pool* pool, void* data, std::size_t size) noexcept;

    buffer_pool* pool_ = nullptr;
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

export class buffer_pool {
public:
    explicit buffer_pool(std::size_t block_size = 4096,
                         std::size_t max_blocks = 1024) noexcept;
    ~buffer_pool();

    buffer_pool(const buffer_pool&) = delete;
    auto operator=(const buffer_pool&) -> buffer_pool& = delete;

    [[nodiscard]] auto acquire() -> pooled_buffer;
    [[nodiscard]] auto pool_size() const noexcept -> std::size_t;
    [[nodiscard]] auto block_size() const noexcept -> std::size_t;

private:
    friend class pooled_buffer;
    struct block_node { block_node* next = nullptr; };

    void return_block(void* data) noexcept;
    auto allocate_new() -> pooled_buffer;

    std::size_t block_size_;
    std::size_t max_blocks_;
    std::atomic<block_node*> free_head_{nullptr};
    std::atomic<std::size_t> pool_size_{0};
};

} // namespace cnetmod
