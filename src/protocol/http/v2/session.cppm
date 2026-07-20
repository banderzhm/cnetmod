export module cnetmod.protocol.http.v2.session;

import std;
import cnetmod.core.socket;
import cnetmod.core.buffer;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.protocol.http.v2.frame;
import cnetmod.protocol.http.v2.settings;
import cnetmod.protocol.http.v2.header_compression;
import cnetmod.protocol.http.v2.stream;

export namespace cnetmod::http::v2 {
struct server_request {
  std::uint32_t stream_id{};
  std::vector<header_field> headers;
  std::vector<std::byte> body;
};

struct server_response {
  std::uint32_t status = 200;
  std::vector<header_field> headers;
  std::vector<header_field> trailers;
  std::vector<std::byte> body;
};

using server_handler =
    std::function<cnetmod::task<server_response>(server_request)>;
using transport_reader =
    std::function<cnetmod::task<std::expected<std::size_t, std::error_code>>(
        cnetmod::mutable_buffer)>;
using transport_writer =
    std::function<cnetmod::task<std::expected<void, std::error_code>>(
        cnetmod::const_buffer)>;

/// RFC 9113 connection state machine.  It is transport-neutral at the frame
/// layer and owns all stream and HPACK state on the connection's io_context.
class session {
public:
  session(cnetmod::io_context &context, cnetmod::socket &socket,
          server_handler handler = {}, transport_reader reader = {},
          transport_writer writer = {});

  [[nodiscard]] auto run(std::span<const std::byte> initial = {})
      -> cnetmod::task<void>;
  [[nodiscard]] auto receive(std::span<const std::byte> bytes)
      -> std::expected<void, std::error_code>;
  [[nodiscard]] auto take_outbound() -> std::vector<std::byte>;
  [[nodiscard]] auto peer_settings() const noexcept -> const settings &;
  [[nodiscard]] auto local_settings() noexcept -> settings &;

private:
  [[nodiscard]] auto process_frame(frame_header header,
                                   std::span<const std::byte> payload)
      -> std::expected<void, std::error_code>;
  [[nodiscard]] auto
  validate_headers(std::span<const header_field> fields) const
      -> std::error_code;
  [[nodiscard]] auto dispatch_ready() -> cnetmod::task<void>;
  void queue_connection_error(std::uint32_t code);
  void queue_frame(frame_header header, std::span<const std::byte> payload);

  cnetmod::io_context *context_{};
  cnetmod::socket *socket_{};
  transport_reader reader_;
  transport_writer writer_;
  settings local_{};
  settings peer_{};
  header_compression decoder_{};
  header_compression encoder_{};
  std::unordered_map<std::uint32_t, stream> streams_;
  std::vector<std::uint32_t> ready_streams_;
  server_handler handler_;
  std::vector<std::byte> input_;
  std::vector<std::byte> outbound_;
  std::vector<std::byte> header_block_;
  std::uint32_t continuation_stream_id_ = 0;
  bool continuation_end_stream_ = false;
  std::uint32_t last_peer_stream_id_ = 0;
  std::int32_t receive_window_ = 65'535;
  std::int32_t send_window_ = 65'535;
  bool received_settings_ = false;
  bool goaway_received_ = false;
  bool received_preface_ = false;
};
} // namespace cnetmod::http::v2
