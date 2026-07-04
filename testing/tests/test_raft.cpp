#include "test_framework.hpp"

#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.core.ssl;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor.async_op;
import cnetmod.protocol.tcp;
import cnetmod.protocol.raft;

using namespace cnetmod::raft;

static auto make_node(std::string id, std::vector<std::string> peers = {})
    -> std::pair<std::shared_ptr<memory_store>, raft_node>
{
    auto store = std::make_shared<memory_store>();
    raft_config cfg{.id = std::move(id), .peers = std::move(peers)};
    return {store, raft_node{std::move(cfg), store}};
}

static auto reserve_loopback_endpoint() -> cnetmod::endpoint {
    auto ctx = cnetmod::make_io_context();
    cnetmod::tcp::acceptor acc{*ctx};
    auto opened = acc.open(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0},
                           cnetmod::socket_options{.reuse_address = true});
    if (!opened)
        throw std::runtime_error("failed to reserve loopback port");
    auto local = acc.native_socket().local_endpoint();
    acc.close();
    if (!local)
        throw std::runtime_error("failed to read reserved loopback port");
    return *local;
}

static auto touch_file_async(cnetmod::io_context& ctx, const std::filesystem::path& path)
    -> cnetmod::task<void>
{
    auto opened = co_await cnetmod::async_file_open(ctx, path,
        cnetmod::open_mode::write | cnetmod::open_mode::create | cnetmod::open_mode::truncate);
    if (opened)
        (void)co_await cnetmod::async_file_close(ctx, *opened);
}

static auto write_text_file_async(cnetmod::io_context& ctx,
                                  const std::filesystem::path& path,
                                  std::string_view text)
    -> cnetmod::task<bool>
{
    auto opened = co_await cnetmod::async_file_open(ctx, path,
        cnetmod::open_mode::write | cnetmod::open_mode::create | cnetmod::open_mode::truncate);
    if (!opened)
        co_return false;

    std::size_t written = 0;
    while (written < text.size()) {
        auto n = co_await cnetmod::async_file_write(ctx, *opened,
            cnetmod::const_buffer{text.data() + written, text.size() - written},
            written);
        if (!n || *n == 0) {
            (void)co_await cnetmod::async_file_close(ctx, *opened);
            co_return false;
        }
        written += *n;
    }

    auto flushed = co_await cnetmod::async_file_flush(ctx, *opened);
    auto closed = co_await cnetmod::async_file_close(ctx, *opened);
    co_return flushed.has_value() && closed.has_value();
}

#ifdef CNETMOD_HAS_SSL
namespace raft_tls_test_material {
static constexpr std::string_view ca_cert = R"PEM(-----BEGIN CERTIFICATE-----
MIIC8TCCAdmgAwIBAgIUT17vVOs0hMolTxGBR6sW+1AcW7AwDQYJKoZIhvcNAQEL
BQAwHzEdMBsGA1UEAwwUY25ldG1vZC1yYWZ0LXRlc3QtY2EwIBcNMjAwMTAxMDAw
MDAwWhgPMjEyMDAxMDEwMDAwMDBaMB8xHTAbBgNVBAMMFGNuZXRtb2QtcmFmdC10
ZXN0LWNhMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAy+a2CGO8XkDs
EVTmLhJxVBNTaRnUIKu4qZxoCECxbnWYrkds3lQy61cpem/ZKAFhlUB7X4CrTJ2M
CnYTuT8V+/4BlFmvtZRGh6I388kamC2NNu4kOZ8BD4m5DmDytWewOjsHvRHQwNyl
G94PiG9ZALHCKkk63tb4fMenV2bw8KE5DNJn9doQqMr21N3aGnKs27sb6d4/ARom
lymNKeiQyKy6xQcoqHXBd9o0fjOViDg+twdutENpYLo057PpN7FtKV08LW8qwd0K
xzVkG6g1pDUHFbVYWKXkRGPHDQW6OeYUpwznlnSIyg8szz9zViAwn+Fr1jJi0ADB
R0xpWSQm1QIDAQABoyMwITAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIB
hjANBgkqhkiG9w0BAQsFAAOCAQEAnKkhu1Nhcn5vaoT7xo5Pv0I7xURfAs6uT3HG
EARblrjS8tiAAxKHKMjDUwOGX4xntr6rc5EF3lXGY2oQzhmtgAKCyhsOh5rT5mEW
c8G+q1gdNVgMJd87UYJrwJf7Of5oqMOQH5za0p6KG8aLOWPVKeXBdCSR6PQxVg/M
NmgZcqhI8hd1ky5/ZG5lOYSZGNm+2ccsTUXeDP2IUHrnLHuSWu88FbK0AMLTsY0Z
oXj24HcfyivpH76s2VvwMvKgwc350cluGiGz0Y+qRU6hsIu7RKHO76Dej+oDsTtW
75JUvlHuFFL1DlPnBzSNnHN9f/PUw7ajbppOIl7yVZca0eDrkQ==
-----END CERTIFICATE-----
)PEM";
static constexpr std::string_view n1_cert = R"PEM(-----BEGIN CERTIFICATE-----
MIIDBzCCAe+gAwIBAgIUYz1B/C1atOpNYzIK4l24HapusykwDQYJKoZIhvcNAQEL
BQAwHzEdMBsGA1UEAwwUY25ldG1vZC1yYWZ0LXRlc3QtY2EwIBcNMjAwMTAxMDAw
MDAwWhgPMjEyMDAxMDEwMDAwMDBaMA0xCzAJBgNVBAMMAm4xMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAtd4u2qa6bICkm9FkPFRsVDRp2TvkMLClzM7z
wSv6/mw+vvfamdoJXqhRlr6PwcYst/V2nNb75osI1bJ8uLa1mJdnicg0qe/oG+tR
MM1lXxgUT72W04SWDzll3jX73uQFv/dlutAE3fXpydm3GsEFITE+Mvf7lGUPU+CU
BhiKeiJPCC1WYluKl+gPN/G54KPpXUjcQauehkxLAqzUSuhMZM2w9o6i5PfN+UK7
ih1ycFLzONYSl/1XIONgYD9RGyhq70LK0GzCrPkty4Xl+B6MV/Dl/vEza518e5h7
2hoHJMWH1zYutWvb1z7Iw7c6bGTruEnhJDHqbB2/hHoaIdk7AwIDAQABo0swSTAM
BgNVHRMBAf8EAjAAMBoGA1UdEQQTMBGCCWxvY2FsaG9zdIcEfwAAATAdBgNVHSUE
FjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDQYJKoZIhvcNAQELBQADggEBAGcZc4PX
M8BGT3Gi+V//ZJBrqrfRr+kvcWcZwyenHTCbiCBSkUEQiqdup19dt9odDnMTxgEm
tdPlTiFeNmascahiaF2/jQ88ajTBvlnT2w/vMKFZXfBkIDiqBemweWOEbVNxDWUJ
PX/6tWxQAyGqWxwwi9A+sfAfKFgWUBxOZkunvZlmAMlF9LZaoBs2aA6S56MrrmEs
IInAh51nRv4oXw0R4M0n8urbngbhzn5EHyFu1/Lfdjvax+b/6EDgzagV3UzN48TC
atd+j7lTNAby/cjeRBMxCenLsC+e1FvFm7+1wxkLWsQCkbk335r5iwkSiBeGUjzf
HQkINxUs1uuJzkQ=
-----END CERTIFICATE-----
)PEM";
static constexpr std::string_view n1_key = R"PEM(-----BEGIN RSA PRIVATE KEY-----
MIIEogIBAAKCAQEAtd4u2qa6bICkm9FkPFRsVDRp2TvkMLClzM7zwSv6/mw+vvfa
mdoJXqhRlr6PwcYst/V2nNb75osI1bJ8uLa1mJdnicg0qe/oG+tRMM1lXxgUT72W
04SWDzll3jX73uQFv/dlutAE3fXpydm3GsEFITE+Mvf7lGUPU+CUBhiKeiJPCC1W
YluKl+gPN/G54KPpXUjcQauehkxLAqzUSuhMZM2w9o6i5PfN+UK7ih1ycFLzONYS
l/1XIONgYD9RGyhq70LK0GzCrPkty4Xl+B6MV/Dl/vEza518e5h72hoHJMWH1zYu
tWvb1z7Iw7c6bGTruEnhJDHqbB2/hHoaIdk7AwIDAQABAoIBADeLOXUW6aW8rklW
bI4OXJ1k+pTZeUozkReZdxGerIPqrEknqnBFiooJzw1CeuY776lpQsYI1JsnCFY3
sma5ioGlb/5BEnB6MEHinwPQHy8pS/7EkN1dx3Sz23w/sYJz0pu746pn4Kynb7Tx
lL4LiFUIVI49dWzvZnyAAMVR1m0RK1aZCnKuNYckRUtGegaGMo18aX57KwQYAcew
E+VBzKfGlmH1RFtC+Nu+PPDVNc3bcaGFdmpCWKHO5oZWFL4vHjq2XQbINOviELGY
+zo2gVtNdEgN3ifZgKlNOJ3lw7ZrZDgsS/fEXCGlX+gru7DcgKi28fAcWVX1WGXL
8m+OtwECgYEA93uhEnijfnr/3zFKfdaOdA54ftHar18TZ4G+/Kf5Ht5u5vXrrFRK
DSNmJvAoDJ3n3iuNf08a8snKMYYu3PcIt2B5baQtvWRVa5+fyAHVmIUNCzVxebmE
o2dQhAmIUz5kvgowJl29NFSwq+EQCT1dc79kZ2d5dh5PQ31i28irqQECgYEAvCB5
JL2d4Eoio52mSmVwyO1XU4T5q7BEiSH9dTzv7v5BSwjiX1QRlbK09FWUsk/sm4vn
d6fAYMufPpShxg1FDLAKJIey3uMCg0sXeEbaaCVMi/y9kbRnVY4LVM20qBlEvONx
wv5sJApkZbSEeAOVzYkAP+f1y9d9U6qw19eWQAMCgYBClw9WCwPi6nGiun6SsYKP
E720Uf8HpQtxlGWxUfkkJzGsD4ukSOHL+zRnUcNU8cAL9agTE1Pq9ATlondFmWrM
/LZvm/d6uF6LoN27UVMJwPMriuvHlvVrikcN4ArAa09sGw2tpRdd11PaS6qm8c4N
cPgxIpR6BdnKDRjmjm4yAQKBgDzYE9Y6LXF5CLL6Leop3MxfVrsAau+IVuIzSBI+
3yrguKVX/j7upbFQ1w2fEDSLfO7h1L7yVln4AUzwLVIswIRV+zHNYaCMsydbhf+0
irbcOWGdIIKbYkBdbHTFAwTLB1xAA28ZckhFxCrQs3dNUYnkIVyJ3QlMbJA+yGtG
oqWZAoGAeimc3JNSgyBBJpk24YNHt2CNnnlKIJiyBrH6FNigozEwnEX2JqATyhPI
wTFrC0aI7qRTw1LFd374fusyImmsn+emvMqbbzbq+RTLLLRrcNc6HWRqIbWk/Xin
vQTHRnH0yO4RCNv4jysIspO6x+Biuh+V3eBPjNFzsumhTuAKQJ0=
-----END RSA PRIVATE KEY-----
)PEM";
static constexpr std::string_view n2_cert = R"PEM(-----BEGIN CERTIFICATE-----
MIIDBzCCAe+gAwIBAgIUVr/n+U1lBU8DfMm/jYs0PsW0CZIwDQYJKoZIhvcNAQEL
BQAwHzEdMBsGA1UEAwwUY25ldG1vZC1yYWZ0LXRlc3QtY2EwIBcNMjAwMTAxMDAw
MDAwWhgPMjEyMDAxMDEwMDAwMDBaMA0xCzAJBgNVBAMMAm4yMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvZmb6wA7ajItXt0VTOtAakBrjfx4/ngLC2BJ
pUQkhA9/VqNddEPSwd1sdU9FX4w+FfGRZ0aq8zOPH34oDLYe+sDhmXnhzHrpttSX
MZmJGCySLV/f+cnjUmcCOkyCULoR+VGd977KY/9/RXZMOc7ZkjKyQuHGwOGf9N+Q
PhR4CMUxzfqVIc7u/wlyi12GesiM2J0XTXLeL+O+L/8eJbigVgYmMmlGqq5NOaVC
j/h9C5/ZhRbDfC3kLRdZFLqno5jebQytPMdsELwc5rTKohzfFx1txF9qWIoawvWT
tgtOUASjr+nFnzRsQGbjD05u2NXZr4ojpZcXHXxvg9FSWTNJJQIDAQABo0swSTAM
BgNVHRMBAf8EAjAAMBoGA1UdEQQTMBGCCWxvY2FsaG9zdIcEfwAAATAdBgNVHSUE
FjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDQYJKoZIhvcNAQELBQADggEBAIOhQ7fC
rJiO8Pgs9QH+klwUjOQHMRQ/uWCvb0CKdKGY2Teqz5KT8TYKsqVsPMqJE9KnMXw2
6sogb7STpG42Ls7hqNy7X7HHtP9A8vyBjBZQ8Ej479So58aqw5cv81bw7EEo/tgN
xa99wl8bn3LAM0VWBSYO3ZGpws8Z3x+ms0ebrMU+nGNKMKTb7MoGBLa8/Zt9XEkh
l3y7VYdNH3LqjrE4z331NARC+ghpUgdkSCf36vS55EnXsNY1spEvpirWeWBe88wD
DeCOP5RFUAwMddyI8NkuNasZnjMjDydFJ1n6zaDEo9G1rqiHhN/OJdSN0ciPmoQH
O+IjNa4k+NA6U84=
-----END CERTIFICATE-----
)PEM";
static constexpr std::string_view n2_key = R"PEM(-----BEGIN RSA PRIVATE KEY-----
MIIEoQIBAAKCAQEAvZmb6wA7ajItXt0VTOtAakBrjfx4/ngLC2BJpUQkhA9/VqNd
dEPSwd1sdU9FX4w+FfGRZ0aq8zOPH34oDLYe+sDhmXnhzHrpttSXMZmJGCySLV/f
+cnjUmcCOkyCULoR+VGd977KY/9/RXZMOc7ZkjKyQuHGwOGf9N+QPhR4CMUxzfqV
Ic7u/wlyi12GesiM2J0XTXLeL+O+L/8eJbigVgYmMmlGqq5NOaVCj/h9C5/ZhRbD
fC3kLRdZFLqno5jebQytPMdsELwc5rTKohzfFx1txF9qWIoawvWTtgtOUASjr+nF
nzRsQGbjD05u2NXZr4ojpZcXHXxvg9FSWTNJJQIDAQABAoH/YwtHobGv0peLZBT5
dCat6EMnXCjaiinU9U7gfNnpjF7xF+irdcxrwAklK9rRE07ZIAYDBMUREYA++Dg+
TrwnrH+mMqb6ucwTFP9+qRPlvIxAUi82xsfbcwjBhh8XebL3NiK8CIffpbBCVQ42
SQ6LyKEmfieJGGhDI4Cw/UKFOEE7HaXjMGPHfvJAiJ7vIP61Kmt+WImm/QlIHQAk
81/HQ/Ro7F823rPluQFi6xPmBv/mobD3VGW88NSxGkYlEzxGsPpwjc76Vt6zpTmP
/adXaWZS/cKPcvUgkR01iy9dAkkW1bRoya44fNvmR5HL2BuTrWoB+LdyhWfkf5ds
Q/8tAoGBAOeL/PudIdZzWku7QjcG7wwtQ5c/BTzzjfhB4NO+7zZZqDQA2Z9/meew
QsZgfyl9zrCgi5e+0J3pSPFKCc19uodu5ho3a7dIN0z/A37h5M5AeHsVUDkxu0wy
LVqXbII1RPr8c0TkZZvE/r7/oJxNJACHEHdjf3UDOPmKDeQmYa6jAoGBANGfkDXb
VtNdkOXcRFcTkibSjNca9aCiCX5+CbzyWEoo/KuScoN+VX70t1AkO3Mj+OBszl/K
EX2d39My/VqLZ99Q1w57z4M268661ZDRYlo8yO9Y3SWdxLm+uJUzivhGEviZu0y/
ppJPY00nsY9LJNCy95fPoQlJrGKcoNPf3Q2XAoGBALjHkmAJiZNmAs0k5zaapfIF
vUbZ2AhIJSfVCuJwIN4ytnSpqQIMBnpKwz8kitZFu8hgloXGlR0vqjJEb4Y5q72g
1qhdSey+CMO9TsDW7I1cDcnLvHWoJlwsPt1osgNHF4FkLWjxC9U/ZDxwK3AQb4as
QovEL1bl93XMokFMD/AZAoGAIf9T/zgco9kn9++6cbjt2jgJuZVYwv2ktowwfiF7
6kMtf5IX2nWx3g5IcMn/jlQGODfNXMHEBnCFbZZ2eqnjZdeRmXrBFBHjOrsYig07
e1EicZci/sfQsSNagnBCmLOcvg8IzpDCrjYL7+aBKLFSPjrYZxm4j24QdEnc16AW
FO8CgYBa/TtcnXLXX8FkRQXQuuTwV8pQ312TvdbnR50xd1WTkUUelHHYC4M7I3iX
yuTITCfRseQc0gPYx8aDFy1up+KgAe0BtVkXuMIfdM/7dWz2dXAn2Mqm4rYrBIBS
QevtCq73NgTVlXzdSYQ4wzMs86p7CO33q1ERxX0724UDF72i9w==
-----END RSA PRIVATE KEY-----
)PEM";
static constexpr std::string_view n1_fp = "31f3cd637305721516dfaceb7f2d1e635e50812de45811829a3738b99a497261";
static constexpr std::string_view n2_fp = "2943f0976470c63e6ce1cf8efb4c6461e7d3bf2ee3caa293f9c79d39fa1aec55";
}
#endif

class recording_machine final : public state_machine {
public:
    void on_apply(const log_entry& entry) override {
        applied.push_back(entry.command);
    }

    auto save_snapshot(const snapshot_writer& writer) -> std::expected<void, raft_error> override {
        saved = writer.metadata;
        saved_uri = writer.uri;
        return {};
    }

    auto load_snapshot(const snapshot_reader& reader) -> std::expected<void, raft_error> override {
        loaded = reader.metadata;
        loaded_uri = reader.uri;
        return {};
    }

    std::vector<std::string> applied;
    snapshot_metadata saved;
    snapshot_metadata loaded;
    std::string saved_uri;
    std::string loaded_uri;
};

class throwing_append_store final : public raft_storage {
public:
    auto load_hard_state() -> hard_state override { return inner_.load_hard_state(); }
    void save_hard_state(const hard_state& state) override { inner_.save_hard_state(state); }
    auto load_snapshot_metadata() -> snapshot_metadata override { return inner_.load_snapshot_metadata(); }
    void save_snapshot_metadata(const snapshot_metadata& metadata) override {
        inner_.save_snapshot_metadata(metadata);
    }
    auto first_log_index() const -> log_index override { return inner_.first_log_index(); }
    auto last_log_index() const -> log_index override { return inner_.last_log_index(); }
    auto term_at(log_index index) const -> term_t override { return inner_.term_at(index); }
    auto entry_at(log_index index) const -> std::optional<log_entry> override {
        return inner_.entry_at(index);
    }
    auto entries(log_index first_index, std::size_t max_entries) const
        -> std::vector<log_entry> override {
        return inner_.entries(first_index, max_entries);
    }
    void append(const std::vector<log_entry>&) override {
        throw std::runtime_error("injected append failure");
    }
    void truncate_prefix(log_index first_kept_index) override {
        inner_.truncate_prefix(first_kept_index);
    }
    void truncate_suffix(log_index first_removed_index) override {
        inner_.truncate_suffix(first_removed_index);
    }
    void reset_to_snapshot(const snapshot_metadata& metadata) override {
        inner_.reset_to_snapshot(metadata);
    }

private:
    memory_store inner_;
};

class throwing_snapshot_store final : public raft_storage {
public:
    auto load_hard_state() -> hard_state override { return inner_.load_hard_state(); }
    void save_hard_state(const hard_state& state) override { inner_.save_hard_state(state); }
    auto load_snapshot_metadata() -> snapshot_metadata override { return inner_.load_snapshot_metadata(); }
    void save_snapshot_metadata(const snapshot_metadata&) override {
        throw std::runtime_error("injected snapshot metadata failure");
    }
    auto first_log_index() const -> log_index override { return inner_.first_log_index(); }
    auto last_log_index() const -> log_index override { return inner_.last_log_index(); }
    auto term_at(log_index index) const -> term_t override { return inner_.term_at(index); }
    auto entry_at(log_index index) const -> std::optional<log_entry> override {
        return inner_.entry_at(index);
    }
    auto entries(log_index first_index, std::size_t max_entries) const
        -> std::vector<log_entry> override {
        return inner_.entries(first_index, max_entries);
    }
    void append(const std::vector<log_entry>& entries) override { inner_.append(entries); }
    void truncate_prefix(log_index first_kept_index) override {
        inner_.truncate_prefix(first_kept_index);
    }
    void truncate_suffix(log_index first_removed_index) override {
        inner_.truncate_suffix(first_removed_index);
    }
    void reset_to_snapshot(const snapshot_metadata&) override {
        throw std::runtime_error("injected snapshot restore failure");
    }

private:
    memory_store inner_;
};

TEST(raft_single_node_election_becomes_leader) {
    auto [store, node] = make_node("n1");

    auto req = node.begin_election();

    ASSERT_EQ(req.term, 1u);
    ASSERT_EQ(req.candidate_id, std::string("n1"));
    ASSERT_TRUE(node.role() == node_role::leader);
    ASSERT_EQ(node.current_term(), 1u);
    ASSERT_EQ(std::string(node.voted_for()), std::string("n1"));
}

TEST(raft_leader_appends_and_commits_single_node_command) {
    auto [store, node] = make_node("n1");
    node.begin_election();

    auto appended = node.append_command("set x=1");

    ASSERT_TRUE(appended.has_value());
    ASSERT_EQ(appended->index, 2u);
    ASSERT_EQ(appended->term, 1u);
    ASSERT_EQ(node.commit_index(), 2u);
    ASSERT_EQ(store->entry_at(1)->type == entry_type::no_op, true);
    ASSERT_EQ(store->entry_at(2)->command, std::string("set x=1"));
}

TEST(raft_rejects_stale_vote_request) {
    auto [store, node] = make_node("n1", {"n2", "n3"});
    node.begin_election();

    request_vote_request stale{
        .term = 0,
        .candidate_id = "n2",
        .last_log_index = 0,
        .last_log_term = 0,
    };

    auto resp = node.handle_request_vote(stale);

    ASSERT_FALSE(resp.vote_granted);
    ASSERT_EQ(resp.term, 1u);
    ASSERT_EQ(std::string(node.voted_for()), std::string("n1"));
}

TEST(raft_append_entries_appends_log_and_updates_commit) {
    auto [store, node] = make_node("n2", {"n1", "n3"});

    append_entries_request req{
        .term = 3,
        .leader_id = "n1",
        .prev_log_index = 0,
        .prev_log_term = 0,
        .entries = {
            log_entry{.index = 1, .term = 3, .command = "cmd-1"},
            log_entry{.index = 2, .term = 3, .command = "cmd-2"},
        },
        .leader_commit = 2,
    };

    auto resp = node.handle_append_entries(req);

    ASSERT_TRUE(resp.success);
    ASSERT_EQ(resp.term, 3u);
    ASSERT_EQ(resp.match_index, 2u);
    ASSERT_EQ(node.commit_index(), 2u);
    ASSERT_EQ(store->entry_at(2)->command, std::string("cmd-2"));
}

TEST(raft_leader_commits_after_majority_replication) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    node.begin_election();
    ASSERT_TRUE(node.role() == node_role::candidate);

    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));
    ASSERT_TRUE(node.role() == node_role::leader);

    auto appended = node.append_command("cmd-majority");
    ASSERT_TRUE(appended.has_value());
    ASSERT_EQ(node.commit_index(), 0u);

    ASSERT_TRUE(node.handle_append_entries_response("n2", {
        .term = node.current_term(),
        .success = true,
        .match_index = appended->index,
    }));

    ASSERT_EQ(node.commit_index(), appended->index);
}

TEST(raft_pre_vote_does_not_persist_term_or_vote) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    auto req = node.begin_pre_vote();

    ASSERT_TRUE(req.pre_vote);
    ASSERT_EQ(req.term, 1u);
    ASSERT_EQ(node.current_term(), 0u);
    ASSERT_EQ(std::string(node.voted_for()), std::string(""));
}

TEST(raft_installs_snapshot_and_recovers_configuration) {
    auto [store, node] = make_node("n2", {"n1", "n3"});

    install_snapshot_request req{
        .term = 4,
        .leader_id = "n1",
        .metadata = snapshot_metadata{
            .last_included_index = 12,
            .last_included_term = 3,
            .configuration = configuration_state{.voters = {"n1", "n2", "n3"}},
        },
    };

    auto resp = node.handle_install_snapshot(req);

    ASSERT_TRUE(resp.success);
    ASSERT_EQ(node.current_term(), 4u);
    ASSERT_EQ(node.commit_index(), 12u);
    ASSERT_EQ(node.last_applied(), 12u);
    ASSERT_EQ(store->last_log_index(), 12u);
    ASSERT_EQ(store->term_at(12), 3u);
}

TEST(raft_fsm_snapshot_save_and_load_round_trip_metadata) {
    auto store = std::make_shared<memory_store>();
    recording_machine machine;
    raft_node node{raft_config{.id = "n1"}, store, &machine};
    node.begin_election();
    auto appended = node.append_command("snapshotted");
    ASSERT_TRUE(appended.has_value());

    auto snapshot = node.create_snapshot("n1.snapshot");

    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->last_included_index, appended->index);
    ASSERT_EQ(snapshot->uri, std::string("n1.snapshot"));
    ASSERT_EQ(machine.saved_uri, std::string("n1.snapshot"));
    ASSERT_EQ(store->load_snapshot_metadata().uri, std::string("n1.snapshot"));
    ASSERT_EQ(node.make_install_snapshot("n2").uri, std::string("n1.snapshot"));
    ASSERT_EQ(node.metrics().snapshot_index, appended->index);
    ASSERT_TRUE(node.metrics().role == node_role::leader);

    auto follower_store = std::make_shared<memory_store>();
    recording_machine follower_machine;
    raft_node follower{raft_config{.id = "n2", .peers = {"n1"}}, follower_store, &follower_machine};
    auto resp = follower.handle_install_snapshot(install_snapshot_request{
        .term = 2,
        .leader_id = "n1",
        .metadata = *snapshot,
        .uri = "materialized.snapshot",
    });

    ASSERT_TRUE(resp.success);
    ASSERT_EQ(follower_machine.loaded_uri, std::string("materialized.snapshot"));
    ASSERT_EQ(follower_store->load_snapshot_metadata().uri, std::string("materialized.snapshot"));
}

TEST(raft_joint_configuration_requires_both_majorities) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));

    auto change = node.enter_joint_configuration({"n1", "n2", "n3", "n4"});
    ASSERT_TRUE(change.has_value());

    ASSERT_TRUE(node.handle_append_entries_response("n2", {
        .term = node.current_term(),
        .success = true,
        .match_index = change->index,
    }));
    ASSERT_EQ(node.commit_index(), 0u);

    ASSERT_TRUE(node.handle_append_entries_response("n3", {
        .term = node.current_term(),
        .success = true,
        .match_index = change->index,
    }));
    ASSERT_EQ(node.commit_index(), change->index);
    ASSERT_TRUE(node.current_configuration().joint());
    auto leave_entry = store->entry_at(change->index + 1);
    ASSERT_TRUE(leave_entry.has_value());
    ASSERT_TRUE(leave_entry->type == entry_type::configuration);
    ASSERT_FALSE(leave_entry->configuration.joint());
}

TEST(raft_rejects_concurrent_configuration_change) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));

    auto first = node.enter_joint_configuration({"n1", "n2", "n3", "n4"});
    ASSERT_TRUE(first.has_value());
    auto second = node.enter_joint_configuration({"n1", "n2", "n3", "n5"});

    ASSERT_FALSE(second.has_value());
    ASSERT_TRUE(second.error().code == raft_errc::configuration_error);
}

TEST(raft_storage_exception_stops_node) {
    auto store = std::make_shared<throwing_append_store>();
    raft_node node{raft_config{.id = "n1"}, store};

    node.begin_election();

    ASSERT_TRUE(node.stopped());
    ASSERT_TRUE(node.fatal_error().code == raft_errc::storage_error);
    auto appended = node.append_command("must-fail");
    ASSERT_FALSE(appended.has_value());
    ASSERT_TRUE(appended.error().code == raft_errc::storage_error);
}

TEST(raft_wire_round_trips_append_entries) {
    raft_rpc_message msg{
        .type = raft_rpc_type::append_entries,
        .from = "n1",
        .to = "n2",
        .append = append_entries_request{
            .term = 7,
            .leader_id = "n1",
            .prev_log_index = 10,
            .prev_log_term = 6,
            .entries = {
                log_entry{
                    .index = 11,
                    .term = 7,
                    .type = entry_type::configuration,
                    .command = "cmd\nwith\nnewlines",
                    .configuration = configuration_state{
                        .voters = {"n1", "n2", "n3"},
                        .old_voters = {"n1", "n2"},
                        .learners = {"n4"},
                    },
                },
            },
            .leader_commit = 10,
        },
    };

    auto frame = encode_raft_message(msg);
    auto decoded = decode_raft_message(frame);

    ASSERT_EQ(static_cast<int>(decoded.type), static_cast<int>(raft_rpc_type::append_entries));
    ASSERT_EQ(decoded.from, std::string("n1"));
    ASSERT_EQ(decoded.to, std::string("n2"));
    ASSERT_EQ(decoded.append.term, 7u);
    ASSERT_EQ(decoded.append.entries.size(), 1u);
    ASSERT_EQ(decoded.append.entries[0].command, std::string("cmd\nwith\nnewlines"));
    ASSERT_EQ(decoded.append.entries[0].configuration.old_voters.size(), 2u);
    ASSERT_EQ(decoded.append.entries[0].configuration.learners.size(), 1u);
}

TEST(raft_wire_round_trips_timeout_now) {
    raft_rpc_message msg{
        .type = raft_rpc_type::timeout_now,
        .from = "n1",
        .to = "n2",
        .timeout_now = timeout_now_request{
            .term = 5,
            .leader_id = "n1",
        },
    };

    auto decoded = decode_raft_message(encode_raft_message(msg));

    ASSERT_EQ(static_cast<int>(decoded.type), static_cast<int>(raft_rpc_type::timeout_now));
    ASSERT_EQ(decoded.timeout_now.term, 5u);
    ASSERT_EQ(decoded.timeout_now.leader_id, std::string("n1"));
}

TEST(raft_wire_rejects_corrupt_or_truncated_frames) {
    raft_rpc_message msg{
        .type = raft_rpc_type::append_entries,
        .from = "n1",
        .to = "n2",
        .append = append_entries_request{.term = 1, .leader_id = "n1"},
    };

    auto frame = encode_raft_message(msg);
    auto bad_magic = frame;
    bad_magic[0] = std::byte{0};
    ASSERT_THROWS(decode_raft_message(bad_magic));

    auto truncated = frame;
    truncated.pop_back();
    ASSERT_THROWS(decode_raft_message(truncated));

    auto bad_size = frame;
    bad_size[8] = std::byte{0xff};
    ASSERT_THROWS(decode_raft_message(bad_size));
}

TEST(raft_runtime_tick_drives_single_node_election) {
    auto ctx = cnetmod::make_io_context();
    auto store = std::make_shared<memory_store>();
    raft_config cfg{
        .id = "n1",
        .options = raft_options{
            .election_timeout = std::chrono::milliseconds{0},
            .pre_vote = false,
        },
    };
    raft_node node{cfg, store};
    raft_tcp_transport transport{*ctx, "n1"};
    raft_node_runtime runtime{
        *ctx,
        node,
        transport,
        cnetmod::endpoint{cnetmod::ip_address{cnetmod::ipv4_address::loopback()}, 0},
        cfg.options,
        raft_runtime_options{.start_tcp_server = false, .auto_election = false, .auto_heartbeat = false},
    };

    runtime.tick_now();

    ASSERT_TRUE(node.role() == node_role::leader);
    ASSERT_EQ(node.current_term(), 1u);
    ASSERT_EQ(node.commit_index(), 1u);
    runtime.stop();
}

TEST(raft_runtime_async_read_index_returns_ready_for_single_node) {
    auto ctx = cnetmod::make_io_context();
    auto store = std::make_shared<memory_store>();
    raft_node node{raft_config{.id = "n1"}, store};
    node.begin_election();
    raft_tcp_transport transport{*ctx, "n1"};
    raft_node_runtime runtime{
        *ctx,
        node,
        transport,
        cnetmod::endpoint{cnetmod::ip_address{cnetmod::ipv4_address::loopback()}, 0},
        raft_options{},
        raft_runtime_options{.start_tcp_server = false, .auto_election = false, .auto_heartbeat = false},
    };

    auto result = sync_wait(runtime.async_read_index(read_index_request{
        .id = 501,
        .context = "single-node",
    }));

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->ready);
    ASSERT_EQ(result->index, node.commit_index());
    ASSERT_EQ(node.pending_read_count(), 0u);
}

TEST(raft_read_index_waits_for_quorum_ack) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));
    ASSERT_TRUE(node.role() == node_role::leader);

    auto read = node.read_index(read_index_request{.id = 42, .context = "linear-read"});
    ASSERT_TRUE(read.has_value());
    ASSERT_FALSE(read->ready);

    auto heartbeat = node.make_append_entries("n2");
    ASSERT_EQ(heartbeat.read_contexts.size(), 1u);
    ASSERT_EQ(heartbeat.read_contexts[0].id, 42u);

    auto [follower_store, follower] = make_node("n2", {"n1", "n3"});
    auto ack = follower.handle_append_entries(heartbeat);
    ASSERT_TRUE(ack.success);
    ASSERT_EQ(ack.read_acks.size(), 1u);

    ASSERT_TRUE(node.handle_append_entries_response("n2", ack));
    auto ready = node.query_read_index(42);
    ASSERT_TRUE(ready.has_value());
    ASSERT_TRUE(ready->ready);
    auto consumed = node.consume_read_index(42);
    ASSERT_TRUE(consumed.has_value());
    ASSERT_TRUE(consumed->ready);
    ASSERT_EQ(node.pending_read_count(), 0u);
}

TEST(raft_read_index_timeout_expires_pending_request) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));

    auto read = node.read_index(read_index_request{.id = 99, .context = "timeout"});
    ASSERT_TRUE(read.has_value());
    ASSERT_FALSE(read->ready);
    ASSERT_EQ(node.pending_read_count(), 1u);
    ASSERT_EQ(node.metrics().pending_reads, 1u);

    ASSERT_EQ(node.expire_read_indexes(std::chrono::milliseconds{-1}), 1u);
    ASSERT_FALSE(node.query_read_index(99).has_value());
    ASSERT_EQ(node.pending_read_count(), 0u);
}

TEST(raft_learner_replicates_without_voting_or_quorum_power) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));
    ASSERT_TRUE(node.role() == node_role::leader);

    auto learners = node.set_learners({"n4"});
    ASSERT_TRUE(learners.has_value());
    ASSERT_EQ(node.current_configuration().learners.size(), 1u);

    ASSERT_TRUE(node.handle_append_entries_response("n2", {
        .term = node.current_term(),
        .success = true,
        .match_index = learners->index,
    }));
    ASSERT_EQ(node.commit_index(), learners->index);

    auto vote = node.handle_request_vote(request_vote_request{
        .term = node.current_term() + 1,
        .candidate_id = "n4",
        .last_log_index = node.last_log_index(),
        .last_log_term = node.current_term(),
    });
    ASSERT_FALSE(vote.vote_granted);

    auto entry = node.append_command("learner-replicated");
    ASSERT_TRUE(entry.has_value());
    auto append = node.make_append_entries("n4");
    ASSERT_FALSE(append.entries.empty());
    ASSERT_TRUE(node.handle_append_entries_response("n4", {
        .term = node.current_term(),
        .success = true,
        .match_index = entry->index,
    }));
    ASSERT_EQ(node.commit_index(), learners->index);
    ASSERT_EQ(node.metrics().learners, 1u);
}

TEST(raft_learner_cannot_start_election) {
    auto store = std::make_shared<memory_store>();
    store->save_snapshot_metadata(snapshot_metadata{
        .last_included_index = 1,
        .last_included_term = 1,
        .configuration = configuration_state{
            .voters = {"n1", "n2"},
            .learners = {"n3"},
        },
    });
    raft_node node{raft_config{.id = "n3", .peers = {"n1", "n2"}}, store};

    auto req = node.begin_election();

    ASSERT_EQ(req.term, 0u);
    ASSERT_TRUE(node.role() == node_role::follower);
    ASSERT_EQ(node.current_term(), 0u);
}

TEST(raft_promotes_learner_via_joint_consensus) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));

    auto learner = node.set_learners({"n4"});
    ASSERT_TRUE(learner.has_value());
    ASSERT_TRUE(node.handle_append_entries_response("n2", {
        .term = node.current_term(),
        .success = true,
        .match_index = learner->index,
    }));

    auto promoted = node.promote_learner("n4");
    ASSERT_TRUE(promoted.has_value());
    ASSERT_TRUE(promoted->configuration.joint());
    ASSERT_EQ(promoted->configuration.learners.size(), 0u);

    ASSERT_TRUE(node.handle_append_entries_response("n2", {
        .term = node.current_term(),
        .success = true,
        .match_index = promoted->index,
    }));
    ASSERT_EQ(node.commit_index(), learner->index);

    ASSERT_TRUE(node.handle_append_entries_response("n3", {
        .term = node.current_term(),
        .success = true,
        .match_index = promoted->index,
    }));
    ASSERT_EQ(node.commit_index(), promoted->index);
    ASSERT_TRUE(node.current_configuration().joint());
    ASSERT_TRUE(node.current_configuration().learners.empty());
}

TEST(raft_remove_leader_steps_down_after_leave_joint_commits) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));
    ASSERT_TRUE(node.role() == node_role::leader);

    auto removal = node.remove_node("n1");
    ASSERT_TRUE(removal.has_value());
    ASSERT_TRUE(removal->configuration.joint());

    ASSERT_TRUE(node.handle_append_entries_response("n2", {
        .term = node.current_term(),
        .success = true,
        .match_index = removal->index,
    }));
    ASSERT_TRUE(node.handle_append_entries_response("n3", {
        .term = node.current_term(),
        .success = true,
        .match_index = removal->index,
    }));
    ASSERT_TRUE(node.role() == node_role::leader);

    auto leave = store->entry_at(removal->index + 1);
    ASSERT_TRUE(leave.has_value());
    ASSERT_FALSE(leave->configuration.joint());
    ASSERT_TRUE(node.handle_append_entries_response("n2", {
        .term = node.current_term(),
        .success = true,
        .match_index = leave->index,
    }));
    ASSERT_TRUE(node.handle_append_entries_response("n3", {
        .term = node.current_term(),
        .success = true,
        .match_index = leave->index,
    }));

    ASSERT_TRUE(node.role() == node_role::follower);
    ASSERT_FALSE(node.current_configuration().joint());
}

TEST(raft_leader_lease_and_check_quorum_step_down) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));
    ASSERT_TRUE(node.role() == node_role::leader);

    ASSERT_TRUE(node.handle_append_entries_response("n2", {
        .term = node.current_term(),
        .success = true,
        .match_index = node.last_log_index(),
    }));
    ASSERT_TRUE(node.leader_lease_valid(std::chrono::seconds{10}));
    ASSERT_FALSE(node.check_leader_quorum(std::chrono::milliseconds{-1}));
    ASSERT_TRUE(node.role() == node_role::follower);
}

TEST(raft_lease_read_requires_check_quorum) {
    auto store = std::make_shared<memory_store>();
    raft_config cfg{
        .id = "n1",
        .peers = {"n2", "n3"},
        .options = raft_options{
            .check_quorum = false,
            .lease_read = true,
        },
    };
    raft_node node{cfg, store};

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));
    ASSERT_TRUE(node.handle_append_entries_response("n2", {
        .term = node.current_term(),
        .success = true,
        .match_index = node.last_log_index(),
    }));

    auto read = node.read_index(read_index_request{.id = 7, .context = "lease"});
    ASSERT_TRUE(read.has_value());
    ASSERT_FALSE(read->ready);
}

TEST(raft_leader_transfer_to_caught_up_voter_returns_timeout_now) {
    auto [leader_store, leader] = make_node("n1", {"n2", "n3"});
    auto [follower_store, follower] = make_node("n2", {"n1", "n3"});

    leader.begin_election();
    ASSERT_TRUE(leader.handle_vote_response("n2", {
        .term = leader.current_term(),
        .vote_granted = true,
    }));
    ASSERT_TRUE(leader.role() == node_role::leader);

    auto append = leader.make_append_entries("n2");
    auto ack = follower.handle_append_entries(append);
    ASSERT_TRUE(ack.success);
    ASSERT_TRUE(leader.handle_append_entries_response("n2", ack));

    auto transfer = leader.transfer_leader("n2");

    ASSERT_TRUE(transfer.has_value());
    ASSERT_TRUE(transfer->has_value());
    ASSERT_EQ((*transfer)->term, leader.current_term());
    ASSERT_EQ((*transfer)->leader_id, std::string("n1"));
}

TEST(raft_leader_transfer_waits_until_target_catches_up) {
    auto [leader_store, leader] = make_node("n1", {"n2", "n3"});

    leader.begin_election();
    ASSERT_TRUE(leader.handle_vote_response("n2", {
        .term = leader.current_term(),
        .vote_granted = true,
    }));
    auto entry = leader.append_command("pending-transfer");
    ASSERT_TRUE(entry.has_value());

    auto transfer = leader.transfer_leader("n2");
    ASSERT_TRUE(transfer.has_value());
    ASSERT_FALSE(transfer->has_value());

    ASSERT_TRUE(leader.handle_append_entries_response("n2", {
        .term = leader.current_term(),
        .success = true,
        .match_index = leader.last_log_index(),
    }));
    auto timeout_now = leader.take_pending_leader_transfer("n2");

    ASSERT_TRUE(timeout_now.has_value());
    ASSERT_EQ(timeout_now->leader_id, std::string("n1"));
}

TEST(raft_leader_transfer_rejects_learner_target) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));
    auto learners = node.set_learners({"n4"});
    ASSERT_TRUE(learners.has_value());

    auto transfer = node.transfer_leader("n4");

    ASSERT_FALSE(transfer.has_value());
    ASSERT_TRUE(transfer.error().code == raft_errc::not_voter);
}

TEST(raft_timeout_now_starts_immediate_election) {
    auto [store, node] = make_node("n2", {"n1", "n3"});
    auto append = node.handle_append_entries(append_entries_request{
        .term = 3,
        .leader_id = "n1",
        .prev_log_index = 0,
        .prev_log_term = 0,
    });
    ASSERT_TRUE(append.success);

    auto vote = node.handle_timeout_now(timeout_now_request{
        .term = 3,
        .leader_id = "n1",
    });

    ASSERT_TRUE(vote.has_value());
    ASSERT_EQ(vote->candidate_id, std::string("n2"));
    ASSERT_EQ(vote->term, 4u);
    ASSERT_TRUE(node.role() == node_role::candidate);
}

TEST(raft_read_index_pending_request_is_cleared_on_stepdown) {
    auto [store, node] = make_node("n1", {"n2", "n3"});

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));
    auto read = node.read_index(read_index_request{.id = 700, .context = "stepdown"});
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(node.pending_read_count(), 1u);

    auto append = node.handle_append_entries(append_entries_request{
        .term = node.current_term() + 1,
        .leader_id = "n2",
        .prev_log_index = 0,
        .prev_log_term = 0,
    });

    ASSERT_TRUE(append.success);
    ASSERT_TRUE(node.role() == node_role::follower);
    ASSERT_EQ(node.pending_read_count(), 0u);
    ASSERT_FALSE(node.query_read_index(700).has_value());
}

TEST(raft_auto_snapshot_compacts_when_threshold_is_reached) {
    auto store = std::make_shared<memory_store>();
    recording_machine machine;
    raft_node node{raft_config{.id = "n1"}, store, &machine};
    node.begin_election();
    ASSERT_TRUE(node.append_command("a").has_value());
    ASSERT_TRUE(node.append_command("b").has_value());

    auto snapshotted = node.maybe_create_snapshot(raft_snapshot_policy{
        .log_entries_threshold = 2,
        .uri_prefix = "auto-test",
    });

    ASSERT_TRUE(snapshotted.has_value());
    ASSERT_TRUE(snapshotted->has_value());
    ASSERT_EQ((*snapshotted)->last_included_index, node.commit_index());
    ASSERT_EQ(store->first_log_index(), node.commit_index() + 1);
    ASSERT_TRUE(store->load_snapshot_metadata().uri.starts_with("auto-test-"));

    auto second = node.maybe_create_snapshot(raft_snapshot_policy{
        .log_entries_threshold = 1,
        .min_interval = std::chrono::hours{1},
        .uri_prefix = "auto-test",
    });
    ASSERT_TRUE(second.has_value());
    ASSERT_FALSE(second->has_value());
}

TEST(raft_snapshot_storage_failure_stops_node) {
    auto store = std::make_shared<throwing_snapshot_store>();
    recording_machine machine;
    raft_node node{raft_config{.id = "n1"}, store, &machine};

    node.begin_election();
    auto entry = node.append_command("snapshot-fail");
    ASSERT_TRUE(entry.has_value());

    auto snapshot = node.create_snapshot("broken.snapshot");

    ASSERT_FALSE(snapshot.has_value());
    ASSERT_TRUE(node.stopped());
    ASSERT_TRUE(snapshot.error().code == raft_errc::storage_error);
}

TEST(raft_tcp_transport_records_backpressure_metrics) {
    auto ctx = cnetmod::make_io_context();
    raft_tcp_transport transport{*ctx, "n1", raft_tcp_transport_options{
        .max_send_attempts = 1,
        .max_outbound_queue = 0,
    }};
    cnetmod::endpoint ep{cnetmod::ip_address{cnetmod::ipv4_address::loopback()}, 65000};
    transport.add_peer(raft_tcp_peer{.id = "n2", .address = ep});

    transport.send_append_entries("n2", append_entries_request{.term = 1, .leader_id = "n1"});
    auto metrics = transport.peer_metrics("n2");

    ASSERT_TRUE(metrics.has_value());
    ASSERT_EQ(metrics->send_failures, 1u);
    ASSERT_EQ(metrics->queued_sends, 0u);
    ASSERT_TRUE(metrics->last_error == std::make_error_code(std::errc::no_buffer_space));
}

TEST(raft_pipeline_inflight_window_sends_heartbeat_when_full) {
    auto store = std::make_shared<memory_store>();
    raft_config cfg{
        .id = "n1",
        .peers = {"n2", "n3"},
        .options = raft_options{
            .max_entries_per_append = 4,
            .max_inflight_append = 2,
        },
    };
    raft_node node{cfg, store};

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));
    for (int i = 0; i < 12; ++i)
        ASSERT_TRUE(node.append_command(std::format("cmd-{}", i)).has_value());

    auto first = node.make_append_entries("n2");
    ASSERT_EQ(first.entries.size(), 4u);
    node.mark_append_sent("n2", first.entries.back().index);
    ASSERT_TRUE(node.handle_append_entries_response("n2", {
        .term = node.current_term(),
        .success = true,
        .match_index = first.entries.back().index,
    }));

    auto second = node.make_append_entries("n2");
    ASSERT_EQ(second.entries.size(), 4u);
    node.mark_append_sent("n2", second.entries.back().index);
    auto third = node.make_append_entries("n2");
    ASSERT_EQ(third.entries.size(), 4u);
    node.mark_append_sent("n2", third.entries.back().index);

    auto heartbeat_only = node.make_append_entries("n2");
    ASSERT_TRUE(heartbeat_only.entries.empty());
    ASSERT_EQ(heartbeat_only.prev_log_index, third.entries.back().index);
}

TEST(raft_five_node_partition_restart_catches_up_lagging_followers) {
    auto s1 = std::make_shared<memory_store>();
    auto s2 = std::make_shared<memory_store>();
    auto s3 = std::make_shared<memory_store>();
    auto s4 = std::make_shared<memory_store>();
    auto s5 = std::make_shared<memory_store>();
    raft_config cfg1{.id = "n1", .peers = {"n2", "n3", "n4", "n5"}};
    raft_node n1{cfg1, s1};
    raft_node n2{raft_config{.id = "n2", .peers = {"n1", "n3", "n4", "n5"}}, s2};
    raft_node n3{raft_config{.id = "n3", .peers = {"n1", "n2", "n4", "n5"}}, s3};

    n1.begin_election();
    ASSERT_FALSE(n1.handle_vote_response("n2", {
        .term = n1.current_term(),
        .vote_granted = true,
    }));
    ASSERT_TRUE(n1.handle_vote_response("n3", {
        .term = n1.current_term(),
        .vote_granted = true,
    }));
    ASSERT_TRUE(n1.role() == node_role::leader);

    auto replicate_one = [&](raft_node& follower, const node_id& peer) {
        auto req = n1.make_append_entries(peer);
        if (!req.entries.empty())
            n1.mark_append_sent(peer, req.entries.back().index);
        auto ack = follower.handle_append_entries(req);
        ASSERT_TRUE(ack.success);
        ASSERT_TRUE(n1.handle_append_entries_response(peer, ack));
    };

    for (int i = 0; i < 25; ++i) {
        auto entry = n1.append_command(std::format("partitioned-{}", i));
        ASSERT_TRUE(entry.has_value());
        replicate_one(n2, "n2");
        replicate_one(n3, "n3");
        ASSERT_TRUE(n1.commit_index() >= entry->index);
    }

    raft_node n4{raft_config{.id = "n4", .peers = {"n1", "n2", "n3", "n5"}}, s4};
    raft_node n5{raft_config{.id = "n5", .peers = {"n1", "n2", "n3", "n4"}}, s5};
    for (int i = 0; i < 16 && (n4.commit_index() < n1.commit_index() ||
                               n5.commit_index() < n1.commit_index()); ++i) {
        replicate_one(n4, "n4");
        replicate_one(n5, "n5");
    }

    ASSERT_EQ(n4.commit_index(), n1.commit_index());
    ASSERT_EQ(n5.commit_index(), n1.commit_index());
    ASSERT_EQ(s4->entry_at(n1.commit_index())->command, std::string("partitioned-24"));
    ASSERT_EQ(s5->entry_at(n1.commit_index())->command, std::string("partitioned-24"));
}

TEST(raft_snapshot_retention_removes_old_files_beyond_keep_last) {
    auto ctx = cnetmod::make_io_context();
    auto dir = std::filesystem::temp_directory_path() /
        std::format("cnetmod-raft-retention-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(dir);

    raft_tcp_transport transport{*ctx, "n1", raft_tcp_transport_options{
        .snapshot_directory = dir,
        .snapshot_retention = raft_snapshot_retention_options{
            .keep_last = 2,
            .min_age = std::chrono::seconds{0},
        },
    }};

    bool cleaned = false;
    auto scenario = [&]() -> cnetmod::task<void> {
        for (int i = 0; i < 4; ++i) {
            auto path = dir / std::format("n1-{}-1-snap.snapshot", i);
            co_await touch_file_async(*ctx, path);
            std::filesystem::last_write_time(path,
                std::filesystem::file_time_type::clock::now() - std::chrono::seconds{10 - i});
        }
        auto removed = co_await transport.cleanup_snapshot_files();
        cleaned = removed && *removed == 2u;
        ctx->stop();
    };
    cnetmod::spawn(*ctx, scenario());
    ctx->run();

    ASSERT_TRUE(cleaned);
    std::size_t remaining = 0;
    for (const auto& item : std::filesystem::directory_iterator{dir}) {
        if (item.path().extension() == ".snapshot")
            ++remaining;
    }
    ASSERT_EQ(remaining, 2u);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(raft_tcp_transport_rejects_bad_auth_token) {
    cnetmod::net_init net;
    auto ctx = cnetmod::make_io_context();
    auto ep1 = reserve_loopback_endpoint();
    auto ep2 = reserve_loopback_endpoint();
    auto store = std::make_shared<memory_store>();
    raft_node follower{raft_config{.id = "n2", .peers = {"n1"}}, store};

    raft_tcp_transport sender{*ctx, "n1", raft_tcp_transport_options{
        .retry_backoff = std::chrono::milliseconds{1},
        .security = raft_tcp_security_options{
            .shared_secret = "good",
            .require_auth_token = true,
        },
    }};
    raft_tcp_transport receiver{*ctx, "n2", raft_tcp_transport_options{
        .retry_backoff = std::chrono::milliseconds{1},
        .security = raft_tcp_security_options{
            .shared_secret = "bad",
            .require_auth_token = true,
        },
    }};
    sender.add_peer(raft_tcp_peer{.id = "n2", .address = ep2});
    receiver.add_peer(raft_tcp_peer{.id = "n1", .address = ep1});
    raft_node_runtime runtime{
        *ctx,
        follower,
        receiver,
        ep2,
        raft_options{},
        raft_runtime_options{.auto_election = false, .auto_heartbeat = false},
    };

    auto scenario = [&]() -> cnetmod::task<void> {
        runtime.start();
        (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{30});
        sender.send_append_entries("n2", append_entries_request{
            .term = 7,
            .leader_id = "n1",
            .prev_log_index = 0,
            .prev_log_term = 0,
        });
        (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{80});
        runtime.stop();
        ctx->stop();
    };
    cnetmod::spawn(*ctx, scenario());
    ctx->run();

    ASSERT_EQ(follower.current_term(), 0u);
    ASSERT_EQ(follower.commit_index(), 0u);
}

TEST(raft_tcp_transport_tls_requires_configured_context) {
    auto ctx = cnetmod::make_io_context();
    raft_tcp_transport transport{*ctx, "n1", raft_tcp_transport_options{
        .max_send_attempts = 1,
        .retry_backoff = std::chrono::milliseconds{1},
        .security = raft_tcp_security_options{
            .enable_tls = true,
        },
    }};
    cnetmod::endpoint ep{cnetmod::ip_address{cnetmod::ipv4_address::loopback()}, 65001};
    transport.add_peer(raft_tcp_peer{.id = "n2", .address = ep});

    bool done = false;
    auto scenario = [&]() -> cnetmod::task<void> {
        transport.send_append_entries("n2", append_entries_request{
            .term = 1,
            .leader_id = "n1",
            .entries = {},
        });
        (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{20});
        done = true;
        ctx->stop();
    };
    cnetmod::spawn(*ctx, scenario());
    ctx->run();

    auto metrics = transport.peer_metrics("n2");
    ASSERT_TRUE(done);
    ASSERT_TRUE(metrics.has_value());
    ASSERT_EQ(metrics->send_failures, 1u);
    ASSERT_TRUE(metrics->last_error == std::make_error_code(std::errc::protocol_not_supported));
}

TEST(raft_tcp_transport_mtls_authenticates_peer_certificate) {
#ifdef CNETMOD_HAS_SSL
    cnetmod::net_init net;
    auto ctx = cnetmod::make_io_context();
    auto ep1 = reserve_loopback_endpoint();
    auto ep2 = reserve_loopback_endpoint();
    const auto dir = std::filesystem::temp_directory_path() /
        std::format("cnetmod-raft-mtls-{}", std::chrono::steady_clock::now().time_since_epoch().count());

    bool files_ok = false;
    bool contexts_ok = false;
    bool received = false;
    bool metrics_ok = false;

    auto scenario = [&]() -> cnetmod::task<void> {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            ctx->stop();
            co_return;
        }

        const auto ca_path = dir / "ca.pem";
        const auto n1_cert_path = dir / "n1.pem";
        const auto n1_key_path = dir / "n1.key";
        const auto n2_cert_path = dir / "n2.pem";
        const auto n2_key_path = dir / "n2.key";

        files_ok =
            co_await write_text_file_async(*ctx, ca_path, raft_tls_test_material::ca_cert) &&
            co_await write_text_file_async(*ctx, n1_cert_path, raft_tls_test_material::n1_cert) &&
            co_await write_text_file_async(*ctx, n1_key_path, raft_tls_test_material::n1_key) &&
            co_await write_text_file_async(*ctx, n2_cert_path, raft_tls_test_material::n2_cert) &&
            co_await write_text_file_async(*ctx, n2_key_path, raft_tls_test_material::n2_key);
        if (!files_ok) {
            ctx->stop();
            co_return;
        }

        auto client_ctx_r = cnetmod::ssl_context::client();
        auto server_ctx_r = cnetmod::ssl_context::server();
        if (!client_ctx_r || !server_ctx_r) {
            ctx->stop();
            co_return;
        }
        auto client_ctx = std::move(*client_ctx_r);
        auto server_ctx = std::move(*server_ctx_r);

        contexts_ok =
            client_ctx.load_cert_file(n1_cert_path.string()).has_value() &&
            client_ctx.load_key_file(n1_key_path.string()).has_value() &&
            client_ctx.load_ca_file(ca_path.string()).has_value() &&
            server_ctx.load_cert_file(n2_cert_path.string()).has_value() &&
            server_ctx.load_key_file(n2_key_path.string()).has_value() &&
            server_ctx.load_ca_file(ca_path.string()).has_value();
        if (!contexts_ok) {
            ctx->stop();
            co_return;
        }
        client_ctx.set_verify_peer(true);
        server_ctx.set_verify_peer(true);

        raft_options options;
        auto s1 = std::make_shared<memory_store>();
        auto s2 = std::make_shared<memory_store>();
        raft_node n1{raft_config{.id = "n1", .peers = {"n2"}, .options = options}, s1};
        raft_node n2{raft_config{.id = "n2", .peers = {"n1"}, .options = options}, s2};

        raft_tcp_transport t1{*ctx, "n1", raft_tcp_transport_options{
            .max_send_attempts = 1,
            .retry_backoff = std::chrono::milliseconds{1},
            .security = raft_tcp_security_options{
                .enable_tls = true,
                .require_peer_certificate = true,
                .peer_certificate_sha256 = {{"n2", std::string{raft_tls_test_material::n2_fp}}},
                .client_tls = &client_ctx,
            },
        }};
        raft_tcp_transport t2{*ctx, "n2", raft_tcp_transport_options{
            .max_send_attempts = 1,
            .retry_backoff = std::chrono::milliseconds{1},
            .security = raft_tcp_security_options{
                .enable_tls = true,
                .require_peer_certificate = true,
                .peer_certificate_sha256 = {{"n1", std::string{raft_tls_test_material::n1_fp}}},
                .server_tls = &server_ctx,
            },
        }};
        t1.add_peer(raft_tcp_peer{.id = "n2", .address = ep2});
        t2.add_peer(raft_tcp_peer{.id = "n1", .address = ep1});

        raft_runtime_options runtime_options{.auto_election = false, .auto_heartbeat = false};
        raft_node_runtime r2{*ctx, n2, t2, ep2, options, runtime_options};
        r2.start();
        (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{30});

        t1.send_append_entries("n2", append_entries_request{
            .term = 3,
            .leader_id = "n1",
            .prev_log_index = 0,
            .prev_log_term = 0,
        });
        for (auto i = 0; i < 50; ++i) {
            if (n2.current_term() == 3) {
                received = true;
                break;
            }
            (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{10});
        }

        auto metrics = t1.peer_metrics("n2");
        metrics_ok = metrics.has_value() &&
                     metrics->send_successes == 1 &&
                     metrics->last_error == std::error_code{};
        r2.stop();
        ctx->stop();
    };

    cnetmod::spawn(*ctx, scenario());
    ctx->run();

    std::error_code cleanup_ec;
    std::filesystem::remove_all(dir, cleanup_ec);

    ASSERT_TRUE(files_ok);
    ASSERT_TRUE(contexts_ok);
    ASSERT_TRUE(received);
    ASSERT_TRUE(metrics_ok);
#else
    ASSERT_TRUE(true);
#endif
}

TEST(raft_tcp_three_node_loopback_replicates_and_commits_command) {
    cnetmod::net_init net;
    auto ctx = cnetmod::make_io_context();
    auto ep1 = reserve_loopback_endpoint();
    auto ep2 = reserve_loopback_endpoint();
    auto ep3 = reserve_loopback_endpoint();

    raft_options options{
        .election_timeout = std::chrono::milliseconds{10},
        .heartbeat_interval = std::chrono::milliseconds{10},
        .leader_lease_timeout = std::chrono::seconds{5},
        .pre_vote = false,
        .check_quorum = false,
    };

    auto s1 = std::make_shared<memory_store>();
    auto s2 = std::make_shared<memory_store>();
    auto s3 = std::make_shared<memory_store>();
    raft_node n1{raft_config{.id = "n1", .peers = {"n2", "n3"}, .options = options}, s1};
    raft_node n2{raft_config{.id = "n2", .peers = {"n1", "n3"}, .options = options}, s2};
    raft_node n3{raft_config{.id = "n3", .peers = {"n1", "n2"}, .options = options}, s3};

    raft_tcp_transport t1{*ctx, "n1", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    raft_tcp_transport t2{*ctx, "n2", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    raft_tcp_transport t3{*ctx, "n3", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    t1.add_peer(raft_tcp_peer{.id = "n2", .address = ep2});
    t1.add_peer(raft_tcp_peer{.id = "n3", .address = ep3});
    t2.add_peer(raft_tcp_peer{.id = "n1", .address = ep1});
    t2.add_peer(raft_tcp_peer{.id = "n3", .address = ep3});
    t3.add_peer(raft_tcp_peer{.id = "n1", .address = ep1});
    t3.add_peer(raft_tcp_peer{.id = "n2", .address = ep2});

    raft_runtime_options runtime_options{
        .auto_election = false,
        .auto_heartbeat = false,
    };
    raft_node_runtime r1{*ctx, n1, t1, ep1, options, runtime_options};
    raft_node_runtime r2{*ctx, n2, t2, ep2, options, runtime_options};
    raft_node_runtime r3{*ctx, n3, t3, ep3, options, runtime_options};

    bool elected = false;
    bool replicated = false;
    bool metrics_ok = false;
    auto scenario = [&]() -> cnetmod::task<void> {
        r1.start();
        r2.start();
        r3.start();
        (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{40});

        r1.tick_now();
        for (auto i = 0; i < 50 && n1.role() != node_role::leader; ++i)
            (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{10});
        elected = n1.role() == node_role::leader;

        if (elected) {
            auto entry = n1.append_command("tcp-commit");
            if (entry) {
                for (auto i = 0; i < 80; ++i) {
                    r1.tick_now();
                    r2.tick_now();
                    r3.tick_now();
                    if (n1.commit_index() >= entry->index &&
                        n2.commit_index() >= entry->index &&
                        n3.commit_index() >= entry->index) {
                        replicated = true;
                        break;
                    }
                    (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{10});
                }
            }
        }

        auto m2 = t1.peer_metrics("n2");
        auto m3 = t1.peer_metrics("n3");
        metrics_ok = m2.has_value() && m3.has_value() &&
                     (m2->send_successes + m3->send_successes) != 0;
        r1.stop();
        r2.stop();
        r3.stop();
        ctx->stop();
    };

    cnetmod::spawn(*ctx, scenario());
    ctx->run();

    ASSERT_TRUE(elected);
    ASSERT_TRUE(replicated);
    ASSERT_TRUE(metrics_ok);
    ASSERT_EQ(s2->entry_at(2)->command, std::string("tcp-commit"));
    ASSERT_EQ(s3->entry_at(2)->command, std::string("tcp-commit"));
}

TEST(raft_tcp_five_node_partition_heal_catches_up_lagging_followers) {
    cnetmod::net_init net;
    auto ctx = cnetmod::make_io_context();
    std::array<cnetmod::endpoint, 5> endpoints{
        reserve_loopback_endpoint(),
        reserve_loopback_endpoint(),
        reserve_loopback_endpoint(),
        reserve_loopback_endpoint(),
        reserve_loopback_endpoint(),
    };
    std::array<std::string, 5> ids{"n1", "n2", "n3", "n4", "n5"};

    raft_options options{
        .election_timeout = std::chrono::milliseconds{10},
        .heartbeat_interval = std::chrono::milliseconds{10},
        .leader_lease_timeout = std::chrono::seconds{5},
        .pre_vote = false,
        .check_quorum = false,
    };

    auto s1 = std::make_shared<memory_store>();
    auto s2 = std::make_shared<memory_store>();
    auto s3 = std::make_shared<memory_store>();
    auto s4 = std::make_shared<memory_store>();
    auto s5 = std::make_shared<memory_store>();
    raft_node n1{raft_config{.id = "n1", .peers = {"n2", "n3", "n4", "n5"}, .options = options}, s1};
    raft_node n2{raft_config{.id = "n2", .peers = {"n1", "n3", "n4", "n5"}, .options = options}, s2};
    raft_node n3{raft_config{.id = "n3", .peers = {"n1", "n2", "n4", "n5"}, .options = options}, s3};
    raft_node n4{raft_config{.id = "n4", .peers = {"n1", "n2", "n3", "n5"}, .options = options}, s4};
    raft_node n5{raft_config{.id = "n5", .peers = {"n1", "n2", "n3", "n4"}, .options = options}, s5};

    raft_tcp_transport t1{*ctx, "n1", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    raft_tcp_transport t2{*ctx, "n2", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    raft_tcp_transport t3{*ctx, "n3", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    raft_tcp_transport t4{*ctx, "n4", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    raft_tcp_transport t5{*ctx, "n5", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    std::array<raft_tcp_transport*, 5> transports{&t1, &t2, &t3, &t4, &t5};

    auto connect_all = [&] {
        for (std::size_t i = 0; i < transports.size(); ++i) {
            for (std::size_t j = 0; j < transports.size(); ++j) {
                if (i == j) continue;
                transports[i]->add_peer(raft_tcp_peer{.id = ids[j], .address = endpoints[j]});
            }
        }
    };
    auto disconnect_between = [&](std::span<const std::size_t> left,
                                  std::span<const std::size_t> right) {
        for (auto l : left)
            for (auto r : right)
                transports[l]->remove_peer(ids[r]);
        for (auto r : right)
            for (auto l : left)
                transports[r]->remove_peer(ids[l]);
    };
    connect_all();

    raft_runtime_options runtime_options{.auto_election = false, .auto_heartbeat = false};
    raft_node_runtime r1{*ctx, n1, t1, endpoints[0], options, runtime_options};
    raft_node_runtime r2{*ctx, n2, t2, endpoints[1], options, runtime_options};
    raft_node_runtime r3{*ctx, n3, t3, endpoints[2], options, runtime_options};
    raft_node_runtime r4{*ctx, n4, t4, endpoints[3], options, runtime_options};
    raft_node_runtime r5{*ctx, n5, t5, endpoints[4], options, runtime_options};

    bool elected = false;
    bool majority_committed = false;
    bool healed = false;
    bool finished = false;
    bool timed_out = false;
    log_index last_index = 0;
    auto scenario = [&]() -> cnetmod::task<void> {
        r1.start();
        r2.start();
        r3.start();
        r4.start();
        r5.start();
        (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{40});

        r1.tick_now();
        for (auto i = 0; i < 80 && n1.role() != node_role::leader; ++i)
            (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{10});
        elected = n1.role() == node_role::leader;

        if (elected) {
            std::array majority{std::size_t{0}, std::size_t{1}, std::size_t{2}};
            std::array minority{std::size_t{3}, std::size_t{4}};
            disconnect_between(majority, minority);

            for (auto i = 0; i < 25; ++i) {
                auto entry = n1.append_command(std::format("tcp-partition-{}", i));
                if (entry) last_index = entry->index;
            }
            for (auto i = 0; i < 120; ++i) {
                r1.tick_now();
                if (n1.commit_index() >= last_index &&
                    n2.commit_index() >= last_index &&
                    n3.commit_index() >= last_index) {
                    majority_committed = true;
                    break;
                }
                (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{10});
            }

            connect_all();
            for (auto i = 0; i < 160; ++i) {
                r1.tick_now();
                if (n4.commit_index() >= last_index &&
                    n5.commit_index() >= last_index) {
                    healed = true;
                    break;
                }
                (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{10});
            }
        }

        r1.stop();
        r2.stop();
        r3.stop();
        r4.stop();
        r5.stop();
        finished = true;
        ctx->stop();
    };
    auto watchdog = [&]() -> cnetmod::task<void> {
        (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::seconds{5});
        if (!finished) {
            timed_out = true;
            r1.stop();
            r2.stop();
            r3.stop();
            r4.stop();
            r5.stop();
            ctx->stop();
        }
    };

    cnetmod::spawn(*ctx, scenario());
    cnetmod::spawn(*ctx, watchdog());
    ctx->run();

    ASSERT_FALSE(timed_out);
    ASSERT_TRUE(elected);
    ASSERT_TRUE(majority_committed);
    ASSERT_TRUE(healed);
    ASSERT_EQ(s4->entry_at(last_index)->command, std::string("tcp-partition-24"));
    ASSERT_EQ(s5->entry_at(last_index)->command, std::string("tcp-partition-24"));
}

TEST(raft_tcp_five_node_seeded_chaos_fuzz_converges_after_heal) {
    cnetmod::net_init net;
    auto ctx = cnetmod::make_io_context();
    std::array<cnetmod::endpoint, 5> endpoints{
        reserve_loopback_endpoint(),
        reserve_loopback_endpoint(),
        reserve_loopback_endpoint(),
        reserve_loopback_endpoint(),
        reserve_loopback_endpoint(),
    };
    std::array<std::string, 5> ids{"n1", "n2", "n3", "n4", "n5"};

    raft_options options{
        .election_timeout = std::chrono::milliseconds{10},
        .heartbeat_interval = std::chrono::milliseconds{10},
        .leader_lease_timeout = std::chrono::seconds{5},
        .pre_vote = false,
        .check_quorum = false,
        .max_entries_per_append = 32,
    };

    auto s1 = std::make_shared<memory_store>();
    auto s2 = std::make_shared<memory_store>();
    auto s3 = std::make_shared<memory_store>();
    auto s4 = std::make_shared<memory_store>();
    auto s5 = std::make_shared<memory_store>();
    raft_node n1{raft_config{.id = "n1", .peers = {"n2", "n3", "n4", "n5"}, .options = options}, s1};
    raft_node n2{raft_config{.id = "n2", .peers = {"n1", "n3", "n4", "n5"}, .options = options}, s2};
    raft_node n3{raft_config{.id = "n3", .peers = {"n1", "n2", "n4", "n5"}, .options = options}, s3};
    raft_node n4{raft_config{.id = "n4", .peers = {"n1", "n2", "n3", "n5"}, .options = options}, s4};
    raft_node n5{raft_config{.id = "n5", .peers = {"n1", "n2", "n3", "n4"}, .options = options}, s5};

    raft_tcp_transport t1{*ctx, "n1", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    raft_tcp_transport t2{*ctx, "n2", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    raft_tcp_transport t3{*ctx, "n3", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    raft_tcp_transport t4{*ctx, "n4", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    raft_tcp_transport t5{*ctx, "n5", raft_tcp_transport_options{.retry_backoff = std::chrono::milliseconds{1}}};
    std::array<raft_tcp_transport*, 5> transports{&t1, &t2, &t3, &t4, &t5};
    std::array<raft_node*, 5> nodes{&n1, &n2, &n3, &n4, &n5};
    std::array<std::shared_ptr<memory_store>, 5> stores{s1, s2, s3, s4, s5};
    std::array<std::array<bool, 5>, 5> connected{};

    auto connect_link = [&](std::size_t from, std::size_t to) {
        if (from == to || connected[from][to]) return;
        transports[from]->add_peer(raft_tcp_peer{.id = ids[to], .address = endpoints[to]});
        connected[from][to] = true;
    };
    auto disconnect_link = [&](std::size_t from, std::size_t to) {
        if (from == to || !connected[from][to]) return;
        transports[from]->remove_peer(ids[to]);
        connected[from][to] = false;
    };
    auto connect_all = [&] {
        for (std::size_t i = 0; i < transports.size(); ++i)
            for (std::size_t j = 0; j < transports.size(); ++j)
                connect_link(i, j);
    };
    auto disconnect_all = [&] {
        for (std::size_t i = 0; i < transports.size(); ++i)
            for (std::size_t j = 0; j < transports.size(); ++j)
                disconnect_link(i, j);
    };
    connect_all();

    raft_runtime_options runtime_options{.auto_election = false, .auto_heartbeat = false};
    raft_node_runtime r1{*ctx, n1, t1, endpoints[0], options, runtime_options};
    raft_node_runtime r2{*ctx, n2, t2, endpoints[1], options, runtime_options};
    raft_node_runtime r3{*ctx, n3, t3, endpoints[2], options, runtime_options};
    raft_node_runtime r4{*ctx, n4, t4, endpoints[3], options, runtime_options};
    raft_node_runtime r5{*ctx, n5, t5, endpoints[4], options, runtime_options};
    std::array<raft_node_runtime*, 5> runtimes{&r1, &r2, &r3, &r4, &r5};

    bool elected = false;
    bool converged = false;
    bool finished = false;
    bool timed_out = false;
    log_index last_index = 0;
    std::size_t appended = 0;
    auto scenario = [&]() -> cnetmod::task<void> {
        for (auto* runtime : runtimes)
            runtime->start();
        (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{40});

        r1.tick_now();
        for (auto i = 0; i < 80 && n1.role() != node_role::leader; ++i)
            (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{5});
        elected = n1.role() == node_role::leader;

        if (elected) {
            std::mt19937_64 rng{0xc0ffee5eedULL};
            for (auto round = 0; round < 160; ++round) {
                auto from = static_cast<std::size_t>(rng() % ids.size());
                auto to = static_cast<std::size_t>(rng() % ids.size());
                if (from != to) {
                    if ((rng() % 100) < 35)
                        disconnect_link(from, to);
                    else
                        connect_link(from, to);
                }

                const std::array<std::size_t, 2> majority_followers{
                    static_cast<std::size_t>((round % 2) + 1),
                    static_cast<std::size_t>(((round + 1) % 2) + 1),
                };
                for (auto peer : majority_followers) {
                    connect_link(0, peer);
                    connect_link(peer, 0);
                }

                if (round % 3 == 0) {
                    auto entry = n1.append_command(std::format("chaos-{}", appended));
                    if (entry) {
                        last_index = entry->index;
                        ++appended;
                    }
                }

                r1.tick_now();
                (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{2});
            }

            disconnect_all();
            (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{50});
            connect_all();
            for (auto i = 0; i < 1000; ++i) {
                r1.tick_now();
                if (last_index != 0 &&
                    std::ranges::all_of(nodes, [last_index](raft_node* node) {
                        return node->commit_index() >= last_index;
                    })) {
                    converged = true;
                    break;
                }
                (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::milliseconds{5});
            }
        }

        for (auto* runtime : runtimes)
            runtime->stop();
        finished = true;
        ctx->stop();
    };
    auto watchdog = [&]() -> cnetmod::task<void> {
        (void)co_await cnetmod::async_timer_wait(*ctx, std::chrono::seconds{30});
        if (!finished) {
            timed_out = true;
            for (auto* runtime : runtimes)
                runtime->stop();
            ctx->stop();
        }
    };

    cnetmod::spawn(*ctx, scenario());
    cnetmod::spawn(*ctx, watchdog());
    ctx->run();

    ASSERT_FALSE(timed_out);
    ASSERT_TRUE(elected);
    ASSERT_TRUE(appended > 20);
    ASSERT_TRUE(converged);
    for (std::size_t i = 1; i < stores.size(); ++i) {
        auto entry = stores[i]->entry_at(last_index);
        ASSERT_TRUE(entry.has_value());
        ASSERT_EQ(entry->command, std::format("chaos-{}", appended - 1));
    }
}

TEST(raft_wire_round_trips_snapshot_payload) {
    raft_rpc_message msg{
        .type = raft_rpc_type::install_snapshot,
        .from = "n1",
        .to = "n2",
        .snapshot = install_snapshot_request{
            .term = 3,
            .leader_id = "n1",
            .metadata = snapshot_metadata{
                .last_included_index = 9,
                .last_included_term = 2,
                .configuration = configuration_state{.voters = {"n1", "n2", "n3"}},
            },
            .uri = "leader.snapshot",
            .data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
        },
    };

    auto frame = encode_raft_message(msg);
    auto decoded = decode_raft_message(frame);

    ASSERT_EQ(static_cast<int>(decoded.type), static_cast<int>(raft_rpc_type::install_snapshot));
    ASSERT_EQ(decoded.snapshot.metadata.last_included_index, 9u);
    ASSERT_EQ(decoded.snapshot.data.size(), 3u);
    ASSERT_EQ(static_cast<int>(decoded.snapshot.data[1]), 2);
}

TEST(raft_wire_round_trips_snapshot_chunk_metadata) {
    raft_rpc_message msg{
        .type = raft_rpc_type::install_snapshot,
        .from = "n1",
        .to = "n2",
        .snapshot = install_snapshot_request{
            .term = 5,
            .leader_id = "n1",
            .metadata = snapshot_metadata{
                .last_included_index = 64,
                .last_included_term = 4,
                .configuration = configuration_state{.voters = {"n1", "n2", "n3"}},
            },
            .snapshot_id = "snap-64",
            .offset = 1024,
            .total_size = 4096,
            .chunk_crc32 = 0x11223344u,
            .file_crc32 = 0x55667788u,
            .done = false,
            .data = {std::byte{0xaa}, std::byte{0xbb}},
        },
    };

    auto decoded = decode_raft_message(encode_raft_message(msg));

    ASSERT_EQ(decoded.snapshot.snapshot_id, std::string("snap-64"));
    ASSERT_EQ(decoded.snapshot.offset, 1024u);
    ASSERT_EQ(decoded.snapshot.total_size, 4096u);
    ASSERT_EQ(decoded.snapshot.chunk_crc32, 0x11223344u);
    ASSERT_EQ(decoded.snapshot.file_crc32, 0x55667788u);
    ASSERT_FALSE(decoded.snapshot.done);
    ASSERT_EQ(decoded.snapshot.data.size(), 2u);
}

TEST(raft_wire_round_trips_snapshot_response_resume_metadata) {
    raft_rpc_message msg{
        .type = raft_rpc_type::install_snapshot_response,
        .from = "n2",
        .to = "n1",
        .snapshot_response = install_snapshot_response{
            .term = 8,
            .success = false,
            .snapshot_id = "snap-64",
            .accepted_offset = 2048,
            .error = "out-of-order snapshot chunk",
        },
    };

    auto decoded = decode_raft_message(encode_raft_message(msg));

    ASSERT_EQ(static_cast<int>(decoded.type),
              static_cast<int>(raft_rpc_type::install_snapshot_response));
    ASSERT_EQ(decoded.snapshot_response.term, 8u);
    ASSERT_FALSE(decoded.snapshot_response.success);
    ASSERT_EQ(decoded.snapshot_response.snapshot_id, std::string("snap-64"));
    ASSERT_EQ(decoded.snapshot_response.accepted_offset, 2048u);
    ASSERT_EQ(decoded.snapshot_response.error, std::string("out-of-order snapshot chunk"));
}

TEST(raft_leader_switches_to_snapshot_when_follower_is_behind_compaction) {
    auto store = std::make_shared<memory_store>();
    store->save_snapshot_metadata(snapshot_metadata{
        .last_included_index = 12,
        .last_included_term = 3,
        .configuration = configuration_state{.voters = {"n1", "n2", "n3"}},
    });
    raft_node node{raft_config{.id = "n1", .peers = {"n2", "n3"}}, store};

    node.begin_election();
    ASSERT_TRUE(node.handle_vote_response("n2", {
        .term = node.current_term(),
        .vote_granted = true,
    }));
    ASSERT_TRUE(node.role() == node_role::leader);

    ASSERT_FALSE(node.handle_append_entries_response("n2", {
        .term = node.current_term(),
        .success = false,
        .match_index = 0,
        .conflict_index = 1,
    }));
    ASSERT_TRUE(node.should_send_snapshot("n2"));

    auto snapshot = node.make_install_snapshot("n2");
    ASSERT_EQ(snapshot.metadata.last_included_index, 12u);
    node.mark_snapshot_sent("n2");
    ASSERT_TRUE(node.handle_install_snapshot_response("n2", {
        .term = node.current_term(),
        .success = true,
    }));

    auto next = node.make_append_entries("n2");
    ASSERT_EQ(next.prev_log_index, 12u);
    ASSERT_FALSE(node.should_send_snapshot("n2"));
}

TEST(raft_fault_injection_dropped_append_does_not_commit_until_retry) {
    auto [leader_store, leader] = make_node("n1", {"n2", "n3"});
    auto [follower_store, follower] = make_node("n2", {"n1", "n3"});

    leader.begin_election();
    ASSERT_TRUE(leader.handle_vote_response("n2", {
        .term = leader.current_term(),
        .vote_granted = true,
    }));
    auto entry = leader.append_command("must-survive-retry");
    ASSERT_TRUE(entry.has_value());

    // First AppendEntries is injected as a network drop: no follower handling, no ack.
    ASSERT_EQ(leader.commit_index(), 0u);

    auto retry = leader.make_append_entries("n2");
    auto ack = follower.handle_append_entries(retry);
    ASSERT_TRUE(ack.success);
    ASSERT_TRUE(leader.handle_append_entries_response("n2", ack));
    ASSERT_EQ(leader.commit_index(), entry->index);
}

#ifdef CNETMOD_HAS_LEVELDB
TEST(raft_leveldb_recovers_hard_state_and_log_after_restart) {
    auto dir = std::filesystem::temp_directory_path() /
        std::format("cnetmod-raft-test-{}", std::chrono::steady_clock::now().time_since_epoch().count());

    {
        auto store = std::make_shared<leveldb_store>(dir.string());
        raft_node node{raft_config{.id = "n1"}, store};
        node.begin_election();
        auto entry = node.append_command("persisted");
        ASSERT_TRUE(entry.has_value());
        ASSERT_EQ(node.commit_index(), entry->index);
    }

    {
        auto store = std::make_shared<leveldb_store>(dir.string());
        raft_node recovered{raft_config{.id = "n1"}, store};
        ASSERT_EQ(recovered.current_term(), 1u);
        ASSERT_EQ(recovered.commit_index(), 2u);
        ASSERT_TRUE(store->entry_at(2).has_value());
        ASSERT_EQ(store->entry_at(2)->command, std::string("persisted"));
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
#endif

RUN_TESTS()
