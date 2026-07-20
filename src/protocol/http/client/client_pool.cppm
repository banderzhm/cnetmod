export module cnetmod.protocol.http.client.pool;

import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;

export namespace cnetmod::http {
/// Per-endpoint HTTP client reuse pool.  Connections remain owned by clients;
/// HTTP/2 clients multiplex streams while HTTP/1 clients are leased
/// exclusively.
struct client_pool_key {
  std::string host;
  std::uint16_t port{};
  bool tls{};

  [[nodiscard]] auto operator==(const client_pool_key &) const noexcept
      -> bool = default;
  [[nodiscard]] auto
  operator<=>(const client_pool_key &) const noexcept = default;
};

class client_pool {
public:
  client_pool(cnetmod::io_context &context, client_options options = {},
              std::size_t max_idle = 64);

  [[nodiscard]] auto acquire() -> std::unique_ptr<client>;

  void release(std::unique_ptr<client> value);

  [[nodiscard]] auto acquire(client_pool_key key) -> std::unique_ptr<client>;

  void release(client_pool_key key, std::unique_ptr<client> value);

  void clear() noexcept;

  [[nodiscard]] auto idle_count() const noexcept -> std::size_t;

private:
  cnetmod::io_context *context_{};
  client_options options_;
  std::size_t max_idle_{};
  std::vector<std::unique_ptr<client>> idle_;
  std::map<client_pool_key, std::vector<std::unique_ptr<client>>>
      endpoint_idle_;
};
} // namespace cnetmod::http