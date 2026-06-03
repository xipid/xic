/**
 * @file Network.cpp
 * @brief Networking implementation for the Xi framework (UDP/TCP stations).

 */

#include "../../include/Resource/Network.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Resource {

UDPBind::UDPBind(const NumericalAddress &address) {
  parseNumericalAddress(address, host, port);
  name = "UDPBind";
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd != -1) {
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host.isEmpty()) {
      addr.sin_addr.s_addr = INADDR_ANY;
    } else {
      inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != -1) {
      fcntl(fd, F_SETFL, O_NONBLOCK);

      if (port == 0) {
        socklen_t len = sizeof(addr);
        getsockname(fd, (struct sockaddr *)&addr, &len);
        port = ntohs(addr.sin_port);
      }
    } else {
      close(fd);
      fd = -1;
    }
  }
}

UDPBind::~UDPBind() {
  if (fd != -1)
    close(fd);
}

void UDPBind::onPacket(Func<void(String)> cb) { _packetListener = Move(cb); }

void UDPBind::send(const String &data, const Path &target) {
  if (fd == -1)
    return;

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(target.port());
  inet_pton(AF_INET, target.host().c_str(), &addr.sin_addr);

  ::sendto(fd, data.data(), data.size(), 0, (struct sockaddr *)&addr,
           sizeof(addr));
}

void UDPBind::update() {
  if (fd == -1)
    return;

  u8 buf[65536];
  struct sockaddr_in src_addr;
  socklen_t addr_len = sizeof(src_addr);

  ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src_addr,
                       &addr_len);
  if (n > 0) {
    lastSenderPath = inet_ntoa(src_addr.sin_addr);
    lastSenderPath += ":" + String((long long)ntohs(src_addr.sin_port));

    String packet((const u8 *)buf, (usz)n);

    if (trackClients) {
      NetClient &c = clients[lastSenderPath];
      c.path = Path(lastSenderPath);
      c.lastSeen = millis();
    }

    if (_packetListener.isValid()) {
      _packetListener(packet);
    }
  }
}

// -------------------------------------------------------------------------
// TCPBind
// -------------------------------------------------------------------------

TCPBind::TCPBind(const NumericalAddress &address) {
  parseNumericalAddress(address, host, port);
  name = "TCPBind";
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd != -1) {
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host.isEmpty()) {
      addr.sin_addr.s_addr = INADDR_ANY;
    } else {
      inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != -1) {
      listen(fd, 32);
      fcntl(fd, F_SETFL, O_NONBLOCK);

      if (port == 0) {
        socklen_t len = sizeof(addr);
        getsockname(fd, (struct sockaddr *)&addr, &len);
        port = ntohs(addr.sin_port);
      }
    } else {
      close(fd);
      fd = -1;
    }
  }
}

TCPBind::~TCPBind() {
  if (fd != -1)
    close(fd);
}

void TCPBind::onPacket(Func<void(String)> cb) { _packetListener = Move(cb); }

void TCPBind::send(const String &data, const Path &target) {
  String key = target.toString(true);
  if (clients.has(key)) {
    int clientFd = (int)clients[key].lastSeen;
    ::send(clientFd, data.data(), data.size(), 0);
  }
}

void TCPBind::update() {
  if (fd == -1)
    return;

  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);
  int clientFd = accept(fd, (struct sockaddr *)&client_addr, &addr_len);
  if (clientFd != -1) {
    fcntl(clientFd, F_SETFL, O_NONBLOCK);
    String clientHost = inet_ntoa(client_addr.sin_addr);
    int clientPort = ntohs(client_addr.sin_port);
    String clientKey = clientHost + ":" + String((long long)clientPort);

    NetClient &c = clients[clientKey];
    c.path = Path(clientKey);
    c.lastSeen = clientFd;
    c.lastSent = millis();
  }

  u8 buf[65536];
  for (auto &kv : clients) {
    int cFd = (int)kv.value.lastSeen;
    ssize_t n = recv(cFd, buf, sizeof(buf), 0);
    if (n > 0) {
      lastSenderPath = kv.key;
      String packet((const u8 *)buf, (usz)n);
      if (_packetListener.isValid())
        _packetListener(packet);
    } else if (n == 0) {
      close(cFd);
      // Marked for removal logic should be here if needed
    }
  }
}

} // namespace Resource
