module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.redis:client;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :value;
import :routing;
import :request;
import :parser;

export namespace cnetmod::redis {
struct connect_options {
  std::string host = "127.0.0.1";
  std::uint16_t port = 6379;
  std::string password;
  std::string username;
  std::uint32_t db = 0;
  bool resp3 = true;
  bool tls = false;
  bool tls_verify = true;
  std::string tls_ca_file;
  std::string tls_cert_file;
  std::string tls_key_file;
  std::string tls_sni;
};

using push_callback =
    std::function<void(std::string_view channel, std::string_view message)>;

class client {
public:
  explicit client(io_context &ctx) noexcept;
  auto connect(connect_options opts = {})
      -> task<std::expected<void, std::string>>;
  [[nodiscard]] auto is_open() const noexcept -> bool;
  void close() noexcept;
  auto exec(const request &req)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto cmd(std::initializer_list<std::string_view> args)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto cmd(std::span<const std::string> args)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto cmd_follow_redirect(std::vector<std::string> args,
                           std::size_t max_redirects = 3)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto pipe(std::initializer_list<std::initializer_list<std::string_view>> cmds)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto pipe(std::span<const std::vector<std::string>> cmds)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto subscribe(std::initializer_list<std::string_view> channels)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto unsubscribe(std::initializer_list<std::string_view> channels)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto psubscribe(std::initializer_list<std::string_view> patterns)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto punsubscribe(std::initializer_list<std::string_view> patterns)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto sentinel_get_master_addr_by_name(std::string_view master)
      -> task<std::expected<endpoint_info, std::string>>;
  void on_push(push_callback cb);
  auto receive_push()
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  [[nodiscard]] auto is_resp3() const noexcept -> bool;
  [[nodiscard]] static auto key_slot(std::string_view key) noexcept
      -> std::uint16_t;
  [[nodiscard]] static auto parse_redirect(const std::vector<resp3_node> &nodes)
      -> std::optional<cluster_redirect>;
  [[nodiscard]] static auto
  parse_cluster_slots(const std::vector<resp3_node> &nodes)
      -> std::expected<std::vector<cluster_slot_range>, std::string>;

private:
  auto do_write(const_buffer buf)
      -> task<std::expected<std::size_t, std::error_code>>;
  auto do_read(mutable_buffer buf)
      -> task<std::expected<std::size_t, std::error_code>>;
  auto do_auth(const connect_options &opts)
      -> task<std::expected<void, std::string>>;
  auto subscription_command(std::string_view command,
                            std::initializer_list<std::string_view> names)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto parse_one_response()
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto parse_children(std::size_t count)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto fill() -> task<bool>;
  void compact_buffer();
  io_context &ctx_;
  socket sock_;
  connect_options opts_;
  std::string rbuf_;
  std::size_t rpos_ = 0;
  bool resp3_mode_ = false;
  push_callback push_cb_;
#ifdef CNETMOD_HAS_SSL
  std::unique_ptr<ssl_context> ssl_ctx_;
  std::unique_ptr<ssl_stream> ssl_;
#endif
};

class cluster_client {
public:
  explicit cluster_client(io_context &ctx) noexcept;
  auto connect(connect_options seed) -> task<std::expected<void, std::string>>;
  auto refresh_slots() -> task<std::expected<void, std::string>>;
  auto cmd_for_key(std::vector<std::string> args, std::string_view key,
                   std::size_t max_redirects = 3)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  auto pipeline(std::span<const cluster_pipeline_item> items)
      -> task<std::expected<std::vector<resp3_node>, std::string>>;
  [[nodiscard]] auto slots() const noexcept -> const cluster_slot_cache &;

private:
  static auto endpoint_key(const endpoint_info &ep) -> std::string;
  auto connection_for(const endpoint_info &ep) -> task<client *>;
  io_context &ctx_;
  connect_options seed_options_;
  client seed_;
  cluster_slot_cache slot_cache_;
  std::map<std::string, std::unique_ptr<client>> nodes_;
};
} // namespace cnetmod::redis