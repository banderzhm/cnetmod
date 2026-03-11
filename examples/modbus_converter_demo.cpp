import cnetmod.utils;
import std;

using namespace utils::conv;

void demo_endian() {
    std::println("=== Endian Conversion Demo ===");
    
    // 16-bit conversion
    std::uint16_t value16 = 0x1234;
    std::uint16_t be16 = hton(value16);
    std::println("Original: 0x{:04X}, Big-endian: 0x{:04X}", value16, be16);
    
    // 32-bit conversion
    std::uint32_t value32 = 0x12345678;
    std::uint32_t be32 = hton(value32);
    std::println("Original: 0x{:08X}, Big-endian: 0x{:08X}", value32, be32);
    
    // Read from buffer
    std::array<std::uint8_t, 4> buffer = {0x12, 0x34, 0x56, 0x78};
    std::uint16_t read16 = read_be16(buffer);
    std::uint32_t read32 = read_be32(buffer);
    std::println("Read 16-bit: 0x{:04X}, 32-bit: 0x{:08X}", read16, read32);
    
    // Write to buffer
    std::array<std::uint8_t, 4> output = {};
    write_be16(output, 0xABCD, 0);
    write_be16(output, 0xEF01, 2);
    std::println("Written buffer: {:02X} {:02X} {:02X} {:02X}", 
                output[0], output[1], output[2], output[3]);
    std::println();
}

void demo_register_conversion() {
    std::println("=== Register Data Conversion Demo ===");
    
    // 16-bit integer
    std::int16_t int16_val = -1234;
    std::uint16_t reg = RegisterConverter::from_int16(int16_val);
    std::int16_t back = RegisterConverter::to_int16(reg);
    std::println("int16: {} -> register: 0x{:04X} -> back: {}", int16_val, reg, back);
    
    // 32-bit integer (high word first)
    std::int32_t int32_val = 0x12345678;
    auto regs32 = RegisterConverter::from_int32_hilo(int32_val);
    std::int32_t back32 = RegisterConverter::to_int32_hilo(regs32[0], regs32[1]);
    std::println("int32: 0x{:08X} -> registers: [0x{:04X}, 0x{:04X}] -> back: 0x{:08X}", 
                int32_val, regs32[0], regs32[1], back32);
    
    // Float conversion
    float float_val = 3.14159f;
    auto regs_float = RegisterConverter::from_float_hilo(float_val);
    float back_float = RegisterConverter::to_float_hilo(regs_float[0], regs_float[1]);
    std::println("float: {} -> registers: [0x{:04X}, 0x{:04X}] -> back: {}", 
                float_val, regs_float[0], regs_float[1], back_float);
    
    // Double precision float
    double double_val = 3.141592653589793;
    auto regs_double = RegisterConverter::from_double_hilo(double_val);
    double back_double = RegisterConverter::to_double_hilo(
        regs_double[0], regs_double[1], regs_double[2], regs_double[3]);
    std::println("double: {} -> registers: [0x{:04X}, 0x{:04X}, 0x{:04X}, 0x{:04X}] -> back: {}", 
                double_val, regs_double[0], regs_double[1], 
                regs_double[2], regs_double[3], back_double);
    std::println();
}

void demo_bit_operations() {
    std::println("=== Bit Operations Demo ===");
    
    // Bit operations
    std::uint16_t value = 0b0000000000000000;
    value = BitOps::set_bit(value, 0, true);
    value = BitOps::set_bit(value, 3, true);
    value = BitOps::set_bit(value, 7, true);
    std::println("Set bits 0, 3, 7: 0b{:016b} (0x{:04X})", value, value);
    
    for (int i = 0; i < 8; ++i) {
        bool bit = BitOps::get_bit(value, i);
        std::print("Bit {}: {} ", i, bit ? 1 : 0);
    }
    std::println();
    
    // Bit array conversion
    std::array<bool, 9> bits = {true, false, true, true, false, false, true, false, true};
    auto buffer = BitOps::from_bits(bits);
    std::print("Bits to buffer: ");
    for (auto byte : buffer) {
        std::print("0x{:02X} ", byte);
    }
    std::println();
    
    auto bits_back = BitOps::to_bits(buffer, bits.size());
    std::print("Buffer to bits: ");
    for (auto bit : bits_back) {
        std::print("{}", bit ? 1 : 0);
    }
    std::println("\n");
}

void demo_crc16() {
    std::println("=== CRC16 Checksum Demo ===");
    
    // Calculate CRC
    std::vector<std::uint8_t> data = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    std::uint16_t crc = CRC16::calculate(data);
    std::println("Data: 01 03 00 00 00 0A");
    std::println("CRC16: 0x{:04X} (low byte: 0x{:02X}, high byte: 0x{:02X})", 
                crc, crc & 0xFF, crc >> 8);
    
    // Append CRC
    std::vector<std::uint8_t> data_with_crc = data;
    CRC16::append(data_with_crc);
    std::print("Data with CRC: ");
    for (auto byte : data_with_crc) {
        std::print("{:02X} ", byte);
    }
    std::println();
    
    // Verify CRC
    bool valid = CRC16::verify_and_remove(data_with_crc);
    std::println("CRC verification: {}", valid ? "passed" : "failed");
    std::println();
}

void demo_hex_conversion() {
    std::println("=== Hexadecimal Conversion Demo ===");
    
    // Buffer to hex string
    std::vector<std::uint8_t> data = {0x01, 0x03, 0x00, 0x0A, 0xFF, 0x12};
    std::string hex_upper = Hex::encode(data, true);
    std::string hex_lower = Hex::encode(data, false);
    std::println("Buffer: 01 03 00 0A FF 12");
    std::println("Uppercase hex: {}", hex_upper);
    std::println("Lowercase hex: {}", hex_lower);
    
    // Hex string to buffer
    std::string hex_str = "0103000AFF12";
    auto buffer = Hex::decode(hex_str);
    std::print("Hex string '{}' to buffer: ", hex_str);
    for (auto byte : buffer) {
        std::print("{:02X} ", byte);
    }
    std::println("\n");
}

void demo_modbus_frame() {
    std::println("=== Modbus RTU Frame Demo ===");
    
    // Build Modbus RTU read holding registers request
    // Slave address: 0x01
    // Function code: 0x03 (read holding registers)
    // Start address: 0x0000
    // Register count: 0x000A (10)
    std::vector<std::uint8_t> frame;
    frame.push_back(0x01);  // Slave address
    frame.push_back(0x03);  // Function code
    
    // Start address (big-endian)
    std::array<std::uint8_t, 2> addr_buffer = {};
    write_be16(addr_buffer, 0x0000);
    frame.insert(frame.end(), addr_buffer.begin(), addr_buffer.end());
    
    // Register count (big-endian)
    std::array<std::uint8_t, 2> count_buffer = {};
    write_be16(count_buffer, 0x000A);
    frame.insert(frame.end(), count_buffer.begin(), count_buffer.end());
    
    // Append CRC
    CRC16::append(frame);
    
    std::println("Modbus RTU Request Frame:");
    std::print("  Hex: ");
    for (auto byte : frame) {
        std::print("{:02X} ", byte);
    }
    std::println();
    std::println("  Slave address: 0x{:02X}", frame[0]);
    std::println("  Function code: 0x{:02X}", frame[1]);
    std::println("  Start address: 0x{:04X}", read_be16(std::span(frame).subspan(2, 2)));
    std::println("  Register count: 0x{:04X}", read_be16(std::span(frame).subspan(4, 2)));
    std::println("  CRC: 0x{:02X}{:02X}", frame[7], frame[6]);
    std::println();
}

int main() {
    try {
        std::println("Modbus Converter Demo\n");
        
        demo_endian();
        demo_register_conversion();
        demo_bit_operations();
        demo_crc16();
        demo_hex_conversion();
        demo_modbus_frame();
        
        std::println("All demos completed!");
        return 0;
    } catch (const std::exception& e) {
        std::println( "Error: {}", e.what());
        return 1;
    }
}