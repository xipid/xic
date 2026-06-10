# High-Level Networking

The **Resource** module provides a unified interface for networked communication, covering everything from low-level UDP sockets to high-level HTTP and TLS (Transport Layer Security).

---

## Network Addresses

Building on the `Path` class, xic use specialized `Address` objects for robust IP/DNS handling.

-   **IPv4/IPv6 Support**: Automatically detects and parses both versions.
-   **DNS Resolution**: Handles hostname resolution through the underlying OS drivers.
-   **Numerical Addresses**: For ultra-fast routing, use `NumericalAddress` arrays which represent identities as pure numeric segments.

```cpp
Address addr("xi.local:8080");
if (addr.isIPv4()) {
    Array<u8> ip = addr.ipv4(); // [192, 168, 1, 10]
}
```

---

## Sockets and Bindings

`NetBind` is the abstract interface for all network ports. It simplifies the complexity of OS-specific socket management into a few callbacks.

### Basic UDP Binding
```cpp
SockBind udp("0.0.0.0:4242");

// Register a packet listener
udp.onPacket([](String data) {
    println("Received packet: " + data);
});

// Sending
udp.send("Heartbeat", Path("192.168.1.255:4242"));
```

### Client Tracking
Enable `trackClients` to automatically manage a map of active remote endpoints. This is essential for building servers that need to monitor connection timeouts.

```cpp
udp.trackClients = true;
udp.destroyTimeout = 30000; // Drop clients after 30s of inactivity
```

---

## High-Level Protocols

xic includes built-in support for standard web protocols.

### HTTP Client
```cpp
HTTPClient client;
HTTPResponse res = client.get("https://api.xi.io/v1/data");

if (res.status == 200) {
    String body = res.body();
}
```

### Secure Communication (TLS)
Encrypted networking is handled via the `TLS` module, which integrates with **BearSSL** or **OpenSSL** backends depending on your platform.

```cpp
TLSServer server;
server.useCertificate("root.crt", "private.key");
server.listen(443);
```

---

## Best Practices

1.  **Non-Blocking Logic**: All network bindings in xic are non-blocking by default. Use a task or a main loop `update()` call to drive the network stack.
2.  **Filter Loopback**: In mesh networking scenarios, set `filterLoopback = true` to avoid processing your own broadcast traffic.
3.  **Path Re-use**: Use the `Path` class to store remote endpoints. It is significantly more efficient than re-parsing strings for every `send()` call.
