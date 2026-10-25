# TCP peer-to-peer CSP

1. Client connects to the server node and tells it its own CSP address.
   The "hello" packet may contain additional information, such as an authorization password.
2. The server routes CSP pakcets with a matching address to the client's socket.
3. The route is removed when the client disconnects (or when a new client connects with the same CSP address)

MTU is 500, payloads use network byte order

## "Hello" packet format (16 bytes)

- `u16` - frame length, excluding the length field (= 14)
- `u8` - client's CSP address
- `u8` - reserved
- `u32` - magic 0xfce9f929
- `u8[8]` - password, zeros if unused

The server closes the socket if the connection is rejected.

## CSP packet format

- `u16` - frame length, excluding the length field
- CSP data:
  - `u32` - CSP ID
  - `u8[]` - CSP data
