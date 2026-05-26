/**
 * @file Socket.cpp
 * @brief Unix Domain Socket implementation for the Xi framework.

 */

#include "../../include/Resource/Socket.hpp"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace Resource {

// -------------------------------------------------------------------------
// SockBind
// -------------------------------------------------------------------------

SockBind::SockBind(const String &p) {
  Device::name = "SockBind";
  path = p;
  if (path.isEmpty()) {
    char tmp[] = "/tmp/xi_sock_XXXXXX";
    int tfd = mkstemp(tmp);
    if (tfd != -1) {
      close(tfd);
      unlink(tmp);
      path = tmp;
    }
  }

  fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd != -1) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    unlink(path.c_str());
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != -1) {
      fcntl(fd, F_SETFL, O_NONBLOCK);
      ownsSocket = true;
    } else {
      close(fd);
      fd = -1;
    }
  }
}

SockBind::~SockBind() {
  if (fd != -1) {
    close(fd);
    if (ownsSocket)
      unlink(path.c_str());
  }
}

void SockBind::onPacket(Func<void(String)> cb) { _packetListener = Move(cb); }

void SockBind::send(const String &data, const Path &target) {
  if (fd == -1)
    return;

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  String targetStr = target.toString();
  strncpy(addr.sun_path, targetStr.c_str(), sizeof(addr.sun_path) - 1);

  ::sendto(fd, data.data(), data.size(), 0, (struct sockaddr *)&addr,
           sizeof(addr));
}

void SockBind::update() {
  if (fd == -1)
    return;

  u8 buf[65536];
  struct sockaddr_un src_addr;
  socklen_t addr_len = sizeof(src_addr);

  ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src_addr,
                       &addr_len);
  if (n > 0) {
    lastSenderPath = src_addr.sun_path;
    String packet((const u8 *)buf, (usz)n);

    if (trackClients) {
      String clientPath = lastSenderPath;
      if (!clientPath.isEmpty()) {
        NetClient &c = clients[clientPath];
        c.path = Path(clientPath);
        c.lastSeen = millis();
      }
    }

    if (_packetListener.isValid()) {
      _packetListener(packet);
    }
  }
}

} // namespace Resource
