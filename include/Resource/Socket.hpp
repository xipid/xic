/**
 * @file Socket.hpp
 * @brief Low-level socket abstractions and network binding interfaces for the
 * Xi framework.
 */

#ifndef XI_DATA_SOCKET_HPP
#define XI_DATA_SOCKET_HPP

#include "../Collection/Map.hpp"
#include "../Resource/Path.hpp"
#include "../Xi/Device.hpp"
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
class XI_EXPORT NetBind : public Device {
public:
  bool filterLoopback =
      false; ///< Whether to ignore packets from the local machine.
  bool trackClients = false; ///< Whether to maintain a map of active clients.
  u64 destroyTimeout =
      8000;              ///< Timeout for inactive clients (in milliseconds).
  String lastSenderPath; ///< Path of the last sender for the current packet
                         ///< context.

  Map<String, NetClient> clients; ///< Map of tracked clients.

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
