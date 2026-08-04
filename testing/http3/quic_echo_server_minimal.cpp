module cnetmod.test.quic_echo_server_minimal;

namespace cnetmod::test {

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

quic_echo_server_minimal::quic_echo_server_minimal(
    io_context& ctx,
    ssl_context& ssl_ctx,
    std::uint16_t port)
    : ctx_{ctx}
    , ssl_ctx_{ssl_ctx}
    , port_{port}
    , listen_socket_{std::make_unique<udp::udp_socket>(ctx)}
{
    listen_socket_->bind(endpoint{address_v4::any(), port});
    logger::instance().info(
        "QUIC echo server (minimal) initialized on port {}", port);
}

quic_echo_server_minimal::~quic_echo_server_minimal() {
    if (running_) {
        // 析构时同步等待优雅停止
        stop().sync_wait();
    }
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

auto quic_echo_server_minimal::start()
    -> task<std::expected<bool, std::error_code>>
{
    if (running_) {
        co_return std::unexpected(
            make_error_code(std::errc::already_connected));
    }

    running_ = true;
    logger::instance().info(
        "QUIC echo server (minimal) started on port {}", port_);

    // 将 accept_loop 作为后台协程派生
    ctx_.spawn([this]() -> task<void> {
        co_await accept_loop();
    });

    co_return true;
}

auto quic_echo_server_minimal::stop() -> task<void> {
    running_ = false;
    logger::instance().info("QUIC echo server (minimal) stopping...");

    // 关闭所有活跃连接
    {
        std::lock_guard lock{connections_mutex_};
        for (auto& [conn_id, conn] : connections_) {
            co_await conn->async_close();
        }
        connections_.clear();
    }

    // 关闭监听套接字
    listen_socket_->close();

    active_connections_ = 0;
    logger::instance().info("QUIC echo server (minimal) stopped");
}

auto quic_echo_server_minimal::active_connections() const noexcept
    -> std::size_t
{
    return active_connections_.load();
}

auto quic_echo_server_minimal::is_running() const noexcept -> bool {
    return running_;
}

// ---------------------------------------------------------------------------
// Accept loop
// ---------------------------------------------------------------------------

auto quic_echo_server_minimal::accept_loop() -> task<void> {
    std::array<std::byte, 65536> recv_buffer;

    while (running_) {
        // 等待入站 UDP 数据报
        auto [bytes_received, peer] = co_await listen_socket_->async_recvfrom(
            std::span{recv_buffer}
        );

        if (!bytes_received) {
            logger::instance().debug(
                "Recv failed: {}", bytes_received.error().message());
            continue;
        }

        auto pkt_span = std::span<const std::byte>{
            recv_buffer.begin(),
            recv_buffer.begin() + bytes_received.value()
        };

        // 解析 QUIC 包头
        auto header_result = quic_packet::parse_header(pkt_span);

        if (!header_result) {
            // 非法包 —— 静默丢弃，不崩溃
            logger::instance().debug("Invalid packet header, dropping");
            continue;
        }

        auto& header = header_result.value();

        // Initial 包 → 新连接尝试
        if (header.packet_type == packet_type::initial) {
            logger::instance().info("New connection attempt from {}", peer);

            auto conn = std::make_unique<quic_connection>(
                ctx_, ssl_ctx_, quic_role::server
            );

            auto conn_id = conn->local_cid().value();

            {
                std::lock_guard lock{connections_mutex_};
                connections_.emplace(conn_id, std::move(conn));
            }

            active_connections_++;

            // 获取裸指针用于协程捕获（连接生命周期由 connections_ 管理）
            auto conn_ptr = connections_[conn_id].get();

            ctx_.spawn([this, conn_ptr, peer]() -> task<void> {
                co_await handle_connection(
                    std::unique_ptr<quic_connection>(
                        conn_ptr, [](quic_connection*) {}),  // 不释放，由 map 管理
                    peer
                );
            });
        }

        // 非 Initial 包：应根据 DCID 路由到对应连接
        // （Phase 2 简化实现：仅处理新连接，后续包由连接内部处理）
    }
}

// ---------------------------------------------------------------------------
// Connection handler
// ---------------------------------------------------------------------------

auto quic_echo_server_minimal::handle_connection(
    std::unique_ptr<quic_connection> conn,
    endpoint peer) -> task<void>
{
    logger::instance().info("Handling connection from {}", peer);

    try {
        // 持续接受新流直到连接关闭
        while (conn->is_established() && running_) {
            auto stream = co_await conn->accept_stream();

            if (!stream) {
                logger::instance().warn("Stream accept failed");
                break;
            }

            auto stream_id = stream->id();

            logger::instance().debug(
                "New stream {} from {}", stream_id, peer);

            // 为每个流派生独立的 echo 协程
            ctx_.spawn([this, &conn_ref = *conn, stream_id]() -> task<void> {
                co_await echo_stream_data(conn_ref, stream_id);
            });
        }

    } catch (const std::exception& e) {
        logger::instance().error(
            "Connection handler error ({}): {}", peer, e.what());
    }

    // 从连接表中移除
    {
        std::lock_guard lock{connections_mutex_};
        auto it = std::ranges::find_if(connections_, [&](const auto& pair) {
            return pair.second.get() == conn.get();
        });
        if (it != connections_.end()) {
            connections_.erase(it);
        }
    }

    active_connections_--;
    logger::instance().info("Connection from {} closed", peer);
}

// ---------------------------------------------------------------------------
// Stream echo handler
// ---------------------------------------------------------------------------

auto quic_echo_server_minimal::echo_stream_data(
    quic_connection& conn,
    std::uint64_t stream_id) -> task<void>
{
    std::array<std::byte, 16384> buffer;

    while (running_) {
        // 从流中读取数据
        auto result = co_await conn.async_read_stream(
            stream_id, std::span{buffer}
        );

        if (!result || result.value() == 0) {
            // 流已关闭或读取出错
            break;
        }

        auto bytes_read = result.value();

        logger::instance().debug(
            "Stream {}: received {} bytes", stream_id, bytes_read);

        // 将数据原样回写
        auto data_span = std::span<const std::byte>{
            buffer.begin(),
            buffer.begin() + bytes_read
        };

        auto write_result = co_await conn.async_write_stream(
            stream_id, data_span
        );

        if (!write_result) {
            logger::instance().error(
                "Stream {} echo write failed: {}",
                stream_id, write_result.error().message());
            break;
        }

        logger::instance().debug(
            "Stream {}: echoed {} bytes", stream_id, bytes_read);
    }

    logger::instance().debug("Stream {} handler finished", stream_id);
}

} // namespace cnetmod::test
