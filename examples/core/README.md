# Core Examples

Basic functionality demonstrations of cnetmod framework.

## Examples

### async_file.cpp
Demonstrates asynchronous file I/O operations:
- Reading files asynchronously
- Writing files asynchronously
- File operations with io_context

### serial_port.cpp
Serial port communication example:
- Opening and configuring serial ports
- Async read/write operations
- Cross-platform serial communication (Windows/Linux)

### timer_demo.cpp
Timer and delay operations:
- Using async timers
- Scheduling delayed tasks
- Timer cancellation

### echo_server.cpp
Basic TCP echo server:
- Accepting connections
- Reading and echoing data
- Connection handling

### ssl_echo_server.cpp
SSL/TLS encrypted echo server:
- SSL context setup
- Secure connections
- Certificate handling

## Building

```bash
cd build
cmake --build . --target <example_name>
```

## Running

```bash
./core/<example_name>
```
