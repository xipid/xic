/**
 * @file Network.hpp
 * @brief High-level networking abstractions (UDP/TCP) for the Xi framework.

 */

#ifndef XI_NETWORK_NETWORK_HPP
#define XI_NETWORK_NETWORK_HPP

#include "Socket.hpp"

/**
 * @namespace Resource
 */
namespace Resource {

using namespace Xi;

/**
 * @class UDPBind
 * @brief Represents a UDP network binding.
 */
class XI_EXPORT UDPBind : public NetBind {
public:
  int fd = -1;  ///< File descriptor for the socket.
  int port = 0; ///< Local port binding.
  String host;  ///< Local host/IP binding.

  UDPBind(int p = 0, const String &h = "");
  virtual ~UDPBind();

  void onPacket(Func<void(String)> cb) override;
  void send(const String &data, const Path &target = Path()) override;
  void update() override;

private:
  Func<void(String)> _packetListener;
};

/**
 * @class TCPBind
 * @brief Represents a TCP network binding.
 */
class XI_EXPORT TCPBind : public NetBind {
public:
  int fd = -1;
  int port = 0;
  String host;

  TCPBind(int p = 0, const String &h = "");
  virtual ~TCPBind();

  void onPacket(Func<void(String)> cb) override;
  void send(const String &data, const Path &target = Path()) override;
  void update() override;

private:
  Func<void(String)> _packetListener;
};

// Factory functions
XI_EXPORT UDPBind *requestUDPBind(int port = 0, const String &host = "");
XI_EXPORT TCPBind *requestTCPBind(int port = 0, const String &host = "");

} // namespace Resource

#endif // XI_DATA_NETWORK_HPP
