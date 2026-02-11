module;

#include <cnetmod/config.hpp>

export module cnetmod.io.io_operation;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;

namespace cnetmod {

// =============================================================================
// I/O 操作类型
// =============================================================================

/// I/O 操作类型
export enum class io_op_type {
    // 网络操作
    accept,
    connect,
    read,
    write,
    close,

    // 文件操作
    file_read,
    file_write,
    file_flush,
};

// =============================================================================
// I/O 操作结果
// =============================================================================

/// I/O 操作完成结果
export struct io_result {
    std::error_code error;          // 错误码
    std::size_t bytes_transferred;  // 传输的字节数

    [[nodiscard]] auto success() const noexcept -> bool {
        return !error;
    }

    explicit operator bool() const noexcept {
        return success();
    }
};

// =============================================================================
// I/O 操作基类
// =============================================================================

/// 异步 I/O 操作的抽象基类
/// 平台特定的实现需要继承此类
export class io_operation {
public:
    virtual ~io_operation() = default;

    /// 获取操作类型
    [[nodiscard]] virtual auto type() const noexcept -> io_op_type = 0;

    /// 执行操作（由 io_context 调用）
    virtual void execute() = 0;

    /// 完成操作（由 io_context 在操作完成时调用）
    /// @param result 操作结果
    virtual void complete(io_result result) = 0;

protected:
    io_operation() = default;
};

} // namespace cnetmod
