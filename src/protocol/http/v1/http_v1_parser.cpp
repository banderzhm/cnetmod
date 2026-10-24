module cnetmod.protocol.http;

import std;
import :parser;
import cnetmod.protocol.http.semantics;

namespace cnetmod::http {
namespace detail {
    constexpr auto ascii_lower(unsigned char value) noexcept -> unsigned char
    {
        return value >= 'A' && value <= 'Z'
            ? static_cast<unsigned char>(value + ('a' - 'A'))
            : value;
    }

    constexpr auto header_hash(std::string_view value) noexcept -> std::uint64_t
    {
        std::uint64_t result = 1469598103934665603ULL;
        for (const auto character : value)
        {
            result ^= ascii_lower(static_cast<unsigned char>(character));
            result *= 1099511628211ULL;
        }
        return result;
    }

    auto header_equal(std::string_view left, std::string_view right) noexcept
        -> bool
    {
        if (left.size() != right.size())
            return false;
        // Clients almost always send canonical-case names.
        if (std::memcmp(left.data(), right.data(), left.size()) == 0)
            return true;
        for (std::size_t index{}; index < left.size(); ++index)
            if (ascii_lower(static_cast<unsigned char>(left[index])) !=
                ascii_lower(static_cast<unsigned char>(right[index])))
                return false;
        return true;
    }

    auto common_header_slot(std::string_view name) noexcept
        -> std::optional<std::size_t>
    {
        // Length dispatch avoids hashing the hot fixed-name lookups (notably
        // Connection, Content-Length and Authorization). Hashing remains only
        // for extension headers in the open-addressed fallback table.
        switch (name.size())
        {
        case 4:
            if (header_equal(name, "Host"))
                return 0;
            break;
        case 5:
            if (header_equal(name, "Range"))
                return 7;
            break;
        case 6:
            if (header_equal(name, "Accept"))
                return 17;
            if (header_equal(name, "Origin"))
                return 8;
            if (header_equal(name, "Cookie"))
                return 15;
            break;
        case 7:
            if (header_equal(name, "Upgrade"))
                return 16;
            break;
        case 9:
            if (header_equal(name, "Forwarded"))
                return 9;
            if (header_equal(name, "X-Real-IP"))
                return 11;
            break;
        case 10:
            if (header_equal(name, "Connection"))
                return 1;
            if (header_equal(name, "Tracestate"))
                return 13;
            if (header_equal(name, "User-Agent"))
                return 14;
            break;
        case 11:
            if (header_equal(name, "Traceparent"))
                return 12;
            break;
        case 12:
            if (header_equal(name, "Content-Type"))
                return 4;
            break;
        case 13:
            if (header_equal(name, "Authorization"))
                return 5;
            break;
        case 14:
            if (header_equal(name, "Content-Length"))
                return 2;
            break;
        case 15:
            if (header_equal(name, "Accept-Encoding"))
                return 6;
            if (header_equal(name, "X-Forwarded-For"))
                return 10;
            break;
        case 17:
            if (header_equal(name, "Transfer-Encoding"))
                return 3;
            break;
        default:
            break;
        }
        return std::nullopt;
    }

    auto find_crlf(const char* data, std::size_t size) noexcept -> std::size_t
    {
        // memchr is vectorised; a lone CR (not followed by LF) is skipped.
        const char* cursor = data;
        const char* const end = data + size;
        while (cursor < end)
        {
            const auto* cr = static_cast<const char*>(
                std::memchr(cursor, '\r', static_cast<std::size_t>(end - cursor)));
            if (cr == nullptr || cr + 1 >= end)
                return std::string_view::npos;
            if (cr[1] == '\n')
                return static_cast<std::size_t>(cr - data);
            cursor = cr + 1;
        }
        return std::string_view::npos;
    }

    auto trim(std::string_view value) noexcept -> std::string_view
    {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.remove_suffix(1);
        return value;
    }

    auto completed_chunked_message_size(
        std::string_view readable, std::size_t chunk_size_line_end) noexcept
        -> std::optional<std::size_t>
    {
        const auto trailer_start = chunk_size_line_end + 2;
        const auto trailers = readable.substr(trailer_start);

        // RFC 9112: the last chunk is followed by an optional trailer section
        // and a mandatory empty line. Do not report the message complete when
        // only "0\r\n" (or one byte of the final CRLF) has arrived.
        if (trailers.starts_with("\r\n"))
            return trailer_start + 2;

        const auto trailer_end = trailers.find("\r\n\r\n");
        if (trailer_end == std::string_view::npos)
            return std::nullopt;
        return trailer_start + trailer_end + 4;
    }
} // namespace detail

request_parser::request_parser()
    : request_parser(4096) {}

request_parser::request_parser(std::size_t initial_capacity)
{
    buf_.reserve(std::min(initial_capacity, max_header_size));
    header_fields_.reserve(16);
    header_index_.reserve(32);
    common_headers_.fill(missing_header);
}

auto request_parser::ready() const noexcept -> bool
{
    return ready_;
}

auto request_parser::headers_ready() const noexcept -> bool
{
    return headers_ready_;
}

auto request_parser::method() const noexcept -> std::string_view
{
    const auto& storage = headers_preserved_ ? header_storage_ : buf_;
    return {storage.data() + method_offset_, method_size_};
}

auto request_parser::method_enum() const noexcept -> std::optional<http_method>
{
    return string_to_method(method());
}

auto request_parser::uri() const noexcept -> std::string_view
{
    const auto& storage = headers_preserved_ ? header_storage_ : buf_;
    return {storage.data() + uri_offset_, uri_size_};
}

auto request_parser::version() const noexcept -> http_version
{
    return version_;
}

auto request_parser::headers() const -> const header_map&
{
    materialize_headers();
    return headers_;
}

auto request_parser::take_headers() -> header_map
{
    materialize_headers();
    headers_materialized_ = false;
    return std::exchange(headers_, header_map{});
}

auto request_parser::take_header_snapshot() -> request_header_snapshot
{
    if (headers_materialized_)
    {
        headers_materialized_ = false;
        return request_header_snapshot{
            std::exchange(headers_, header_map{})};
    }

    preserve_headers();
    std::vector<request_header_snapshot::field> fields;
    fields.reserve(header_fields_.size());
    for (const auto& item : header_fields_)
        fields.push_back({item.name_offset, item.name_size,
            item.value_offset, item.value_size});
    header_fields_.clear();
    header_index_.clear();
    common_headers_.fill(missing_header);
    headers_preserved_ = false;
    return request_header_snapshot{
        std::exchange(header_storage_, std::string{}), std::move(fields)};
}

auto request_parser::body() const noexcept -> std::string_view
{
    return body_;
}

auto request_parser::take_body_chunk() -> std::string
{
    auto chunk = std::move(body_);
    body_.clear();
    return chunk;
}

auto request_parser::get_header(std::string_view k) const -> std::string_view
{
    const auto index = find_header(k);
    if (index == missing_header)
        return {};
    if (header_fields_[index].duplicate)
    {
        materialize_headers();
        const auto it = headers_.find(k);
        return it == headers_.end() ? std::string_view{}
                                    : std::string_view{it->second};
    }
    return header_value(index);
}

void request_parser::reset() noexcept
{
    buf_.clear();
    header_storage_.clear();
    // Header-only requests alternate ownership of the receive allocation.
    // Return the larger retained block to the input side before parsing the
    // next keep-alive request, avoiding a new allocation per request.
    if (buf_.capacity() < header_storage_.capacity())
        buf_.swap(header_storage_);
    read_pos_ = 0;
    message_start_ = 0;
    reset_message_state();
}

void request_parser::reset_message_state() noexcept
{
    method_offset_ = method_size_ = uri_offset_ = uri_size_ = 0;
    version_ = http_version::http_1_1;
    header_fields_.clear();
    header_index_.clear();
    common_headers_.fill(missing_header);
    headers_.clear();
    headers_materialized_ = false;
    body_.clear();
    state_ = state::request_line;
    header_bytes_ = body_bytes_remaining_ = body_bytes_received_ = 0;
    header_end_ = 0;
    headers_preserved_ = false;
    chunked_ = headers_ready_ = ready_ = false;
}

auto request_parser::has_buffered_data() const noexcept -> bool
{
    return read_pos_ < buf_.size();
}

auto request_parser::reset_for_next_message() noexcept -> bool
{
    // A pipelined batch is parsed in place: the next message starts at the
    // current read position. Moving the unread tail to the front for every
    // request made a batch of N requests cost O(N^2) byte moves; compact only
    // once the consumed prefix is large, so the buffer stays bounded.
    constexpr std::size_t compaction_threshold = 16U * 1024U;
    if (read_pos_ == buf_.size())
    {
        buf_.clear();
        read_pos_ = 0;
    }
    else if (read_pos_ >= compaction_threshold)
    {
        buf_.erase(0, read_pos_);
        read_pos_ = 0;
    }
    message_start_ = read_pos_;
    header_storage_.clear();
    reset_message_state();
    return read_pos_ < buf_.size();
}

auto request_parser::consume_buffered()
    -> std::expected<void, std::error_code>
{
    auto parsed = consume(nullptr, 0);
    if (!parsed)
        return std::unexpected(parsed.error());
    return {};
}

auto request_parser::readable_buffer() const noexcept -> std::string_view
{
    return {buf_.data() + read_pos_, buf_.size() - read_pos_};
}

void request_parser::compact_buffer()
{
    // Header offsets are recorded while fragmented input arrives. Keep the
    // bounded header block stable until finalize_headers() moves it into its
    // request-lifetime backing store.
    if (!headers_ready_)
        return;
    if (read_pos_ == 0)
        return;
    if (read_pos_ == buf_.size())
        buf_.clear();
    else if (read_pos_ >= 8192U)
        buf_.erase(0, read_pos_);
    else
        return;
    read_pos_ = 0;
}

auto request_parser::parse_request_line(std::string_view l) -> std::expected<void, std::error_code>
{
    auto a = l.find(' ');
    if (a == std::string_view::npos)
        return std::unexpected(make_error_code(http_errc::invalid_method));
    const auto* base = buf_.data();
    method_offset_ = static_cast<std::size_t>(l.data() - base);
    method_size_ = a;
    auto r = l.substr(a + 1);
    auto b = r.find(' ');
    if (b == std::string_view::npos)
        return std::unexpected(make_error_code(http_errc::invalid_uri));
    uri_offset_ = static_cast<std::size_t>(r.data() - base);
    uri_size_ = b;
    auto v = string_to_version(r.substr(b + 1));
    if (!v)
        return std::unexpected(make_error_code(http_errc::invalid_version));
    version_ = *v;
    return {};
}

auto request_parser::parse_header_line(std::string_view l) -> std::expected<void, std::error_code>
{
    auto c = l.find(':');
    if (c == std::string_view::npos)
        return std::unexpected(make_error_code(http_errc::invalid_header));
    auto k = detail::trim(l.substr(0, c));
    auto v = detail::trim(l.substr(c + 1));
    if (k.empty())
        return std::unexpected(make_error_code(http_errc::invalid_header));
    const auto* base = buf_.data();
    header_fields_.push_back({
        .name_offset = static_cast<std::size_t>(k.data() - base),
        .name_size = k.size(),
        .value_offset = static_cast<std::size_t>(v.data() - base),
        .value_size = v.size(),
    });
    return {};
}

auto request_parser::header_name(std::size_t index) const noexcept
    -> std::string_view
{
    const auto& field = header_fields_[index];
    const auto& storage = headers_preserved_ ? header_storage_ : buf_;
    return {storage.data() + field.name_offset, field.name_size};
}

auto request_parser::header_value(std::size_t index) const noexcept
    -> std::string_view
{
    const auto& field = header_fields_[index];
    const auto& storage = headers_preserved_ ? header_storage_ : buf_;
    return {storage.data() + field.value_offset, field.value_size};
}

void request_parser::preserve_headers()
{
    if (headers_preserved_ || !headers_ready_)
        return;

    header_storage_.assign(
        buf_.data() + message_start_, header_end_ - message_start_);
    method_offset_ -= message_start_;
    uri_offset_ -= message_start_;
    for (auto& field : header_fields_)
    {
        field.name_offset -= message_start_;
        field.value_offset -= message_start_;
    }
    headers_preserved_ = true;
}

void request_parser::finalize_headers()
{
    // Keep complete requests in the receive buffer. Most requests are already
    // fully buffered, so copying their headers into a second string only adds
    // allocator and memory-bandwidth cost. consume() preserves the header block
    // lazily before an append can invalidate its views.
    header_end_ = read_pos_;

    std::size_t capacity = 8;
    while (capacity < header_fields_.size() * 2)
        capacity *= 2;
    header_index_.clear();

    for (std::size_t index{}; index < header_fields_.size(); ++index)
    {
        const auto name = header_name(index);
        if (const auto common = detail::common_header_slot(name))
        {
            auto& slot = common_headers_[*common];
            if (slot == missing_header)
                slot = index;
            else
                header_fields_[slot].duplicate = true;
            continue;
        }

        // The headers emitted by browsers and common benchmark clients are
        // covered by the fixed slots. Build the fallback hash table only when
        // an extension header actually needs it.
        if (header_index_.empty())
            header_index_.assign(capacity, missing_header);
        auto bucket = static_cast<std::size_t>(detail::header_hash(name)) &
            (header_index_.size() - 1);
        while (header_index_[bucket] != missing_header)
        {
            const auto existing = header_index_[bucket];
            if (detail::header_equal(header_name(existing), name))
            {
                header_fields_[existing].duplicate = true;
                break;
            }
            bucket = (bucket + 1) & (header_index_.size() - 1);
        }
        if (header_index_[bucket] == missing_header)
            header_index_[bucket] = index;
    }
}

auto request_parser::find_header(std::string_view name) const noexcept
    -> std::size_t
{
    if (const auto common = detail::common_header_slot(name))
        return common_headers_[*common];
    if (header_index_.empty())
        return missing_header;
    auto bucket = static_cast<std::size_t>(detail::header_hash(name)) &
        (header_index_.size() - 1);
    for (std::size_t probes{}; probes < header_index_.size(); ++probes)
    {
        const auto index = header_index_[bucket];
        if (index == missing_header)
            return missing_header;
        if (detail::header_equal(header_name(index), name))
            return index;
        bucket = (bucket + 1) & (header_index_.size() - 1);
    }
    return missing_header;
}

void request_parser::materialize_headers() const
{
    if (headers_materialized_)
        return;
    headers_.clear();
    headers_.reserve(header_fields_.size());
    for (std::size_t index{}; index < header_fields_.size(); ++index)
    {
        const auto name = header_name(index);
        const auto value = header_value(index);
        auto found = headers_.find(name);
        if (found == headers_.end())
            headers_.emplace(std::string{name}, std::string{value});
        else
        {
            found->second += ", ";
            found->second += value;
        }
    }
    headers_materialized_ = true;
}

auto request_parser::prepare_body() -> bool
{
    auto t = get_header("Transfer-Encoding");
    if (t.find("chunked") != std::string_view::npos)
    {
        chunked_ = true;
        return true;
    }
    auto c = get_header("Content-Length");
    std::size_t n{};
    auto [p, e] = std::from_chars(c.data(), c.data() + c.size(), n);
    if (!c.empty() && e == std::errc{} && n)
    {
        if (n > max_body_size)
            return false;
        body_bytes_remaining_ = n;
        body_.reserve(n);
        return true;
    }
    return false;
}

auto request_parser::process_chunked_body() -> std::expected<bool, std::error_code>
{
    for (;;)
    {
        const auto readable = readable_buffer();
        auto p = detail::find_crlf(readable.data(), readable.size());
        if (p == std::string_view::npos)
            return false;
        std::size_t n{};
        auto [q, e] = std::from_chars(
            readable.data(), readable.data() + p, n, 16);
        if (e != std::errc{})
            return std::unexpected(make_error_code(http_errc::invalid_chunk));
        if (!n)
        {
            const auto completed =
                detail::completed_chunked_message_size(readable, p);
            if (!completed)
            {
                if (readable.size() > max_header_size)
                    return std::unexpected(
                        make_error_code(http_errc::header_too_large));
                return false;
            }
            read_pos_ += *completed;
            return true;
        }
        auto start = p + 2;
        if (readable.size() < start + n + 2)
            return false;
        body_.append(readable.data() + start, n);
        body_bytes_received_ += n;
        read_pos_ += start + n + 2;
        if (body_bytes_received_ > max_body_size)
            return std::unexpected(make_error_code(http_errc::body_too_large));
    }
}

auto request_parser::consume(const char* data, std::size_t len)
    -> std::expected<std::size_t, std::error_code>
{
    if (ready_)
        return std::size_t{0};
    if (len != 0 && headers_ready_ && !headers_preserved_)
        preserve_headers();
    compact_buffer();
    if (len != 0)
        buf_.append(data, len);
    const std::size_t total_consumed = len;
    while (!ready_)
    {
        switch (state_)
        {
        case state::request_line:
        {
            const auto readable = readable_buffer();
            const auto pos = detail::find_crlf(readable.data(), readable.size());
            if (pos == std::string_view::npos)
            {
                if (readable.size() > max_header_size)
                    return std::unexpected(make_error_code(http_errc::header_too_large));
                return total_consumed;
            }
            auto parsed = parse_request_line(readable.substr(0, pos));
            if (!parsed)
                return std::unexpected(parsed.error());
            header_bytes_ += pos + 2;
            read_pos_ += pos + 2;
            state_ = state::headers;
            break;
        }
        case state::headers:
        {
            const auto readable = readable_buffer();
            const auto pos = detail::find_crlf(readable.data(), readable.size());
            if (pos == std::string_view::npos)
            {
                if (header_bytes_ + readable.size() > max_header_size)
                    return std::unexpected(make_error_code(http_errc::header_too_large));
                return total_consumed;
            }
            if (pos == 0)
            {
                read_pos_ += 2;
                header_bytes_ += 2;
                finalize_headers();
                headers_ready_ = true;
                if (!prepare_body())
                    ready_ = true;
                else
                    state_ = state::body;
                break;
            }
            auto parsed = parse_header_line(readable.substr(0, pos));
            if (!parsed)
                return std::unexpected(parsed.error());
            header_bytes_ += pos + 2;
            read_pos_ += pos + 2;
            break;
        }
        case state::body:
        {
            if (chunked_)
            {
                auto parsed = process_chunked_body();
                if (!parsed)
                    return std::unexpected(parsed.error());
                if (!*parsed)
                    return total_consumed;
            }
            else
            {
                const auto readable = readable_buffer();
                const auto take =
                    std::min(readable.size(), body_bytes_remaining_);
                body_.append(readable.data(), take);
                body_bytes_received_ += take;
                read_pos_ += take;
                body_bytes_remaining_ -= take;
                if (body_bytes_remaining_ != 0)
                    return total_consumed;
            }
            ready_ = true;
            break;
        }
        }
    }
    return total_consumed;
}

auto response_parser::consume(const char* data, std::size_t len)
    -> std::expected<std::size_t, std::error_code>
{
    if (ready_)
        return std::size_t{0};
    compact_buffer();
    buf_.append(data, len);
    const std::size_t total_consumed = len;
    while (!ready_)
    {
        switch (state_)
        {
        case state::status_line:
        {
            const auto readable = readable_buffer();
            const auto pos = detail::find_crlf(readable.data(), readable.size());
            if (pos == std::string_view::npos)
            {
                if (readable.size() > max_header_size)
                    return std::unexpected(make_error_code(http_errc::header_too_large));
                return total_consumed;
            }
            auto parsed = parse_status_line(readable.substr(0, pos));
            if (!parsed)
                return std::unexpected(parsed.error());
            header_bytes_ += pos + 2;
            read_pos_ += pos + 2;
            state_ = state::headers;
            break;
        }
        case state::headers:
        {
            const auto readable = readable_buffer();
            const auto pos = detail::find_crlf(readable.data(), readable.size());
            if (pos == std::string_view::npos)
            {
                if (header_bytes_ + readable.size() > max_header_size)
                    return std::unexpected(make_error_code(http_errc::header_too_large));
                return total_consumed;
            }
            if (pos == 0)
            {
                read_pos_ += 2;
                header_bytes_ += 2;
                if (!prepare_body())
                    ready_ = true;
                else
                    state_ = state::body;
                break;
            }
            auto parsed = parse_header_line(readable.substr(0, pos));
            if (!parsed)
                return std::unexpected(parsed.error());
            header_bytes_ += pos + 2;
            read_pos_ += pos + 2;
            break;
        }
        case state::body:
        {
            if (chunked_)
            {
                auto parsed = process_chunked_body();
                if (!parsed)
                    return std::unexpected(parsed.error());
                if (!*parsed)
                    return total_consumed;
            }
            else
            {
                const auto readable = readable_buffer();
                const auto take =
                    std::min(readable.size(), body_bytes_remaining_);
                body_.append(readable.data(), take);
                read_pos_ += take;
                body_bytes_remaining_ -= take;
                if (body_bytes_remaining_ != 0)
                    return total_consumed;
            }
            ready_ = true;
            break;
        }
        }
    }
    return total_consumed;
}

auto response_parser::ready() const noexcept -> bool
{
    return ready_;
}

auto response_parser::version() const noexcept -> http_version
{
    return version_;
}

auto response_parser::status_code() const noexcept -> int
{
    return status_code_;
}

auto response_parser::status_message() const noexcept -> std::string_view
{
    return status_msg_;
}

auto response_parser::headers() const noexcept -> const header_map&
{
    return headers_;
}

auto response_parser::body() const noexcept -> std::string_view
{
    return body_;
}

auto response_parser::get_header(std::string_view k) const -> std::string_view
{
    const auto it = headers_.find(k);
    return it == headers_.end() ? std::string_view{} : std::string_view{it->second};
}

void response_parser::reset() noexcept
{
    buf_.clear();
    read_pos_ = 0;
    version_ = http_version::http_1_1;
    status_code_ = 0;
    status_msg_.clear();
    headers_.clear();
    body_.clear();
    state_ = state::status_line;
    header_bytes_ = body_bytes_remaining_ = 0;
    chunked_ = ready_ = false;
}

auto response_parser::readable_buffer() const noexcept -> std::string_view
{
    return {buf_.data() + read_pos_, buf_.size() - read_pos_};
}

void response_parser::compact_buffer()
{
    if (read_pos_ == 0)
        return;
    if (read_pos_ == buf_.size())
        buf_.clear();
    else if (read_pos_ >= 8192U)
        buf_.erase(0, read_pos_);
    else
        return;
    read_pos_ = 0;
}

auto response_parser::parse_status_line(std::string_view l) -> std::expected<void, std::error_code>
{
    auto a = l.find(' ');
    if (a == std::string_view::npos)
        return std::unexpected(make_error_code(http_errc::invalid_status_line));
    auto v = string_to_version(l.substr(0, a));
    if (!v)
        return std::unexpected(make_error_code(http_errc::invalid_version));
    version_ = *v;
    auto r = l.substr(a + 1);
    auto b = r.find(' ');
    auto code = b == std::string_view::npos ? r : r.substr(0, b);
    if (b != std::string_view::npos)
        status_msg_ = std::string(r.substr(b + 1));
    auto [p, e] = std::from_chars(code.data(), code.data() + code.size(), status_code_);
    if (e != std::errc{})
        return std::unexpected(make_error_code(http_errc::invalid_status_line));
    return {};
}

auto response_parser::parse_header_line(std::string_view l) -> std::expected<void, std::error_code>
{
    auto c = l.find(':');
    if (c == std::string_view::npos)
        return std::unexpected(make_error_code(http_errc::invalid_header));
    auto k = detail::trim(l.substr(0, c));
    auto v = detail::trim(l.substr(c + 1));
    if (k.empty())
        return std::unexpected(make_error_code(http_errc::invalid_header));
    auto it = headers_.find(k);
    if (it == headers_.end())
        headers_.emplace(std::string(k), std::string(v));
    else
    {
        it->second += ", ";
        it->second += v;
    }
    return {};
}

auto response_parser::prepare_body() -> bool
{
    auto t = get_header("Transfer-Encoding");
    if (t.find("chunked") != std::string_view::npos)
    {
        chunked_ = true;
        return true;
    }
    auto c = get_header("Content-Length");
    std::size_t n{};
    auto [p, e] = std::from_chars(c.data(), c.data() + c.size(), n);
    if (!c.empty() && e == std::errc{} && n)
    {
        if (n > max_body_size)
            return false;
        body_bytes_remaining_ = n;
        body_.reserve(n);
        return true;
    }
    return false;
}

auto response_parser::process_chunked_body() -> std::expected<bool, std::error_code>
{
    for (;;)
    {
        const auto readable = readable_buffer();
        auto p = detail::find_crlf(readable.data(), readable.size());
        if (p == std::string_view::npos)
            return false;
        std::size_t n{};
        auto [q, e] = std::from_chars(
            readable.data(), readable.data() + p, n, 16);
        if (e != std::errc{})
            return std::unexpected(make_error_code(http_errc::invalid_chunk));
        if (!n)
        {
            const auto completed =
                detail::completed_chunked_message_size(readable, p);
            if (!completed)
            {
                if (readable.size() > max_header_size)
                    return std::unexpected(
                        make_error_code(http_errc::header_too_large));
                return false;
            }
            read_pos_ += *completed;
            return true;
        }
        auto start = p + 2;
        if (readable.size() < start + n + 2)
            return false;
        body_.append(readable.data() + start, n);
        read_pos_ += start + n + 2;
        if (body_.size() > max_body_size)
            return std::unexpected(make_error_code(http_errc::body_too_large));
    }
}
} // namespace cnetmod::http
