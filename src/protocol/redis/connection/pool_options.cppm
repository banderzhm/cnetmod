export module cnetmod.protocol.redis:pool_options;

import std;

export namespace cnetmod::redis {
struct pool_params {
  std::string host = "127.0.0.1";
  std::uint16_t port = 6379;
  std::string password;
  std::string username;
  std::uint32_t db = 0;
  bool resp3 = true;
  std::size_t initial_size = 1;
  std::size_t max_size = 16;
  std::chrono::steady_clock::duration connect_timeout =
      std::chrono::seconds(10);
  std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);
  std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);
  std::chrono::steady_clock::duration ping_interval = std::chrono::hours(1);
  std::chrono::steady_clock::duration ping_timeout = std::chrono::seconds(10);
  bool tls = false;
  bool tls_verify = true;
  std::string tls_ca_file;
  std::string tls_cert_file;
  std::string tls_key_file;
  std::string tls_sni;
};
} // namespace cnetmod::redis
