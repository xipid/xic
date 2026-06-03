/**
 * @file Socket.hpp
 * @brief Low-level socket abstractions and network binding interfaces for the
 * Xi framework.
 */

#ifndef XI_DATA_SOCKET_HPP
#define XI_DATA_SOCKET_HPP

#include "../Collection/Map.hpp"
#include "../Resource/Path.hpp"
#include <Xi/Primitives.hpp>
#include "../Xi/Func.hpp"

using namespace Collection;
using namespace Xi;

/**
 * @namespace Resource
 */
namespace Resource {

/**
 * @struct NetClient
 * @brief Tracks metadata for a remote network client.
 */
struct NetClient {
  Path path;        ///< The network path/address of the client.
  u64 lastSeen = 0; ///< Timestamp of the last received packet.
  u64 lastSent = 0; ///< Timestamp of the last sent packet.
};

/**
 * @class NetBind
 * @brief Abstract base class for network bindings (UDP, TCP, Unix Sockets).
 */
class XI_EXPORT NetBind {
public:
  String name = "NetBind";
  bool filterLoopback =
      false; ///< Whether to ignore packets from the local machine.
  bool trackClients = false; ///< Whether to maintain a map of active clients.
  u64 destroyTimeout =
      8000;              ///< Timeout for inactive clients (in milliseconds).
  String lastSenderPath; ///< Path of the last sender for the current packet
                         ///< context.

  Map<String, NetClient> clients; ///< Map of tracked clients.

  static void parseNumericalAddress(const NumericalAddress &addr, String &host, int &port) {
    if (addr.size() >= 5 && addr[0] == 7) {
      host = String((long long)addr[1]) + "." + String((long long)addr[2]) + "." + String((long long)addr[3]) + "." + String((long long)addr[4]);
      port = (addr.size() >= 6) ? (int)addr[5] : 0;
    } else if (addr.size() >= 9 && addr[0] == 8) {
      host = "";
      for (usz i = 1; i <= 8; ++i) {
        if (i > 1) host += ":";
        u64 val = addr[i];
        if (val == 0) {
          host += "0";
        } else {
          char buf[32];
          int len = 0;
          const char hexChars[] = "0123456789abcdef";
          while (val > 0) {
            buf[len++] = hexChars[val & 0xf];
            val >>= 4;
          }
          for (int j = len - 1; j >= 0; --j) {
            host.push((u8)buf[j]);
          }
        }
      }
      port = (addr.size() >= 10) ? (int)addr[9] : 0;
    } else if (addr.size() >= 4) {
      host = String((long long)addr[0]) + "." + String((long long)addr[1]) + "." + String((long long)addr[2]) + "." + String((long long)addr[3]);
      port = (addr.size() >= 5) ? (int)addr[4] : 0;
    } else {
      host = "";
      port = 0;
    }
  }

  /**
   * @brief Registers a callback for incoming packets.
   * @param cb The callback function.
   */
  virtual void onPacket(Func<void(String)> cb) = 0;

  /**
   * @brief Sends data to a target network path.
   * @param data The payload to send.
   * @param target The destination path.
   */
  virtual void send(const String &data, const Path &target = Path()) = 0;

  virtual void update() {}

  virtual ~NetBind() = default;
};

/**
 * @class SockBind
 * @brief Implementation of network binding using OS sockets.
 */
class XI_EXPORT SockBind : public NetBind {
public:
  String path; ///< The socket path or address string.
  int fd = -1; ///< Socket file descriptor.
  bool ownsSocket =
      false; ///< Whether this instance is responsible for closing the socket.

  SockBind(const String &p = "");
  virtual ~SockBind();

  void onPacket(Func<void(String)> cb) override;
  void send(const String &data, const Path &target = Path()) override;
  void update() override;

private:
  Func<void(String)> _packetListener;
};

} // namespace Resource

#endif // XI_DATA_SOCKET_HPP
