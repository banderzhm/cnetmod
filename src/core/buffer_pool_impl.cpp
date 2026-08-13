module cnetmod.core.buffer_pool;

import std;
import cnetmod.core.buffer;

namespace cnetmod {
pooled_buffer::pooled_buffer(buffer_pool* pool, void* data, std::size_t size) noexcept
    : pool_(pool), data_(data), size_(size)
{
}

pooled_buffer::~pooled_buffer()
{
    release();
}

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

auto pooled_buffer::data() noexcept -> void*
{
    return data_;
}

auto pooled_buffer::data() const noexcept -> const void*
{
    return data_;
}

auto pooled_buffer::size() const noexcept -> std::size_t
{
    return size_;
}

auto pooled_buffer::valid() const noexcept -> bool
{
    return data_ != nullptr;
}

pooled_buffer::operator mutable_buffer() noexcept
{
    return {data_, size_};
}

pooled_buffer::operator const_buffer() const noexcept
{
    return {data_, size_};
}

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
    : block_size_(std::max<std::size_t>(block_size, 1U)),
      max_blocks_(std::max<std::size_t>(max_blocks, 2U)),
      free_blocks_(max_blocks_)
{
}

buffer_pool::~buffer_pool()
{
    while (auto node = free_blocks_.try_dequeue())
        ::operator delete(*node);
}

auto buffer_pool::acquire() -> pooled_buffer
{
    if (auto node = free_blocks_.try_dequeue())
        return {this, reinterpret_cast<char*>(*node) + sizeof(block_node), block_size_};
    return allocate_new();
}

auto buffer_pool::pool_size() const noexcept -> std::size_t
{
    return free_blocks_.approximate_size();
}

auto buffer_pool::block_size() const noexcept -> std::size_t
{
    return block_size_;
}

void buffer_pool::return_block(void* data) noexcept
{
    auto* node = reinterpret_cast<block_node*>(static_cast<char*>(data) - sizeof(block_node));
    if (!free_blocks_.try_enqueue(node))
        ::operator delete(node);
}

auto buffer_pool::allocate_new() -> pooled_buffer
{
    auto* mem = ::operator new(sizeof(block_node) + block_size_);
    return {this, static_cast<char*>(mem) + sizeof(block_node), block_size_};
}
} // namespace cnetmod
