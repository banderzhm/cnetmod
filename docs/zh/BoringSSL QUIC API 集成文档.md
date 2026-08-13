# BoringSSL QUIC API 集成文档

## SSL_set_quic_method() 回调详解

BoringSSL 通过 `SSL_set_quic_method()` 接口将 TLS 1.3 握手流程与 QUIC 传输层解耦。开发者注册一组回调函数指针结构体，实现密钥交换和握手数据的自定义处理。

### 回调注册方式

```c
static const SSL_QUIC_METHOD quic_methods = {
    set_read_secret,        // 安装读密钥
    set_write_secret,       // 安装写密钥
    add_handshake_data,     // 向 QUIC 层传递握手消息
    flush_flight,           // 发送缓存的握手包
    send_alert,             // 发送 TLS 告警
};

// 在创建 SSL 对象时注册
SSL_set_quic_method(ssl, &quic_methods);
```

**核心设计理念**：
- **零拷贝传递**：密钥材料直接传递指针，不额外拷贝
- **异步解耦**：握手数据通过回调异步传递给 QUIC 传输层
- **分层抽象**：TLS 引擎只负责协议逻辑，加密/解密由 QUIC 层执行

---

## 各回调的作用时机

### 1. set_read_secret(level, cipher, secret)

**触发时机**：当 TLS 引擎完成密钥推导，需要安装对端的读密钥时触发。

**参数详解**：

```cpp
enum ssl_encryption_level_t {
    ssl_encryption_initial = 0,      // Initial 层（ClientHello）
    ssl_encryption_early_data = 1,   // 0-RTT 早数据（受控、显式 opt-in）
    ssl_encryption_handshake = 2,    // Handshake 层（ServerHello/Cert）
    ssl_encryption_application = 3   // 1-RTT 应用层
};

// 典型触发序列（Client 视角）：
// Initial → Handshake（ServerHello 之后）
// Handshake → Application（Server Finished 之后）

// 典型触发序列（Server 视角）：
// Initial（ClientHello 之后）
// Handshake（Client Finished 之后）
// Application（Server 发送 Finished 之后）
```

**实现要点**：

```cpp
int set_read_secret(SSL* ssl, ssl_encryption_level_t level,
                    const SSL_CIPHER* cipher, const uint8_t* secret,
                    size_t secret_len) {
    
    auto session = static_cast<quic_tls_session*>(SSL_get_app_data(ssl));
    
    // 存储密钥材料（供后续 Header Protection 使用）
    session->install_read_key(level, cipher, 
                               std::span<const uint8_t>{secret, secret_len});
    
    // 记录日志
    logger::debug("Install read key at level={}, cipher={}, secret_len={}",
                  level_name(level), SSL_CIPHER_get_name(cipher), secret_len);
    
    // 验证密钥推导正确性
    if (level == ssl_encryption_handshake && !session->handshake_keys_installed_) {
        session->handshake_keys_installed_ = true;
        session->on_handshake_key_available();
    }
    
    return 1;  // 成功
}
```

**密钥存储结构**：

```cpp
struct quic_tls_session {
    struct level_keys {
        std::array<uint8_t, 32> key_;      // AEAD 密钥
        std::array<uint8_t, 32> iv_;       // 初始化向量
        std::array<uint8_t, 16> hp_key_;   // Header Protection 密钥
        const SSL_CIPHER* cipher_ = nullptr;
    };
    
    std::array<level_keys, 4> keys_;  // 按 level 索引
    
    void install_read_key(ssl_encryption_level_t level, 
                          const SSL_CIPHER* cipher,
                          std::span<const uint8_t> secret) {
        auto& keys = keys_[level];
        keys.cipher_ = cipher;
        
        // 从 secret 派生 key, iv, hp_key（HKDF-SHA256）
        hkdf_expand_label(keys.key_, secret, "quic key", 32);
        hkdf_expand_label(keys.iv_, secret, "quic iv", 12);
        hkdf_expand_label(keys.hp_key_, secret, "quic hp", 16);
    }
};
```

---

### 2. set_write_secret(level, cipher, secret)

**触发时机**：TLS 引擎生成本地写密钥时触发。

**与 read_secret 的区别**：

```cpp
// read_secret: 安装对端的读密钥（用于解密对方发送的数据）
// write_secret: 安装本端的写密钥（用于加密发送给对方的数据）

// 两者使用相同的密钥推导机制，但方向相反
int set_write_secret(SSL* ssl, ssl_encryption_level_t level,
                     const SSL_CIPHER* cipher, const uint8_t* secret,
                     size_t secret_len) {
    
    auto session = static_cast<quic_tls_session*>(SSL_get_app_data(ssl));
    
    // 存储写密钥
    session->install_write_key(level, cipher,
                                std::span<const uint8_t>{secret, secret_len});
    
    // 在 write_secret 安装后才能发送加密数据
    if (level == ssl_encryption_application) {
        session->application_keys_ready_ = true;
    }
    
    return 1;
}
```

**典型使用场景**：

```cpp
// Client 发送加密的 ClientHello（Initial 层）
void send_initial_client_hello() {
    // 1. SSL 生成 ClientHello 数据
    // 2. 触发 set_write_secret(initial)
    // 3. 使用 Initial 密钥加密 ClientHello 包
    // 4. 通过 add_handshake_data() 获取加密数据
    // 5. 通过 flush_flight() 发送到网络
}
```

---

### 3. add_handshake_data(level, data, len)

**触发时机**：TLS 引擎需要将 handshake 消息传递给 QUIC 传输层时调用。

**典型流程**：

```cpp
int add_handshake_data(SSL* ssl, ssl_encryption_level_t level,
                       const uint8_t* data, size_t len) {
    
    auto session = static_cast<quic_tls_session*>(SSL_get_app_data(ssl));
    
    // 将 handshake 消息缓存到 CRYPTO frame
    auto offset = session->get_next_offset(level);
    session->crypto_frames_[level].push_back({
        .offset = offset,
        .data = std::vector<std::byte>{
            reinterpret_cast<const std::byte*>(data),
            reinterpret_cast<const std::byte*>(data) + len}
    });
    
    logger::debug("Add handshake data: level={}, offset={}, length={}",
                  level_name(level), offset, len);
    
    return 1;
}

// 内部实现
class quic_tls_session {
    struct crypto_frame {
        size_t offset;
        std::vector<std::byte> data;
    };
    
    std::map<ssl_encryption_level_t, std::vector<crypto_frame>> crypto_frames_;
    
    size_t get_next_offset(ssl_encryption_level_t level) {
        auto& frames = crypto_frames_[level];
        if (frames.empty()) return 0;
        return frames.back().offset + frames.back().data.size();
    }
};
```

**批量收集逻辑**：

```cpp
// 收集所有 pending handshake 消息并打包发送
void flush_pending_handshake_data() {
    for (const auto& [level, frames] : crypto_frames_) {
        for (const auto& frame : frames) {
            // 构造 QUIC CRYPTO frame
            crypto_packet pkt{
                .type = get_packet_type(level),
                .dcid = dcid_,
                .scid = scid_,
                .crypto_offset = frame.offset,
                .crypto_data = frame.data
            };
            
            send_packet(pkt);
        }
    }
    crypto_frames_.clear();
}
```

---

### 4. flush_flight()

**触发时机**：TLS 引擎要求发送所有未确认的 handshake 消息时调用。

**典型用途**：

```cpp
int flush_flight(SSL* ssl) {
    auto session = static_cast<quic_tls_session*>(SSL_get_app_data(ssl));
    
    // 批量发送 pending packets
    session->flush_pending_handshake_data();
    
    // 减少网络往返：合并多个 handshake 包到单个 UDP 帧
    if (session->can_coalesce_packets()) {
        session->coalesce_pending_packets();
    }
    
    logger::debug("Flush flight: {} packets sent", session->packets_in_flight());
    
    return 1;
}
```

**性能优化**：

```cpp
void coalesce_pending_packets() {
    // 合并多个小握手包到单个 UDP 帧
    std::vector<std::byte> coalesced;
    
    for (auto& pkt : pending_packets_) {
        if (coalesced.size() + pkt.encoded_size() > 1500) {
            // 发送当前帧
            send_udp_datagram(coalesced);
            coalesced.clear();
        }
        pkt.encode_to(coalesced);
    }
    
    if (!coalesced.empty()) {
        send_udp_datagram(coalesced);
    }
    
    pending_packets_.clear();
}
```

---

### 5. send_alert(level, code)

**触发时机**：TLS 握手失败或收到非法消息时触发。

**典型用法**：

```cpp
int send_alert(SSL* ssl, ssl_encryption_level_t level, uint8_t alert_code) {
    auto session = static_cast<quic_tls_session*>(SSL_get_app_data(ssl));
    
    // 记录详细错误信息
    switch (alert_code) {
        case SSL_AD_BAD_CERTIFICATE:
            logger::error("TLS alert: bad certificate at level {}", level);
            break;
        case SSL_AD_CERTIFICATE_EXPIRED:
            logger::error("TLS alert: certificate expired");
            break;
        case SSL_AD_UNKNOWN_CA:
            logger::error("TLS alert: unknown CA");
            break;
        case SSL_AD_PROTOCOL_VERSION:
            logger::error("TLS alert: protocol version mismatch");
            break;
        case SSL_AD_HANDSHAKE_FAILURE:
            logger::error("TLS alert: handshake failure");
            break;
        default:
            logger::error("TLS alert: code={} at level={}", alert_code, level);
    }
    
    // 发送 CONNECTION_CLOSE frame 通知对端
    session->send_connection_close(
        quic_errc::crypto_exchange_error,
        fmt::format("TLS alert: {}", alert_code)
    );
    
    return 1;
}
```

**错误码映射**：

```cpp
quic_error_code map_tls_alert_to_quic_error(uint8_t alert) {
    switch (alert) {
        case SSL_AD_BAD_CERTIFICATE:
        case SSL_AD_UNSUPPORTED_CERTIFICATE:
        case SSL_AD_CERTIFICATE_REVOKED:
        case SSL_AD_CERTIFICATE_EXPIRED:
        case SSL_AD_CERTIFICATE_UNKNOWN:
            return quic_errc::certificate_required;
            
        case SSL_AD_DECRYPT_ERROR:
        case SSL_AD_HANDSHAKE_FAILURE:
            return quic_errc::crypto_exchange_error;
            
        default:
            return quic_errc::protocol_violation;
    }
}
```

---

## Transport Parameters 编码格式

### Client Hello 中的 ClientHello Extensions

**TLV 编码格式**：

```
Type = 0x0039 (quic_transport_parameters)
Length = varint (2 bytes)
Value = sequence of {
    parameter_id: varint
    parameter_length: varint
    parameter_value: byte[length]
}
```

**C++ 实现**：

```cpp
void encode_transport_parameters(std::vector<std::byte>& buffer,
                                  const transport_params& params) {
    auto start = buffer.size();
    
    // 预留 extension 头和长度
    append_varint(buffer, 0x0039);  // extension type
    size_t length_pos = buffer.size();
    append_varint(buffer, 0);  // placeholder for length
    
    // 编码各个参数
    auto encode_param = [&](uint64_t id, auto value) {
        append_varint(buffer, id);
        if constexpr (std::is_same_v<decltype(value), bool>) {
            append_varint(buffer, 0);  // length 0 for flag
        } else if constexpr (std::is_integral_v<decltype(value)>) {
            auto value_start = buffer.size();
            append_varint(buffer, varint_size(value));
            append_varint(buffer, value);
        } else {
            append_varint(buffer, value.size());
            std::copy(value.begin(), value.end(), std::back_inserter(buffer));
        }
    };
    
    // 必须参数
    encode_param(0x0000, params.original_destination_connection_id);
    encode_param(0x0001, params.max_idle_timeout);
    encode_param(0x0002, params.stateless_reset_token);
    encode_param(0x0003, params.max_udp_payload_size);
    encode_param(0x0004, params.initial_max_data);
    encode_param(0x0005, params.initial_max_stream_data_bidi_local);
    encode_param(0x0006, params.initial_max_stream_data_bidi_remote);
    encode_param(0x0007, params.initial_max_stream_data_uni);
    encode_param(0x0008, params.initial_max_streams_bidi);
    encode_param(0x0009, params.initial_max_streams_uni);
    encode_param(0x000A, params.ack_delay_exponent);
    encode_param(0x000B, params.max_ack_delay);
    encode_param(0x000C, params.disable_active_migration);
    encode_param(0x000F, params.initial_source_connection_id);
    
    // 回填长度
    auto end = buffer.size();
    auto length = end - length_pos - varint_size(0);
    overwrite_varint(buffer, length_pos, length);
}
```

**常见参数值**：

```cpp
transport_params client_params{
    .max_idle_timeout = 30000,           // 30 秒空闲超时
    .max_udp_payload_size = 1500,        // 标准 MTU
    .initial_max_data = 1048576,         // 1MB 初始窗口
    .initial_max_stream_data_bidi_local = 16384,   // 16KB
    .initial_max_stream_data_bidi_remote = 16384,  // 16KB
    .initial_max_stream_data_uni = 16384,          // 16KB
    .initial_max_streams_bidi = 100,     // 100 并发双向流
    .initial_max_streams_uni = 10,       // 10 并发单播流
    .ack_delay_exponent = 3,             // 默认指数
    .max_ack_delay = 25,                 // 25ms 最大 ACK 延迟
    .disable_active_migration = false,   // 允许连接迁移
};
```

---

### Server Hello 中的 Server Hello Extensions

**新增必含参数**：

```cpp
transport_params server_params = client_params;  // 继承客户端值

// 新增 Server 专属参数
server_params.original_destination_connection_id = server_dcid;  // Anti-amplification
server_params.stateless_reset_token = generate_random_token(16); // 16 bytes token
server_params.preferred_address = preferred_address_opt;         // 可选迁移地址
server_params.retry_token = generate_retry_token(client_addr);   // 重试令牌
```

**Anti-amplification 机制**：

```cpp
// Server 发送的字节数不能超过接收的 3 倍
void validate_amplification_limit(const quic_connection& conn) {
    auto received_bytes = conn.bytes_received();
    auto sent_bytes = conn.bytes_sent();
    
    if (sent_bytes > 3 * received_bytes) {
        // 触发 amplification limit，等待 client 响应
        conn.set_amplification_limited(true);
    }
}
```

---

## Header Protection/AEAD 加密流程

### Header Protection（短包头保护）

**保护目标**：隐藏 Packet Number 和 Key Phase bit，防止流量分析。

**算法流程**：

```cpp
struct packet_protector {
    // HP_KEY: Header Protection 密钥（16 字节）
    std::array<uint8_t, 16> hp_key_;
    
    // AEAD 密钥（用于载荷加密）
    std::array<uint8_t, 32> aead_key_;
    std::array<uint8_t, 12> aead_iv_;
    
    void protect_header(std::vector<std::byte>& packet, size_t pn_offset) {
        // 1. 提取 sample（从 protected header 的第 4 字节之后开始）
        //    对于短包头：sample = payload[pn_offset + 4 : pn_offset + 20]
        auto sample = std::span{packet}.subspan(pn_offset + 4, 16);
        
        // 2. 使用 HP 密钥进行 AES-128-ECB 加密
        std::array<uint8_t, 16> mask;
        AES_128_ECB_encrypt(hp_key_.data(), sample.data(), mask.data());
        
        // 3. XOR 掩码应用于 PN 和 Flags 字段
        //    header[0] 的 bit 0-1（packet number length）
        //    header[1..4] 的 packet number
        
        // 获取 fixed bit mask
        uint8_t fixed_bit_mask = (packet[0] & 0x80) ? 0x0F : 0x1F;
        
        // 应用掩码
        packet[0] ^= std::byte{mask[0] & fixed_bit_mask};
        
        // 加密 packet number
        size_t pn_len = (packet[0] & 0x03) + 1;
        for (size_t i = 0; i < pn_len; ++i) {
            packet[pn_offset + i] ^= std::byte{mask[1 + i]};
        }
    }
    
    void remove_header_protection(std::vector<std::byte>& packet, size_t pn_offset) {
        // 反向操作
        auto sample = std::span{packet}.subspan(pn_offset + 4, 16);
        
        std::array<uint8_t, 16> mask;
        AES_128_ECB_decrypt(hp_key_.data(), sample.data(), mask.data());
        
        // 恢复原始 header
        uint8_t fixed_bit_mask = (packet[0] & 0x80) ? 0x0F : 0x1F;
        packet[0] ^= std::byte{mask[0] & fixed_bit_mask};
        
        size_t pn_len = (packet[0] & 0x03) + 1;
        for (size_t i = 0; i < pn_len; ++i) {
            packet[pn_offset + i] ^= std::byte{mask[1 + i]};
        }
    }
};
```

---

### Packet Protection（载荷加密）

**AEAD 加密模式**：

```cpp
struct packet_protection {
    // nonce = IV XOR (packet_number padded to 12 bytes)
    std::array<uint8_t, 12> compute_nonce(uint64_t packet_number) const {
        std::array<uint8_t, 12> nonce = aead_iv_;
        
        // 将 packet_number 转换为 big-endian 并 XOR 到 IV 末尾
        for (int i = 0; i < 8; ++i) {
            nonce[4 + i] ^= static_cast<uint8_t>((packet_number >> (8 * (7 - i))) & 0xFF);
        }
        
        return nonce;
    }
    
    // 加密载荷
    std::vector<std::byte> encrypt(uint64_t packet_number,
                                    std::span<const std::byte> plaintext,
                                    std::span<const std::byte> aad) {
        auto nonce = compute_nonce(packet_number);
        
        // AEAD 加密（AES-128-GCM 或 ChaCha20-Poly1305）
        std::vector<std::byte> ciphertext(plaintext.size() + 16);  // 16 字节 tag
        
        if (is_aes_gcm()) {
            AES_GCM_encrypt(aead_key_.data(), nonce.data(),
                           aad.data(), aad.size(),
                           plaintext.data(), plaintext.size(),
                           ciphertext.data(), ciphertext.data() + plaintext.size());
        } else {
            ChaCha20_Poly1305_encrypt(aead_key_.data(), nonce.data(),
                                       aad.data(), aad.size(),
                                       plaintext.data(), plaintext.size(),
                                       ciphertext.data(), ciphertext.data() + plaintext.size());
        }
        
        return ciphertext;
    }
    
    // 解密载荷
    expected<std::vector<std::byte>, error_code> decrypt(
            uint64_t packet_number,
            std::span<const std::byte> ciphertext,
            std::span<const std::byte> aad) {
        
        auto nonce = compute_nonce(packet_number);
        std::vector<std::byte> plaintext(ciphertext.size() - 16);
        
        bool success = false;
        if (is_aes_gcm()) {
            success = AES_GCM_decrypt(aead_key_.data(), nonce.data(),
                                      aad.data(), aad.size(),
                                      ciphertext.data(), ciphertext.size(),
                                      plaintext.data());
        } else {
            success = ChaCha20_Poly1305_decrypt(aead_key_.data(), nonce.data(),
                                                 aad.data(), aad.size(),
                                                 ciphertext.data(), ciphertext.size(),
                                                 plaintext.data());
        }
        
        if (!success) {
            return unexpected(quic_errc::aead_limit_reached);
        }
        
        return plaintext;
    }
};
```

---

## BoringSSL 版本兼容性说明

### 最低要求

**BoringSSL ≥ 1.1.1**（含完整 QUIC API 支持）

### API 变更历史

| 版本 | 变更 |
|------|------|
| 2020.x | `SSL_set_quic_method()` 引入 |
| 2021.x | `SSL_quic_read_level()` 加入 |
| 2022.x | `SSL_provide_quic_data()` 优化 |
| 2023.x | `SSL_set_quic_early_data_context()` 增强 |

### 版本检测宏

```cmake
# CMakeLists.txt 中检测 BoringSSL 版本
find_package(BoringSSL REQUIRED)

if(BORINGSSL_VERSION_STRING VERSION_GREATER_EQUAL "3.0")
    message(STATUS "Using modern BoringSSL with full QUIC support")
    add_definitions(-DCNETMOD_BORINGSSL_MODERN)
elseif(BORINGSSL_VERSION_STRING VERSION_GREATER_EQUAL "2.0")
    message(STATUS "Using BoringSSL 2.x (compatible)")
    add_definitions(-DCNETMOD_BORINGSSL_V2)
else()
    message(WARNING "BoringSSL version < 2.0, some QUIC features disabled")
    add_definitions(-DCNETMOD_BORINGSSL_LEGACY)
endif()
```

**条件编译代码**：

```cpp
#if defined(CNETMOD_BORINGSSL_MODERN)
    // 使用最新 API
    SSL_set_quic_early_data_context(ssl, early_data_ctx, early_data_ctx_len);
#elif defined(CNETMOD_BORINGSSL_V2)
    // 兼容模式
    SSL_set_quic_early_data_context(ssl, early_data_ctx, early_data_ctx_len);
#else
    // 旧版本回退
    // 0-RTT 功能不可用
#endif
```

---

## 已知限制与规避方案

### 限制 1: 无标准 OpenSSL QUIC 支持

**现状**：
- OpenSSL 3.0 引入了 QUIC API，但实现不完整
- 生产环境必须使用 BoringSSL

**规避方案**：

```cmake
# 强制检查 BoringSSL
if(DEFINED OPENSSL_VERSION AND NOT DEFINED BORINGSSL_VERSION)
    message(FATAL_ERROR "OpenSSL does not support QUIC API. Please use BoringSSL.")
endif()
```

### 限制 2: API 仍在演进

**现状**：
- BoringSSL QUIC API 尚未冻结
- 不同版本 API 可能有细微差异

**规避方案**：

```cpp
// 封装适配层隔离变化
class quic_tls_adapter {
    std::unique_ptr<SSL, decltype(&SSL_free)> ssl_;
    
    // 统一接口
    int set_quic_method(const SSL_QUIC_METHOD* method) {
#if defined(CNETMOD_BORINGSSL_MODERN)
        return SSL_set_quic_method(ssl_.get(), method);
#else
        return SSL_set_quic_method(ssl_.get(), method);  // 旧版本
#endif
    }
};
```

---

*本文档为 cnetmod BoringSSL QUIC 集成指南 v1.0-MVP，最后更新：2026-08-03*
