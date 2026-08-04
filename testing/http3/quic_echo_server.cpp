module cnetmod.test.quic_echo_server;

namespace cnetmod::test {

quic_echo_server::quic_echo_server(
    io_context& ctx,
    ssl_context& ssl_ctx,
    std::uint16_t port)
    : ctx_{ctx}
    , ssl_ctx_{ssl_ctx}
    , port_{port}
    , listen_socket_{std::make_unique<udp::udp_socket>(ctx)}
{
    // 绑定到指定端口（INADDR_ANY）
    listen_socket_->bind(endpoint{address_v4::any(), port});

    logger::instance().info("QUIC echo server initialized on port {}", port);
}

quic_echo_server::~quic_echo_server() {
    if (running_) {
        // 析构时同步等待优雅停止
        stop().sync_wait();
    }
}

auto quic_echo_server::start() -> task<std::expected<bool, std::error_code>> {
    if (running_) {
        co_return std::unexpected(make_error_code(std::errc::already_connected));
    }

    running_ = true;
    logger::instance().info("QUIC echo server started listening on port {}", port_);

    // 将 accept_loop 作为后台任务派生
    ctx_.spawn([this]() -> task<void> {
        auto result = co_await accept_loop();
        if (!result) {
            logger::instance().error("Accept loop failed: {}", result.error().message());
        }
    });

    co_return true;
}

auto quic_echo_server::stop() -> task<void> {
    running_ = false;

    logger::instance().info("QUIC echo server stopping...");

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

    logger::instance().info("QUIC echo server stopped");
}

auto quic_echo_server::accept_loop() -> task<std::expected<bool, std::error_code>> {
    std::array<std::byte, 65536> recv_buffer;

    while (running_) {
        // 等待入站数据报
        auto [bytes_received, peer] = co_await listen_socket_->async_recvfrom(
            std::span{recv_buffer}
        );

        if (!bytes_received) {
            logger::instance().warn("Recv failed: {}", bytes_received.error().message());
            continue;
        }

        // 解析 QUIC 包头
        auto header_result = quic_packet::parse_header(
            std::span<const std::byte>{
                recv_buffer.begin(),
                recv_buffer.begin() + bytes_received.value()
            }
        );

        if (!header_result) {
            logger::instance().debug("Invalid packet header, dropping");
            continue;
        }

        auto& header = header_result.value();

        // 检查是否为 Initial 包（新连接尝试）
        if (header.packet_type == packet_type::initial) {
            logger::instance().info("New connection attempt from {}", peer);

            // 创建新的 QUIC 连接（服务端角色）
            auto conn = std::make_unique<quic_connection>(
                ctx_, ssl_ctx_, quic_role::server
            );

            auto conn_id = conn->local_cid();
            auto conn_ptr = conn.get();

            {
                std::lock_guard lock{connections_mutex_};
                connections_.emplace(conn_id.value(), std::move(conn));
            }

            active_connections_++;

            // 为该连接派生处理任务
            ctx_.spawn([this, conn_ptr, peer]() -> task<void> {
                co_await handle_connection(conn_ptr, peer);
            });
        }

        // 将数据包转发到现有连接
        // （生产实现中应根据 DCID 查找对应连接）
    }

    co_return true;
}

auto quic_echo_server::handle_connection(
    std::unique_ptr<quic_connection> conn,
    endpoint peer) -> task<void>
{
    logger::instance().info("Handling connection from {}", peer);

    try {
        // 持续接受并处理流
        while (conn->is_established()) {
            auto stream = co_await conn->accept_stream();

            if (!stream) {
                logger::instance().warn("Stream accept failed");
                break;
            }

            // 为每个流派生独立的读写任务
            ctx_.spawn([this, &conn = *conn, stream_id = stream->id()]() -> task<void> {
                std::array<std::byte, 16384> buffer;

                while (true) {
                    // 从流中读取数据
                    auto result = co_await conn.async_read_stream(
                        stream_id, std::span{buffer}
                    );

                    if (!result || result.value() == 0) {
                        break; // 流已关闭
                    }

                    auto bytes_read = result.value();

                    logger::instance().debug(
                        "Received {} bytes on stream {}",
                        bytes_read, stream_id
                    );

                    // 回显数据
                    co_await process_stream_data(
                        conn,
                        stream_id,
                        std::span<const std::byte>{
                            buffer.begin(),
                            buffer.begin() + bytes_read
                        }
                    );
                }
            });
        }

    } catch (const std::exception& e) {
        logger::instance().error("Connection handler error: {}", e.what());
    }

    // 从活跃连接表中移除
    {
        std::lock_guard lock{connections_mutex_};
        connections_.erase(conn->local_cid().value());
    }

    active_connections_--;
    logger::instance().info("Connection from {} closed", peer);
}

auto quic_echo_server::process_stream_data(
    quic_connection& conn,
    std::uint64_t stream_id,
    std::span<const std::byte> data) -> task<void>
{
    logger::instance().debug("Echoing {} bytes back on stream {}", data.size(), stream_id);

    // 将收到的数据原样写回同一流
    auto result = co_await conn.async_write_stream(stream_id, data);

    if (!result) {
        logger::instance().error("Echo failed: {}", result.error().message());
    }
}

} // namespace cnetmod::test
