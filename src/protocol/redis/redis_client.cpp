module cnetmod.protocol.redis;

import std;
import :client;

namespace cnetmod::redis {
client::client(io_context &ctx) noexcept : ctx_(ctx) {}

auto client::connect(connect_options opts)
    -> task<std::expected<void, std::string>> {
  opts_ = opts;
  auto connected =
      co_await async_connect_happy_eyeballs(ctx_, opts.host, opts.port);
  if (!connected)
    co_return std::unexpected(connected.error().message());
  sock_ = std::move(connected->sock);
#ifdef CNETMOD_HAS_SSL
  if (opts.tls) {
    auto context = ssl_context::client();
    if (!context) {
      sock_.close();
      co_return std::unexpected("ssl context: " + context.error().message());
    }
    ssl_ctx_ = std::make_unique<ssl_context>(std::move(*context));
    ssl_ctx_->set_verify_peer(opts.tls_verify);
    if (!opts.tls_ca_file.empty()) {
      auto r = ssl_ctx_->load_ca_file(opts.tls_ca_file);
      if (!r) {
        sock_.close();
        co_return std::unexpected("ssl ca: " + r.error().message());
      }
    } else if (opts.tls_verify)
      (void)ssl_ctx_->set_default_ca();
    if (!opts.tls_cert_file.empty()) {
      auto r = ssl_ctx_->load_cert_file(opts.tls_cert_file);
      if (!r) {
        sock_.close();
        co_return std::unexpected("ssl cert: " + r.error().message());
      }
    }
    if (!opts.tls_key_file.empty()) {
      auto r = ssl_ctx_->load_key_file(opts.tls_key_file);
      if (!r) {
        sock_.close();
        co_return std::unexpected("ssl key: " + r.error().message());
      }
    }
    ssl_ = std::make_unique<ssl_stream>(*ssl_ctx_, ctx_, sock_);
    ssl_->set_connect_state();
    ssl_->set_hostname(opts.tls_sni.empty() ? opts.host : opts.tls_sni);
    auto handshake = co_await ssl_->async_handshake();
    if (!handshake) {
      sock_.close();
      co_return std::unexpected("ssl handshake: " +
                                handshake.error().message());
    }
  }
#else
  if (opts.tls) {
    sock_.close();
    co_return std::unexpected(std::string("SSL not available"));
  }
#endif
  if (opts.resp3) {
    request hello;
    if (!opts.password.empty())
      hello.push("HELLO", "3", "AUTH",
                 opts.username.empty() ? "default" : opts.username,
                 opts.password);
    else
      hello.push("HELLO", "3");
    auto response = co_await exec(hello);
    if (!response)
      co_return std::unexpected("HELLO 3 failed: " + response.error());
    resp3_mode_ = response->empty() || !response->front().is_error();
    if (!resp3_mode_ && !opts.password.empty()) {
      auto auth = co_await do_auth(opts);
      if (!auth)
        co_return auth;
    }
  } else {
    resp3_mode_ = false;
    if (!opts.password.empty()) {
      auto auth = co_await do_auth(opts);
      if (!auth)
        co_return auth;
    }
  }
  if (opts.db > 0) {
    request select;
    select.push("SELECT", std::to_string(opts.db));
    auto response = co_await exec(select);
    if (!response)
      co_return std::unexpected("SELECT failed: " + response.error());
    if (!response->empty() && response->front().is_error())
      co_return std::unexpected("SELECT error: " + response->front().value);
  }
  co_return std::expected<void, std::string>{};
}

auto client::is_open() const noexcept -> bool { return sock_.is_open(); }

void client::close() noexcept {
#ifdef CNETMOD_HAS_SSL
  ssl_.reset();
  ssl_ctx_.reset();
#endif
  sock_.close();
}

auto client::exec(const request &request)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  auto payload = request.payload();
  auto written = co_await do_write({payload.data(), payload.size()});
  if (!written)
    co_return std::unexpected(written.error().message());
  std::vector<resp3_node> result;
  for (std::size_t index = 0; index < request.size(); ++index) {
    auto nodes = co_await parse_one_response();
    if (!nodes)
      co_return std::unexpected(nodes.error());
    for (auto &node : *nodes)
      result.push_back(std::move(node));
  }
  co_return result;
}

auto client::cmd(std::initializer_list<std::string_view> args)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  if (args.size() == 0)
    co_return std::unexpected(std::string("empty command"));
  std::string payload;
  detail::add_header(payload, resp3_type::array, args.size());
  for (auto arg : args)
    detail::add_bulk(payload, arg);
  auto written = co_await do_write({payload.data(), payload.size()});
  if (!written)
    co_return std::unexpected(written.error().message());
  co_return co_await parse_one_response();
}

auto client::cmd(std::span<const std::string> args)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  if (args.empty())
    co_return std::unexpected(std::string("empty command"));
  std::string payload;
  detail::add_header(payload, resp3_type::array, args.size());
  for (auto const &arg : args)
    detail::add_bulk(payload, arg);
  auto written = co_await do_write({payload.data(), payload.size()});
  if (!written)
    co_return std::unexpected(written.error().message());
  co_return co_await parse_one_response();
}

auto client::cmd_follow_redirect(std::vector<std::string> args,
                                 std::size_t limit)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  for (std::size_t attempt = 0; attempt <= limit; ++attempt) {
    auto response =
        co_await cmd(std::span<const std::string>{args.data(), args.size()});
    if (!response)
      co_return response;
    auto redirect = parse_redirect(*response);
    if (!redirect)
      co_return response;
    if (attempt == limit)
      co_return std::unexpected(
          std::string("redis cluster redirect limit exceeded"));
    auto next = opts_;
    next.host = redirect->endpoint.host;
    next.port = redirect->endpoint.port;
    close();
    auto connection = co_await connect(next);
    if (!connection)
      co_return std::unexpected(connection.error());
    if (redirect->kind == redirect_kind::ask) {
      auto asking = co_await cmd({"ASKING"});
      if (!asking)
        co_return asking;
      if (has_error(*asking))
        co_return std::unexpected(std::string(error_message(*asking)));
    }
  }
  co_return std::unexpected(std::string("redis cluster redirect failed"));
}

auto client::pipe(
    std::initializer_list<std::initializer_list<std::string_view>> commands)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  std::string batch;
  for (auto const &command : commands) {
    detail::add_header(batch, resp3_type::array, command.size());
    for (auto arg : command)
      detail::add_bulk(batch, arg);
  }
  auto written = co_await do_write({batch.data(), batch.size()});
  if (!written)
    co_return std::unexpected(written.error().message());
  std::vector<resp3_node> result;
  for (std::size_t i = 0; i < commands.size(); ++i) {
    auto nodes = co_await parse_one_response();
    if (!nodes)
      co_return std::unexpected(nodes.error());
    for (auto &node : *nodes)
      result.push_back(std::move(node));
  }
  co_return result;
}

auto client::pipe(std::span<const std::vector<std::string>> commands)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  std::string batch;
  for (auto const &command : commands) {
    if (command.empty())
      co_return std::unexpected(std::string("empty command in pipeline"));
    detail::add_header(batch, resp3_type::array, command.size());
    for (auto const &arg : command)
      detail::add_bulk(batch, arg);
  }
  auto written = co_await do_write({batch.data(), batch.size()});
  if (!written)
    co_return std::unexpected(written.error().message());
  std::vector<resp3_node> result;
  for (std::size_t i = 0; i < commands.size(); ++i) {
    auto nodes = co_await parse_one_response();
    if (!nodes)
      co_return std::unexpected(nodes.error());
    for (auto &node : *nodes)
      result.push_back(std::move(node));
  }
  co_return result;
}

auto client::subscribe(std::initializer_list<std::string_view> names)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  co_return co_await subscription_command("SUBSCRIBE", names);
}

auto client::unsubscribe(std::initializer_list<std::string_view> names)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  co_return co_await subscription_command("UNSUBSCRIBE", names);
}

auto client::psubscribe(std::initializer_list<std::string_view> names)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  co_return co_await subscription_command("PSUBSCRIBE", names);
}

auto client::punsubscribe(std::initializer_list<std::string_view> names)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  co_return co_await subscription_command("PUNSUBSCRIBE", names);
}

auto client::sentinel_get_master_addr_by_name(std::string_view master)
    -> task<std::expected<endpoint_info, std::string>> {
  auto response = co_await cmd({"SENTINEL", "GET-MASTER-ADDR-BY-NAME", master});
  if (!response)
    co_return std::unexpected(response.error());
  if (has_error(*response))
    co_return std::unexpected(std::string(error_message(*response)));
  auto values = all_values(*response);
  if (values.size() < 2)
    co_return std::unexpected(
        std::string("sentinel returned no master address"));
  std::uint16_t port{};
  auto [_, error] = std::from_chars(values[1].data(),
                                    values[1].data() + values[1].size(), port);
  if (error != std::errc{})
    co_return std::unexpected(
        std::string("sentinel returned invalid master port"));
  co_return endpoint_info{.host = std::string(values[0]), .port = port};
}

void client::on_push(push_callback callback) { push_cb_ = std::move(callback); }

auto client::receive_push()
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  co_return co_await parse_one_response();
}

auto client::is_resp3() const noexcept -> bool { return resp3_mode_; }

auto client::do_write(const_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>> {
#ifdef CNETMOD_HAS_SSL
  if (ssl_) {
    auto result = co_await ssl_->async_write_all(buffer);
    if (!result)
      co_return std::unexpected(result.error());
    co_return buffer.size;
  }
#endif
  auto result = co_await async_write_all(ctx_, sock_, buffer);
  if (!result)
    co_return std::unexpected(result.error());
  co_return buffer.size;
}

auto client::do_read(mutable_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>> {
#ifdef CNETMOD_HAS_SSL
  if (ssl_)
    co_return co_await ssl_->async_read(buffer);
#endif
  co_return co_await async_read(ctx_, sock_, buffer);
}

auto client::do_auth(const connect_options &options)
    -> task<std::expected<void, std::string>> {
  request auth;
  if (options.username.empty())
    auth.push("AUTH", options.password);
  else
    auth.push("AUTH", options.username, options.password);
  auto response = co_await exec(auth);
  if (!response)
    co_return std::unexpected("AUTH failed: " + response.error());
  if (!response->empty() && response->front().is_error())
    co_return std::unexpected("AUTH error: " + response->front().value);
  co_return std::expected<void, std::string>{};
}

auto client::subscription_command(std::string_view command,
                                  std::initializer_list<std::string_view> names)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  std::string payload;
  detail::add_header(payload, resp3_type::array, names.size() + 1);
  detail::add_bulk(payload, command);
  for (auto name : names)
    detail::add_bulk(payload, name);
  auto written = co_await do_write({payload.data(), payload.size()});
  if (!written)
    co_return std::unexpected(written.error().message());
  std::vector<resp3_node> result;
  for (std::size_t i = 0; i < names.size(); ++i) {
    auto nodes = co_await parse_one_response();
    if (!nodes)
      co_return std::unexpected(nodes.error());
    for (auto &node : *nodes)
      result.push_back(std::move(node));
  }
  co_return result;
}

auto client::parse_one_response()
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  std::vector<resp3_node> nodes;
  resp3_parser parser;
  std::error_code error;
  while (true) {
    if (rpos_ >= rbuf_.size() || rbuf_.empty()) {
      if (!(co_await fill()))
        co_return std::unexpected(std::string("connection closed"));
    }
    auto view = std::string_view(rbuf_).substr(rpos_);
    auto node = parser.consume(view, error);
    if (error)
      co_return std::unexpected(error.message());
    if (node) {
      nodes.push_back(std::move(*node));
      rpos_ += parser.consumed();
      parser.reset();
      auto &first = nodes.front();
      if (first.is_aggregate() && first.aggregate_size > 0) {
        auto children = co_await parse_children(
            first.aggregate_size * element_multiplicity(first.data_type));
        if (!children)
          co_return std::unexpected(children.error());
        for (auto &child : *children)
          nodes.push_back(std::move(child));
      }
      compact_buffer();
      co_return nodes;
    }
    rpos_ += parser.consumed();
    parser.reset();
    if (!(co_await fill()))
      co_return std::unexpected(std::string("connection closed"));
  }
}

auto client::parse_children(std::size_t count)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  std::vector<resp3_node> children;
  children.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    resp3_parser parser;
    std::error_code error;
    while (true) {
      auto view = std::string_view(rbuf_).substr(rpos_);
      if (view.empty()) {
        if (!(co_await fill()))
          co_return std::unexpected(std::string("connection closed"));
        continue;
      }
      auto node = parser.consume(view, error);
      if (error)
        co_return std::unexpected(error.message());
      if (!node) {
        rpos_ += parser.consumed();
        parser.reset();
        if (!(co_await fill()))
          co_return std::unexpected(std::string("connection closed"));
        continue;
      }
      rpos_ += parser.consumed();
      children.push_back(std::move(*node));
      auto &child = children.back();
      if (child.is_aggregate() && child.aggregate_size > 0) {
        auto sub = co_await parse_children(
            child.aggregate_size * element_multiplicity(child.data_type));
        if (!sub)
          co_return std::unexpected(sub.error());
        for (auto &item : *sub)
          children.push_back(std::move(item));
        i += sub->size();
      }
      break;
    }
  }
  co_return children;
}

auto client::fill() -> task<bool> {
  std::array<std::byte, 8192> storage{};
  auto read = co_await do_read({storage.data(), storage.size()});
  if (!read || *read == 0)
    co_return false;
  rbuf_.append(reinterpret_cast<const char *>(storage.data()), *read);
  co_return true;
}

void client::compact_buffer() {
  if (rpos_ > 4096) {
    rbuf_.erase(0, rpos_);
    rpos_ = 0;
  }
}

cluster_client::cluster_client(io_context &context) noexcept
    : ctx_(context), seed_(context) {}

auto cluster_client::connect(connect_options seed)
    -> task<std::expected<void, std::string>> {
  seed_options_ = std::move(seed);
  auto connected = co_await seed_.connect(seed_options_);
  if (!connected)
    co_return connected;
  co_return co_await refresh_slots();
}

auto cluster_client::refresh_slots() -> task<std::expected<void, std::string>> {
  auto response = co_await seed_.cmd({"CLUSTER", "SLOTS"});
  if (!response)
    co_return std::unexpected(response.error());
  auto ranges = client::parse_cluster_slots(*response);
  if (!ranges)
    co_return std::unexpected(ranges.error());
  slot_cache_.update(*ranges);
  co_return std::expected<void, std::string>{};
}

auto cluster_client::cmd_for_key(std::vector<std::string> args,
                                 std::string_view key, std::size_t limit)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  if (args.empty())
    co_return std::unexpected(std::string("empty command"));
  for (std::size_t attempt = 0; attempt <= limit; ++attempt) {
    auto endpoint = slot_cache_.endpoint_for_slot(client::key_slot(key));
    if (!endpoint) {
      auto refreshed = co_await refresh_slots();
      if (!refreshed)
        co_return std::unexpected(refreshed.error());
      endpoint = slot_cache_.endpoint_for_slot(client::key_slot(key));
    }
    if (!endpoint)
      co_return std::unexpected(std::string("redis cluster slot not covered"));
    auto *connection = co_await connection_for(*endpoint);
    if (!connection)
      co_return std::unexpected(
          std::string("redis cluster node connect failed"));
    auto response = co_await connection->cmd(
        std::span<const std::string>{args.data(), args.size()});
    if (!response)
      co_return response;
    auto redirect = client::parse_redirect(*response);
    if (!redirect)
      co_return response;
    if (attempt == limit)
      co_return std::unexpected(
          std::string("redis cluster redirect limit exceeded"));
    if (redirect->kind == redirect_kind::moved)
      slot_cache_.update_slot(redirect->slot, redirect->endpoint);
    auto *next = co_await connection_for(redirect->endpoint);
    if (!next)
      co_return std::unexpected(
          std::string("redis cluster redirect connect failed"));
    if (redirect->kind == redirect_kind::ask) {
      auto asking = co_await next->cmd({"ASKING"});
      if (!asking)
        co_return asking;
      if (has_error(*asking))
        co_return std::unexpected(std::string(error_message(*asking)));
    }
  }
  co_return std::unexpected(std::string("redis cluster command failed"));
}

auto cluster_client::pipeline(std::span<const cluster_pipeline_item> items)
    -> task<std::expected<std::vector<resp3_node>, std::string>> {
  std::map<std::string, std::vector<std::vector<std::string>>> grouped;
  std::map<std::string, endpoint_info> endpoints;
  for (auto const &item : items) {
    if (item.args.empty())
      co_return std::unexpected(
          std::string("empty command in cluster pipeline"));
    auto endpoint = slot_cache_.endpoint_for_slot(client::key_slot(item.key));
    if (!endpoint) {
      auto refreshed = co_await refresh_slots();
      if (!refreshed)
        co_return std::unexpected(refreshed.error());
      endpoint = slot_cache_.endpoint_for_slot(client::key_slot(item.key));
    }
    if (!endpoint)
      co_return std::unexpected(std::string("redis cluster slot not covered"));
    auto key = endpoint_key(*endpoint);
    endpoints[key] = *endpoint;
    grouped[key].push_back(item.args);
  }
  std::vector<resp3_node> result;
  for (auto &[key, commands] : grouped) {
    auto *connection = co_await connection_for(endpoints[key]);
    if (!connection)
      co_return std::unexpected(
          std::string("redis cluster node connect failed"));
    auto response =
        co_await connection->pipe(std::span<const std::vector<std::string>>{
            commands.data(), commands.size()});
    if (!response)
      co_return std::unexpected(response.error());
    for (auto &node : *response)
      result.push_back(std::move(node));
  }
  co_return result;
}

auto cluster_client::slots() const noexcept -> const cluster_slot_cache & {
  return slot_cache_;
}

auto cluster_client::endpoint_key(const endpoint_info &endpoint)
    -> std::string {
  auto ipv6 = endpoint.host.find(':') != std::string::npos &&
              !(endpoint.host.starts_with("[") && endpoint.host.ends_with("]"));
  return (ipv6 ? "[" + endpoint.host + "]" : endpoint.host) + ":" +
         std::to_string(endpoint.port);
}

auto cluster_client::connection_for(const endpoint_info &endpoint)
    -> task<client *> {
  auto key = endpoint_key(endpoint);
  auto existing = nodes_.find(key);
  if (existing != nodes_.end() && existing->second->is_open())
    co_return existing->second.get();
  auto options = seed_options_;
  options.host = endpoint.host;
  options.port = endpoint.port;
  auto connection = std::make_unique<client>(ctx_);
  auto connected = co_await connection->connect(options);
  if (!connected)
    co_return nullptr;
  auto *result = connection.get();
  nodes_[key] = std::move(connection);
  co_return result;
}

namespace detail {
constexpr std::array<std::uint16_t, 256> crc16_table = [] {
  std::array<std::uint16_t, 256> table{};
  for (std::uint16_t index{}; index < 256; ++index) {
    auto crc = static_cast<std::uint16_t>(index << 8);
    for (int bit{}; bit < 8; ++bit)
      crc = static_cast<std::uint16_t>((crc & 0x8000) ? ((crc << 1) ^ 0x1021)
                                                      : (crc << 1));
    table[index] = crc;
  }
  return table;
}();

auto crc16(std::string_view value) noexcept -> std::uint16_t {
  std::uint16_t crc{};
  for (unsigned char character : value)
    crc = static_cast<std::uint16_t>(
        (crc << 8) ^ crc16_table[((crc >> 8) ^ character) & 0xFF]);
  return crc;
}

auto cluster_hash_key(std::string_view key) noexcept -> std::string_view {
  auto open = key.find('{');
  if (open == std::string_view::npos)
    return key;
  auto close = key.find('}', open + 1);
  return close == std::string_view::npos || close == open + 1
             ? key
             : key.substr(open + 1, close - open - 1);
}

auto parse_u16(std::string_view text) -> std::optional<std::uint16_t> {
  std::uint16_t value{};
  auto [_, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} ? std::optional{value} : std::nullopt;
}

auto parse_cluster_endpoint(const std::vector<resp3_node> &nodes,
                            std::size_t &index)
    -> std::expected<endpoint_info, std::string> {
  if (index >= nodes.size() || !nodes[index].is_aggregate())
    return std::unexpected(
        std::string("cluster slots endpoint is not an array"));
  auto fields = nodes[index++].aggregate_size;
  if (fields < 2 || index + 1 >= nodes.size())
    return std::unexpected(std::string("cluster slots endpoint is incomplete"));
  auto host = nodes[index++].value;
  auto port = parse_u16(nodes[index++].value);
  if (host.empty() || !port)
    return std::unexpected(
        std::string("cluster slots endpoint host/port invalid"));
  for (std::size_t skipped = 2; skipped < fields && index < nodes.size();
       ++skipped)
    ++index;
  return endpoint_info{.host = std::string(host), .port = *port};
}
} // namespace detail

auto client::key_slot(std::string_view key) noexcept -> std::uint16_t {
  return static_cast<std::uint16_t>(
      detail::crc16(detail::cluster_hash_key(key)) % 16384);
}

auto client::parse_redirect(const std::vector<resp3_node> &nodes)
    -> std::optional<cluster_redirect> {
  auto message = error_message(nodes);
  if (message.empty())
    return std::nullopt;
  redirect_kind kind;
  std::string_view rest;
  if (message.starts_with("MOVED ")) {
    kind = redirect_kind::moved;
    rest = message.substr(6);
  } else if (message.starts_with("ASK ")) {
    kind = redirect_kind::ask;
    rest = message.substr(4);
  } else
    return std::nullopt;
  auto space = rest.find(' ');
  if (space == std::string_view::npos)
    return std::nullopt;
  std::uint16_t slot{};
  auto slot_text = rest.substr(0, space);
  auto [_, slot_error] = std::from_chars(
      slot_text.data(), slot_text.data() + slot_text.size(), slot);
  if (slot_error != std::errc{})
    return std::nullopt;
  auto address = rest.substr(space + 1);
  std::string_view host, port_text;
  if (address.starts_with('[')) {
    auto bracket = address.find(']');
    if (bracket == std::string_view::npos || bracket + 1 >= address.size() ||
        address[bracket + 1] != ':')
      return std::nullopt;
    host = address.substr(1, bracket - 1);
    port_text = address.substr(bracket + 2);
  } else {
    auto colon = address.rfind(':');
    if (colon == std::string_view::npos)
      return std::nullopt;
    host = address.substr(0, colon);
    port_text = address.substr(colon + 1);
  }
  auto port = detail::parse_u16(port_text);
  if (host.empty() || !port)
    return std::nullopt;
  return cluster_redirect{
      .kind = kind,
      .slot = slot,
      .endpoint = {.host = std::string(host), .port = *port}};
}

auto client::parse_cluster_slots(const std::vector<resp3_node> &nodes)
    -> std::expected<std::vector<cluster_slot_range>, std::string> {
  if (nodes.empty() || !nodes.front().is_aggregate())
    return std::unexpected(
        std::string("CLUSTER SLOTS response is not an array"));
  std::vector<cluster_slot_range> ranges;
  ranges.reserve(nodes.front().aggregate_size);
  std::size_t index = 1;
  for (std::size_t range_index{}; range_index < nodes.front().aggregate_size;
       ++range_index) {
    if (index >= nodes.size() || !nodes[index].is_aggregate())
      return std::unexpected(
          std::string("CLUSTER SLOTS range is not an array"));
    auto fields = nodes[index++].aggregate_size;
    if (fields < 3 || index + 2 >= nodes.size())
      return std::unexpected(std::string("CLUSTER SLOTS range is incomplete"));
    auto start = detail::parse_u16(nodes[index++].value);
    auto end = detail::parse_u16(nodes[index++].value);
    if (!start || !end || *start > *end || *end > 16383)
      return std::unexpected(std::string("CLUSTER SLOTS range bounds invalid"));
    auto master = detail::parse_cluster_endpoint(nodes, index);
    if (!master)
      return std::unexpected(master.error());
    cluster_slot_range range{
        .start = *start, .end = *end, .master = *master, .replicas = {}};
    for (std::size_t field = 3; field < fields && index < nodes.size();
         ++field) {
      auto replica = detail::parse_cluster_endpoint(nodes, index);
      if (!replica)
        return std::unexpected(replica.error());
      range.replicas.push_back(*replica);
    }
    ranges.push_back(std::move(range));
  }
  return ranges;
}
} // namespace cnetmod::redis
