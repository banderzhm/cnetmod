# SOCKS5 Examples

Complete examples demonstrating SOCKS5 client and server usage.

## Server Example

The server example demonstrates a multi-core SOCKS5 proxy server with:
- Multiple worker threads for handling connections
- Username/password authentication
- Connection statistics
- Configurable connection limits

### Running the Server

```bash
# Run with default number of workers (hardware concurrency)
./socks5_server_demo

# Run with specific number of workers
./socks5_server_demo 4

# Run with 8 worker threads
./socks5_server_demo 8
```

### Server Configuration

The server supports two authentication methods:
- **No authentication**: Enabled by default
- **Username/Password**: 
  - `admin` / `secret` (admin user)
  - `user` / `pass` (regular user)

Default settings:
- Listen address: `0.0.0.0:1080`
- Max connections: 1000
- Statistics reporting: Every 10 seconds

## Client Examples

The client example includes 4 different scenarios:

### Example 1: HTTP Request through SOCKS5

Demonstrates making an HTTP request through the SOCKS5 proxy:
- Connects to proxy with authentication
- Connects to httpbin.org
- Sends HTTP GET request
- Receives and displays response

```bash
./socks5_client_demo 1
# or
./socks5_client_demo http
```

### Example 2: No Authentication

Shows connecting without authentication:
- Connects to proxy without credentials
- Connects to example.com

```bash
./socks5_client_demo 2
# or
./socks5_client_demo noauth
```

### Example 3: IP Address Connection

Demonstrates connecting using IP address instead of domain:
- Connects using admin credentials
- Connects to 8.8.8.8:53 (Google DNS)

```bash
./socks5_client_demo 3
# or
./socks5_client_demo ip
```

### Example 4: Error Handling

Shows various error scenarios:
- Connection to non-existent proxy
- Wrong authentication credentials
- Connection to unreachable host

```bash
./socks5_client_demo 4
# or
./socks5_client_demo error
```

### Run All Examples

```bash
# Run all examples sequentially
./socks5_client_demo
```

## Testing with Standard Tools

You can test the SOCKS5 server with standard command-line tools:

### Using curl

```bash
# No authentication
curl --socks5 127.0.0.1:1080 http://httpbin.org/ip

# With username/password
curl --socks5 user:pass@127.0.0.1:1080 http://httpbin.org/ip
curl --socks5 admin:secret@127.0.0.1:1080 http://httpbin.org/ip

# HTTPS through SOCKS5
curl --socks5 127.0.0.1:1080 https://httpbin.org/ip
```

### Using ssh

```bash
# SSH through SOCKS5 proxy
ssh -o ProxyCommand='nc -X 5 -x 127.0.0.1:1080 %h %p' user@remote-host
```

### Using Firefox

1. Open Firefox Settings
2. Go to Network Settings
3. Select "Manual proxy configuration"
4. Set SOCKS Host: `127.0.0.1`, Port: `1080`
5. Select "SOCKS v5"
6. Check "Proxy DNS when using SOCKS v5"

### Using Chrome/Chromium

```bash
# Launch Chrome with SOCKS5 proxy
chrome --proxy-server="socks5://127.0.0.1:1080"

# With authentication (requires extension)
chrome --proxy-server="socks5://user:pass@127.0.0.1:1080"
```

## Performance Testing

### Using Apache Bench through SOCKS5

```bash
# Install proxychains
sudo apt-get install proxychains

# Configure proxychains (/etc/proxychains.conf)
# Add: socks5 127.0.0.1 1080

# Run benchmark
proxychains ab -n 1000 -c 10 http://httpbin.org/ip
```

### Connection Stress Test

```bash
# Test with multiple concurrent connections
for i in {1..100}; do
  curl --socks5 127.0.0.1:1080 http://httpbin.org/ip &
done
wait
```

## Architecture

### Server Architecture

```
┌─────────────────────────────────────────┐
│         Accept Thread (Main)            │
│  - Listen on 0.0.0.0:1080              │
│  - Accept connections                   │
│  - Dispatch to workers                  │
└──────────────┬──────────────────────────┘
               │
               ├──────────┬──────────┬──────────┐
               ▼          ▼          ▼          ▼
         ┌─────────┐┌─────────┐┌─────────┐┌─────────┐
         │Worker 1 ││Worker 2 ││Worker 3 ││Worker N │
         │         ││         ││         ││         │
         │ Handle  ││ Handle  ││ Handle  ││ Handle  │
         │ Auth    ││ Auth    ││ Auth    ││ Auth    │
         │ Request ││ Request ││ Request ││ Request │
         │ Relay   ││ Relay   ││ Relay   ││ Relay   │
         └─────────┘└─────────┘└─────────┘└─────────┘
```

### Client Flow

```
1. Connect to Proxy
   ↓
2. Authentication Negotiation
   ↓
3. Send Credentials (if needed)
   ↓
4. Send CONNECT Request
   ↓
5. Receive Response
   ↓
6. Data Transfer (bidirectional)
```

## Troubleshooting

### Server won't start

```bash
# Check if port is already in use
netstat -tuln | grep 1080
lsof -i :1080

# Kill existing process
kill $(lsof -t -i:1080)
```

### Client connection fails

1. Verify server is running: `netstat -tuln | grep 1080`
2. Check firewall rules: `sudo iptables -L`
3. Test with telnet: `telnet 127.0.0.1 1080`

### Authentication fails

- Verify credentials match server configuration
- Check server logs for authentication attempts
- Ensure `allow_username_password` is enabled

### Slow performance

- Increase worker thread count
- Check network latency to target hosts
- Monitor CPU and memory usage
- Adjust `max_connections` limit

## Security Considerations

⚠️ **Important**: These examples are for demonstration purposes only!

For production use:
- Use strong authentication credentials
- Implement rate limiting
- Add IP whitelisting/blacklisting
- Enable logging and monitoring
- Use TLS for authentication (not implemented)
- Implement access control lists
- Add connection timeout handling
- Implement proper DNS resolution
- Add support for BIND and UDP ASSOCIATE commands

## Next Steps

- Implement BIND command for reverse connections
- Add UDP ASSOCIATE for UDP proxying
- Implement GSSAPI authentication
- Add DNS resolution support
- Implement connection pooling
- Add bandwidth limiting
- Implement access control rules
- Add comprehensive logging
