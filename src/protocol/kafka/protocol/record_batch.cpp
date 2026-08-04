module;
#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_ZLIB
    #include <zlib.h>
#endif
#ifdef CNETMOD_HAS_LZ4
    #include <lz4frame.h>
#endif
module cnetmod.protocol.kafka.record_batch;
import std;
import cnetmod.protocol.kafka.protocol_value_codec;

namespace cnetmod::kafka {
namespace {
#ifdef CNETMOD_HAS_ZLIB
    class gzip_codec final : public compression_codec
    {
    public:
        auto algorithm() const noexcept -> compression override
        {
            return compression::gzip;
        }

        auto compress(std::span<const std::byte> in) -> result<bytes> override
        {
            z_stream s{};
            if (deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8,
                    Z_DEFAULT_STRATEGY) != Z_OK)
                return std::unexpected(make_error(error_code::configuration,
                    "zlib deflate initialization failed"));
            bytes out(deflateBound(&s, static_cast<uLong>(in.size())));
            s.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(in.data()));
            s.avail_in = static_cast<uInt>(in.size());
            s.next_out = reinterpret_cast<Bytef*>(out.data());
            s.avail_out = static_cast<uInt>(out.size());
            auto rc = deflate(&s, Z_FINISH);
            auto n = s.total_out;
            deflateEnd(&s);
            if (rc != Z_STREAM_END)
                return std::unexpected(
                    make_error(error_code::corrupt_message, "gzip compression failed"));
            out.resize(n);
            return out;
        }

        auto decompress(std::span<const std::byte> in, std::size_t hint)
            -> result<bytes> override
        {
            z_stream s{};
            if (inflateInit2(&s, MAX_WBITS + 16) != Z_OK)
                return std::unexpected(make_error(error_code::configuration,
                    "zlib inflate initialization failed"));
            bytes out(std::max<std::size_t>(hint, 64 * 1024));
            s.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(in.data()));
            s.avail_in = static_cast<uInt>(in.size());
            int rc = Z_OK;
            while (rc == Z_OK)
            {
                if (s.total_out == out.size())
                    out.resize(out.size() * 2);
                s.next_out = reinterpret_cast<Bytef*>(out.data() + s.total_out);
                s.avail_out = static_cast<uInt>(out.size() - s.total_out);
                rc = inflate(&s, Z_NO_FLUSH);
            }
            auto n = s.total_out;
            inflateEnd(&s);
            if (rc != Z_STREAM_END)
                return std::unexpected(
                    make_error(error_code::corrupt_message, "invalid gzip record batch"));
            out.resize(n);
            return out;
        }
    };
#endif
#ifdef CNETMOD_HAS_LZ4
    class lz4_frame_codec final : public compression_codec
    {
    public:
        auto algorithm() const noexcept -> compression override
        {
            return compression::lz4;
        }

        auto compress(std::span<const std::byte> input) -> result<bytes> override
        {
            LZ4F_preferences_t preferences{};
            preferences.frameInfo.blockMode = LZ4F_blockIndependent;
            preferences.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
            preferences.frameInfo.contentSize = input.size();
            const auto bound = LZ4F_compressFrameBound(input.size(), &preferences);
            if (LZ4F_isError(bound))
                return std::unexpected(make_error(
                    error_code::configuration,
                    std::string("LZ4 frame bound failed: ") + LZ4F_getErrorName(bound)));
            bytes output(bound);
            const auto written = LZ4F_compressFrame(
                output.data(), output.size(), input.data(), input.size(), &preferences);
            if (LZ4F_isError(written))
                return std::unexpected(make_error(
                    error_code::corrupt_message,
                    std::string("LZ4 frame compression failed: ") +
                        LZ4F_getErrorName(written)));
            output.resize(written);
            return output;
        }

        auto decompress(std::span<const std::byte> input, std::size_t hint)
            -> result<bytes> override
        {
            LZ4F_dctx* raw_context = nullptr;
            const auto created =
                LZ4F_createDecompressionContext(&raw_context, LZ4F_VERSION);
            if (LZ4F_isError(created))
                return std::unexpected(make_error(
                    error_code::configuration,
                    std::string("LZ4 decompressor creation failed: ") +
                        LZ4F_getErrorName(created)));

            struct context_owner
            {
                LZ4F_dctx* value;

                ~context_owner()
                {
                    LZ4F_freeDecompressionContext(value);
                }
            } context{raw_context};

            bytes output;
            output.reserve(std::max<std::size_t>(hint, 64 * 1024));
            std::size_t source_offset = 0;
            std::array<std::byte, 64 * 1024> chunk{};
            std::size_t remaining_hint = 1;
            while (source_offset < input.size() || remaining_hint != 0)
            {
                std::size_t source_size = input.size() - source_offset;
                std::size_t destination_size = chunk.size();
                remaining_hint = LZ4F_decompress(
                    context.value, chunk.data(), &destination_size,
                    input.data() + source_offset, &source_size, nullptr);
                if (LZ4F_isError(remaining_hint))
                    return std::unexpected(make_error(
                        error_code::corrupt_message,
                        std::string("invalid Kafka LZ4 frame: ") +
                            LZ4F_getErrorName(remaining_hint)));
                source_offset += source_size;
                output.insert(output.end(), chunk.begin(),
                    chunk.begin() + static_cast<std::ptrdiff_t>(destination_size));
                if (remaining_hint == 0)
                    break;
                if (source_size == 0 && destination_size == 0)
                    return std::unexpected(make_error(error_code::corrupt_message,
                        "truncated Kafka LZ4 frame"));
            }
            if (source_offset != input.size())
                return std::unexpected(make_error(error_code::corrupt_message,
                    "trailing bytes in Kafka LZ4 frame"));
            return output;
        }
    };
#endif
} // namespace

compression_registry::compression_registry()
{
#ifdef CNETMOD_HAS_ZLIB
    install(std::make_shared<gzip_codec>());
#endif
#ifdef CNETMOD_HAS_LZ4
    install(std::make_shared<lz4_frame_codec>());
#endif
}

void compression_registry::install(std::shared_ptr<compression_codec> c)
{
    if (c)
        codecs_[c->algorithm()] = std::move(c);
}

auto compression_registry::find(compression c) const
    -> std::shared_ptr<compression_codec>
{
    auto i = codecs_.find(c);
    return i == codecs_.end() ? nullptr : i->second;
}

auto compression_registry::supports(compression c) const noexcept -> bool
{
    return c == compression::none || codecs_.contains(c);
}

auto compression_registry::available() const -> std::vector<compression>
{
    std::vector<compression> out{compression::none};
    for (auto& [c, _] : codecs_)
        out.push_back(c);
    return out;
}

namespace {
    auto err(std::string m)
    {
        return make_error(error_code::corrupt_message, std::move(m));
    }

    auto raw_records(std::span<const record> records, std::int64_t base_ts)
        -> bytes
    {
        protocol::encoder out;
        std::int32_t delta = 0;
        for (auto& r : records)
        {
            protocol::encoder body;
            body.int8(0);
            body.varlong((r.timestamp < 0 ? base_ts : r.timestamp) - base_ts);
            body.varint(delta++);
            if (r.key)
            {
                body.varint(static_cast<std::int32_t>(r.key->size()));
                body.raw(*r.key);
            }
            else
                body.varint(-1);
            if (r.value)
            {
                body.varint(static_cast<std::int32_t>(r.value->size()));
                body.raw(*r.value);
            }
            else
                body.varint(-1);
            body.varint(static_cast<std::int32_t>(r.headers.size()));
            for (auto& h : r.headers)
            {
                body.varint(static_cast<std::int32_t>(h.key.size()));
                body.raw(
                    {reinterpret_cast<const std::byte*>(h.key.data()), h.key.size()});
                body.varint(static_cast<std::int32_t>(h.value.size()));
                body.raw(h.value);
            }
            auto encoded = std::move(body).take();
            out.varint(static_cast<std::int32_t>(encoded.size()));
            out.raw(encoded);
        }
        return std::move(out).take();
    }
} // namespace

auto encode_record_batch(std::span<const record> records,
    const record_batch_options& o,
    const compression_registry& r) -> result<bytes>
{
    if (records.empty())
        return bytes{};
    auto base_ts = records.front().timestamp < 0
        ? std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count()
        : records.front().timestamp;
    auto last_ts = base_ts;
    for (auto& x : records)
        last_ts = std::max(last_ts, x.timestamp < 0 ? base_ts : x.timestamp);
    auto payload = raw_records(records, base_ts);
    if (o.compression_type != compression::none)
    {
        auto c = r.find(o.compression_type);
        if (!c)
            return std::unexpected(make_error(error_code::configuration,
                "compression codec is not installed"));
        auto z = c->compress(payload);
        if (!z)
            return std::unexpected(z.error());
        payload = std::move(*z);
    }
    protocol::encoder fields;
    fields.int16(static_cast<std::int16_t>(
        (o.attributes & ~7) | static_cast<std::int16_t>(o.compression_type) |
        (o.transactional ? 0x10 : 0)));
    fields.int32(static_cast<std::int32_t>(records.size() - 1));
    fields.int64(base_ts);
    fields.int64(last_ts);
    fields.int64(o.producer_id);
    fields.int16(o.producer_epoch);
    fields.int32(o.base_sequence);
    fields.int32(static_cast<std::int32_t>(records.size()));
    fields.raw(payload);
    auto crc_fields = std::move(fields).take();
    protocol::encoder batch;
    batch.int64(0);
    batch.int32(static_cast<std::int32_t>(4 + 1 + 4 + crc_fields.size()));
    batch.int32(-1);
    batch.int8(2);
    batch.int32(static_cast<std::int32_t>(protocol::crc32c(crc_fields)));
    batch.raw(crc_fields);
    return std::move(batch).take();
}

auto decode_record_batch(std::span<const std::byte> input,
    const topic_partition& tp,
    const compression_registry& r)
    -> result<decoded_record_batch>
{
    protocol::decoder d(input);
    auto base = d.int64();
    auto len = d.int32();
    auto epoch = d.int32();
    auto magic = d.int8();
    auto crc = d.int32();
    if (!base || !len || !epoch || !magic || !crc || *magic != 2)
        return std::unexpected(err("invalid record batch header"));
    auto tail = d.slice(d.remaining());
    if (!tail)
        return std::unexpected(tail.error());
    if (protocol::crc32c(*tail) != static_cast<std::uint32_t>(*crc))
        return std::unexpected(err("record batch CRC32C mismatch"));
    protocol::decoder f(*tail);
    auto attrs = f.int16();
    auto last_delta = f.int32();
    auto base_ts = f.int64();
    auto max_ts = f.int64();
    auto pid = f.int64();
    auto pep = f.int16();
    auto seq = f.int32();
    auto count = f.int32();
    if (!attrs || !last_delta || !base_ts || !max_ts || !pid || !pep || !seq ||
        !count)
        return std::unexpected(err("truncated record batch"));
    auto payload = f.slice(f.remaining());
    if (!payload)
        return std::unexpected(payload.error());
    bytes expanded(payload->begin(), payload->end());
    auto alg = static_cast<compression>(*attrs & 7);
    if (alg != compression::none)
    {
        auto c = r.find(alg);
        if (!c)
            return std::unexpected(make_error(error_code::configuration,
                "compression codec is not installed"));
        auto x = c->decompress(expanded, 0);
        if (!x)
            return std::unexpected(x.error());
        expanded = std::move(*x);
    }
    protocol::decoder q(expanded);
    decoded_record_batch out{.base_offset = *base,
        .last_offset = *base + *last_delta,
        .producer_id = *pid,
        .transactional = (*attrs & 0x10) != 0,
        .control = (*attrs & 0x20) != 0,
        .records = {}};
    out.records.reserve(*count);
    for (std::int32_t i = 0; i < *count; ++i)
    {
        auto n = q.varint();
        if (!n || *n < 0)
            return std::unexpected(err("invalid record length"));
        auto raw = q.slice(*n);
        if (!raw)
            return std::unexpected(raw.error());
        protocol::decoder z(*raw);
        auto a = z.int8();
        auto td = z.varlong();
        auto od = z.varint();
        auto kl = z.varint();
        if (!a || !td || !od || !kl)
            return std::unexpected(err("truncated record"));
        std::optional<bytes> key;
        if (*kl >= 0)
        {
            auto x = z.slice(*kl);
            if (!x)
                return std::unexpected(x.error());
            key = bytes(x->begin(), x->end());
        }
        auto vl = z.varint();
        if (!vl)
            return std::unexpected(vl.error());
        std::optional<bytes> val;
        if (*vl >= 0)
        {
            auto x = z.slice(*vl);
            if (!x)
                return std::unexpected(x.error());
            val = bytes(x->begin(), x->end());
        }
        auto hc = z.varint();
        if (!hc || *hc < 0)
            return std::unexpected(err("invalid headers"));
        std::vector<header> headers;
        for (int h = 0; h < *hc; ++h)
        {
            auto nk = z.varint();
            if (!nk || *nk < 0)
                return std::unexpected(err("invalid header key"));
            auto ks = z.slice(*nk);
            auto nv = z.varint();
            if (!ks || !nv || *nv < 0)
                return std::unexpected(err("invalid header"));
            auto vs = z.slice(*nv);
            if (!vs)
                return std::unexpected(vs.error());
            headers.push_back(
                {std::string(reinterpret_cast<const char*>(ks->data()), ks->size()),
                    bytes(vs->begin(), vs->end())});
        }
        out.records.push_back({tp, *base + *od, *base_ts + *td, std::move(key),
            std::move(val), std::move(headers), *epoch});
    }
    return out;
}
} // namespace cnetmod::kafka
