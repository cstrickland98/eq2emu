# Phase 2: Real Net Transport

Status: complete.

## Purpose

Replace source2's in-memory transport abstractions with real TCP/UDP transport implementations while preserving the existing no-gameplay net boundary.

## Where Work Begins

Start with:

- `source2/net/include/eq2/net/session.h`
- `source2/net/include/eq2/net/tcp_server.h`
- `source2/net/include/eq2/net/udp_stream.h`
- `source2/net/tests/net_tests.cpp`
- Legacy references:
  - `source/common/EQStream.*`
  - `source/common/EQStreamFactory.*`
  - `source/common/TCPConnection.*`

The existing source2 net types model lifecycle, queues, and events, but do not own sockets.

## Work Entailed

- Choose implementation approach:
  - standard sockets,
  - Boost.Asio,
  - a temporary adapter around legacy transport,
  - another dependency already acceptable for the project.
- Add real TCP listener/session implementation.
- Add real UDP socket/datagram stream implementation suitable for EQ2 client traffic.
- Preserve existing abstract surface for fake tests.
- Add event delivery for:
  - accepted/connected,
  - bytes received,
  - writable/backpressure,
  - disconnect,
  - stopped,
  - socket errors.
- Add graceful stop and deterministic thread ownership.
- Add backpressure behavior on real writes.
- Add loopback integration tests that bind local ports and exchange bytes.
- Keep net free of protocol/login/world/zone/gameplay types.

## Deliverables

- Real TCP server/session implementation.
- Real UDP stream/datagram implementation.
- Net service interface that apps can own.
- Loopback tests for TCP and UDP.
- Error and shutdown tests.
- Dependency documentation if Boost.Asio or another library is introduced.

## Exit Criteria

- Net loopback tests pass without fake packet handlers.
- Real TCP and UDP services can start, exchange bytes locally, and stop cleanly.
- No account, character, spawn, combat, Lua, login, world, or zone types appear in net headers.
- App composition can depend on real net services without changing login/world domain logic.

## Progress

- Added `source2/net/include/eq2/net/socket_transport.h`.
- Added `eq2::net::SocketRuntime` for platform socket startup/cleanup.
- Added real loopback `TcpSocketServer` that binds a TCP socket, accepts a connection on a worker thread, receives bytes, emits lifecycle and received events, and stops cleanly.
- Added active TCP session tracking and `TcpSocketServer::send` so source2 services can write protocol responses back to connected clients.
- Added real loopback `UdpSocketServer` that binds a UDP socket, tracks one session per remote endpoint, receives datagrams on a worker thread, writes responses back with `sendto`, emits lifecycle and received events, and stops cleanly.
- Added `send_tcp_loopback`, `exchange_tcp_loopback`, and `send_udp_loopback` helpers for integration tests.
- Updated `eq2_net` to link `ws2_32` on Windows.
- Extended `eq2_net_tests` with real TCP/UDP localhost byte-exchange tests and a TCP server-to-client response test.
- Verification:
  - `cmake -S . -B build\source2`: passed.
  - `cmake --build build\source2 --config Debug --target eq2_net_tests`: passed.
  - `ctest --test-dir build\source2 -C Debug -R eq2_net_tests --output-on-failure`: passed.

## Verification Commands

```powershell
cmake --build build\source2 --config Debug --target eq2_net_tests
ctest --test-dir build\source2 -C Debug -R eq2_net_tests --output-on-failure
```
