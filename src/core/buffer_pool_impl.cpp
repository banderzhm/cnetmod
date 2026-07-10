module cnetmod.core.buffer_pool;

import std;
import cnetmod.core.buffer;

namespace cnetmod
{
    pooled_buffer::pooled_buffer(buffer_pool* pool, void* data, std::size_t size) noexcept
        : pool_(pool), data_(data), size_(size)
    {
    }

    pooled_buffer::~pooled_buffer() { release(); }

    pooled_buffer::pooled_buffer(pooled_buffer&& other) noexcept
        : pool_(std::exchange(other.pool_, nullptr)), data_(std::exchange(other.data_, nullptr)), size_(other.size_)
    {
    }

    auto pooled_buffer::operator=(pooled_buffer&& other) noexcept -> pooled_buffer&
    {
        if (this != &other)
        {
            release();
            pool_ = std::exchange(other.pool_, nullptr);
            data_ = std::exchange(other.data_, nullptr);
            size_ = other.size_;
        }
        return *this;
    }

    auto pooled_buffer::data() noexcept -> void* { return data_; }
    auto pooled_buffer::data() const noexcept -> const void* { return data_; }
    auto pooled_buffer::size() const noexcept -> std::size_t { return size_; }
    auto pooled_buffer::valid() const noexcept -> bool { return data_ != nullptr; }
    pooled_buffer::operator mutable_buffer() noexcept { return {data_, size_}; }
    pooled_buffer::operator const_buffer() const noexcept { return {data_, size_}; }

    void pooled_buffer::release() noexcept
    {
        if (pool_ && data_)
        {
            pool_->return_block(data_);
            pool_ = nullptr;
            data_ = nullptr;
        }
    }

    buffer_pool::buffer_pool(std::size_t block_size, std::size_t max_blocks) noexcept
        : block_size_(block_size), max_blocks_(max_blocks)
    {
    }

    buffer_pool::~buffer_pool()
    {
        auto* node = free_head_.load(std::memory_order_relaxed);
        while (node)
        {
            auto* next = node->next;
            ::operator delete(node);
            node = next;
        }
    }

    auto buffer_pool::acquire() -> pooled_buffer
    {
        auto* node = free_head_.load(std::memory_order_acquire);
        while (node)
        {
            if (free_head_.compare_exchange_weak(node, node->next, std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
            {
                pool_size_.fetch_sub(1, std::memory_order_relaxed);
                return {this, reinterpret_cast<char*>(node) + sizeof(block_node), block_size_};
            }
        }
        return allocate_new();
    }

    auto buffer_pool::pool_size() const noexcept -> std::size_t { return pool_size_.load(std::memory_order_relaxed); }
    auto buffer_pool::block_size() const noexcept -> std::size_t { return block_size_; }

    void buffer_pool::return_block(void* data) noexcept
    {
        auto* node = reinterpret_cast<block_node*>(static_cast<char*>(data) - sizeof(block_node));
        if (pool_size_.load(std::memory_order_relaxed) >= max_blocks_)
        {
            ::operator delete(node);
            return;
        }
        node->next = free_head_.load(std::memory_order_relaxed);
        while (!free_head_.compare_exchange_weak(node->next, node, std::memory_order_release,
                                                 std::memory_order_relaxed))
        {
        }
        pool_size_.fetch_add(1, std::memory_order_relaxed);
    }

    auto buffer_pool::allocate_new() -> pooled_buffer
    {
        auto* mem = ::operator new(sizeof(block_node) + block_size_);
        auto* node = static_cast<block_node*>(mem);
        node->next = nullptr;
        return {this, static_cast<char*>(mem) + sizeof(block_node), block_size_};
    }
} // namespace cnetmod