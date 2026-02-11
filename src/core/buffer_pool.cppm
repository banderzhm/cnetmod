module;

#include <cnetmod/config.hpp>

export module cnetmod.core.buffer_pool;

import std;
import cnetmod.core.buffer;

namespace cnetmod {

// =============================================================================
// buffer_pool — lock-free 固定大小 buffer 对象池
// =============================================================================

// Forward declare
export class buffer_pool;

/// RAII buffer 句柄，析构时自动归还到池
export class pooled_buffer {
public:
    pooled_buffer() noexcept = default;

    ~pooled_buffer() { release(); }

    pooled_buffer(const pooled_buffer&) = delete;
    auto operator=(const pooled_buffer&) -> pooled_buffer& = delete;

    pooled_buffer(pooled_buffer&& o) noexcept
        : pool_(std::exchange(o.pool_, nullptr))
        , data_(std::exchange(o.data_, nullptr))
        , size_(o.size_) {}

    auto operator=(pooled_buffer&& o) noexcept -> pooled_buffer& {
        if (this != &o) {
            release();
            pool_ = std::exchange(o.pool_, nullptr);
            data_ = std::exchange(o.data_, nullptr);
            size_ = o.size_;
        }
        return *this;
    }

    [[nodiscard]] auto data() noexcept -> void* { return data_; }
    [[nodiscard]] auto data() const noexcept -> const void* { return data_; }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }
    [[nodiscard]] auto valid() const noexcept -> bool { return data_ != nullptr; }

    /// 隐式转换为 mutable_buffer
    operator mutable_buffer() noexcept { return {data_, size_}; }
    operator const_buffer() const noexcept { return {data_, size_}; }

    /// 手动归还（之后 valid() == false）
    void release();

private:
    friend class buffer_pool;

    pooled_buffer(buffer_pool* pool, void* data, std::size_t size) noexcept
        : pool_(pool), data_(data), size_(size) {}

    buffer_pool* pool_ = nullptr;
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

export class buffer_pool {
public:
    /// @param block_size  每个 buffer 块的大小（字节）
    /// @param max_blocks  池中预分配的最大块数
    explicit buffer_pool(std::size_t block_size = 4096,
                         std::size_t max_blocks = 1024) noexcept
        : block_size_(block_size)
        , max_blocks_(max_blocks) {}

    ~buffer_pool() {
        // 释放 freelist 中所有块
        auto* node = free_head_.load(std::memory_order_relaxed);
        while (node) {
            auto* next = node->next;
            ::operator delete(node);
            node = next;
        }
    }

    buffer_pool(const buffer_pool&) = delete;
    auto operator=(const buffer_pool&) -> buffer_pool& = delete;

    /// 从池中获取一个 buffer（RAII）
    /// 池空时 fallback 到堆分配
    [[nodiscard]] auto acquire() -> pooled_buffer {
        // CAS pop from freelist
        auto* node = free_head_.load(std::memory_order_acquire);
        while (node) {
            if (free_head_.compare_exchange_weak(node, node->next,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                pool_size_.fetch_sub(1, std::memory_order_relaxed);
                void* data = reinterpret_cast<char*>(node) + sizeof(block_node);
                return pooled_buffer{this, data, block_size_};
            }
        }
        // Freelist empty — allocate from heap
        return allocate_new();
    }

    /// 当前池中空闲块数
    [[nodiscard]] auto pool_size() const noexcept -> std::size_t {
        return pool_size_.load(std::memory_order_relaxed);
    }

    /// 每块大小
    [[nodiscard]] auto block_size() const noexcept -> std::size_t {
        return block_size_;
    }

private:
    friend class pooled_buffer;

    struct block_node {
        block_node* next = nullptr;
    };

    void return_block(void* data) noexcept {
        // data 指向 block_node 之后的区域
        auto* node = reinterpret_cast<block_node*>(
            static_cast<char*>(data) - sizeof(block_node));

        auto current_size = pool_size_.load(std::memory_order_relaxed);
        if (current_size >= max_blocks_) {
            // 池已满，直接释放
            ::operator delete(node);
            return;
        }

        // CAS push to freelist
        node->next = free_head_.load(std::memory_order_relaxed);
        while (!free_head_.compare_exchange_weak(node->next, node,
                std::memory_order_release, std::memory_order_relaxed)) {}
        pool_size_.fetch_add(1, std::memory_order_relaxed);
    }

    auto allocate_new() -> pooled_buffer {
        std::size_t alloc_size = sizeof(block_node) + block_size_;
        auto* mem = ::operator new(alloc_size);
        auto* node = static_cast<block_node*>(mem);
        node->next = nullptr;
        void* data = static_cast<char*>(mem) + sizeof(block_node);
        return pooled_buffer{this, data, block_size_};
    }

    std::size_t block_size_;
    std::size_t max_blocks_;
    std::atomic<block_node*> free_head_{nullptr};
    std::atomic<std::size_t> pool_size_{0};
};

// pooled_buffer::release 实现
inline void pooled_buffer::release() {
    if (pool_ && data_) {
        pool_->return_block(data_);
        pool_ = nullptr;
        data_ = nullptr;
    }
}

} // namespace cnetmod
