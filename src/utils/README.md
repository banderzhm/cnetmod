# Protocol Conversion Utilities

This module provides data conversion utilities commonly used in industrial protocols (Modbus, Profinet, OPC UA, etc.) and binary communication.

## Relationship with `cnetmod.core.buffer`

- **`cnetmod.core.buffer`**: Focused on buffer management and stream-oriented I/O operations with `buffer_reader`/`buffer_writer`
- **`cnetmod.utils.conv`**: Focused on protocol-level data type conversions and transformations

Use `buffer` for network I/O, use `conv` for protocol data encoding/decoding.

## Modules

### 1. Endian - Endianness Conversion

Handles big-endian/little-endian conversions. Most industrial protocols use big-endian (network byte order).

```cpp
import cnetmod.utils;
using namespace utils::conv;

// 16-bit conversion
uint16_t value = 0x1234;
uint16_t big_endian = Endian::host_to_be16(value);
uint16_t host = Endian::be16_to_host(big_endian);

// Read from buffer
std::array<uint8_t, 2> buffer = {0x12, 0x34};
uint16_t value = Endian::read_be16(buffer);

// Write to buffer
std::array<uint8_t, 2> output;
Endian::write_be16(output, 0x1234);
```

### 2. RegisterConverter - Register Data Conversion

Modbus registers are 16-bit. This class converts between various data types and registers.

#### Supported Data Types:
- int16_t / uint16_t (single register)
- int32_t / uint32_t (two registers)
- float (two registers)
- double (four registers)

#### Byte Order Options:
- `_hilo`: High word first (standard Modbus)
- `_lohi`: Low word first (some devices)

```cpp
// 32-bit integer to registers
int32_t value = 0x12345678;
auto regs = RegisterConverter::from_int32_hilo(value);
// regs[0] = 0x1234, regs[1] = 0x5678

// Registers to 32-bit integer
int32_t back = RegisterConverter::to_int32_hilo(regs[0], regs[1]);

// Float conversion
float f = 3.14159f;
auto float_regs = RegisterConverter::from_float_hilo(f);
float back_f = RegisterConverter::to_float_hilo(float_regs[0], float_regs[1]);
```

### 3. BitOps - Bit Manipulation

Handles coil and discrete input operations.

```cpp
// Bit operations
uint16_t value = 0;
value = BitOps::set_bit(value, 3, true);  // Set bit 3
bool bit = BitOps::get_bit(value, 3);     // Read bit 3

// Bit array to buffer conversion
std::vector<bool> bits = {true, false, true, true, false};
auto buffer = BitOps::from_bits(bits);
auto bits_back = BitOps::to_bits(buffer, bits.size());

// Calculate buffer size
size_t buffer_size = BitOps::calc_buffer_size(100);  // 13 bytes
```

### 4. CRC16 - Cyclic Redundancy Check

Modbus RTU uses CRC16 checksum.

```cpp
// Calculate CRC
std::vector<uint8_t> data = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
uint16_t crc = CRC16::calculate(data);

// Append CRC to data
CRC16::append(data);  // Automatically adds CRC (low byte first)

// Verify and remove CRC
bool valid = CRC16::verify_and_remove(data);
```

### 5. Hex - Hexadecimal Conversion

For debugging and logging.

```cpp
// Buffer to hex string
std::vector<uint8_t> data = {0x01, 0x03, 0xFF};
std::string hex = Hex::encode(data);  // "0103FF"
std::string hex_lower = Hex::encode(data, false);  // "0103ff"

// Hex string to buffer
auto buffer = Hex::decode("0103FF");
```

## Modbus Function Code Mapping

| Function Code | Function | Data Type | Tool |
|---------------|----------|-----------|------|
| 0x01 | Read Coils | Bits | BitOps |
| 0x02 | Read Discrete Inputs | Bits | BitOps |
| 0x03 | Read Holding Registers | 16-bit registers | RegisterConverter |
| 0x04 | Read Input Registers | 16-bit registers | RegisterConverter |
| 0x05 | Write Single Coil | Bit | BitOps |
| 0x06 | Write Single Register | 16-bit register | RegisterConverter |
| 0x0F | Write Multiple Coils | Bit array | BitOps |
| 0x10 | Write Multiple Registers | 16-bit register array | RegisterConverter |

## Complete Example: Build Modbus RTU Frame

```cpp
import cnetmod.utils;
using namespace utils::conv;

// Read holding registers request
std::vector<uint8_t> build_read_holding_registers(
    uint8_t slave_addr, 
    uint16_t start_addr, 
    uint16_t count) 
{
    std::vector<uint8_t> frame;
    
    // Slave address
    frame.push_back(slave_addr);
    
    // Function code 0x03
    frame.push_back(0x03);
    
    // Start address (big-endian)
    std::array<uint8_t, 2> addr_buffer = {};
    Endian::write_be16(addr_buffer, start_addr);
    frame.insert(frame.end(), addr_buffer.begin(), addr_buffer.end());
    
    // Register count (big-endian)
    std::array<uint8_t, 2> count_buffer = {};
    Endian::write_be16(count_buffer, count);
    frame.insert(frame.end(), count_buffer.begin(), count_buffer.end());
    
    // Append CRC
    CRC16::append(frame);
    
    return frame;
}

// Usage
auto request = build_read_holding_registers(0x01, 0x0000, 10);
// Result: 01 03 00 00 00 0A C5 CD
```

## Performance Characteristics

- All conversion functions are `constexpr` or inline, zero runtime overhead
- Uses `std::bit_cast` for type punning, compliant with C++20 standard
- Uses `std::span` to avoid unnecessary copies
- CRC calculation can be further optimized with lookup tables (current implementation is simple but fast enough)

## Compilation Requirements

- C++20 or higher
- Compiler with C++20 module support
