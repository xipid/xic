/**
 * @file TLS.hpp
 * @brief TLS transport layer using mbedTLS for the Xi framework.
 *
 * Compiled only when XI_TLS_ENABLED is defined (mbedTLS headers present).
 */

#ifndef XI_RESOURCE_TLS_HPP
#define XI_RESOURCE_TLS_HPP

#include "Network.hpp"

#ifdef XI_TLS_ENABLED
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif

namespace Resource {

using namespace Xi;
using namespace Collection;

/**
 * @struct TLSCert
 * @brief Holds a certificate or key blob and its format identifier.
 */
struct XI_EXPORT TLSCert {
  String data; ///< Raw PEM or DER data.
  String type; ///< Format string, e.g. "pem" or "der".
};

/**
 * @class TLSBind
 * @brief Secure transport binding that extends TCPBind with mbedTLS.
 *
 * Acts as both client and server. The role is determined by who initiates:
 *   - Calling connect() makes this a client endpoint.
 *   - Accepting incoming connections makes this a server endpoint.
 *
 * When XI_TLS_ENABLED is not defined, this class is still declared but
 * the mbedTLS-specific members are excluded. The implementation (.cpp) is
 * only compiled when the headers are present.
 */
class XI_EXPORT TLSBind : public TCPBind {
public:
  Array<TLSCert> ca;     ///< Trusted CA chain (seeded by DNSResolver::ca).
  TLSCert cert;          ///< Our own certificate.
  String privateRSAKey;  ///< Private key (PEM/DER blob).
  String publicRSAKey;   ///< Public key (PEM/DER blob).

  /**
   * @brief Constructs a TLS binding on the given port and host.
   * @param port Local port (0 = ephemeral / client-only).
   * @param host Local host/IP to bind on.
   */
  TLSBind(int port = 0, const String &host = "");
  virtual ~TLSBind();

  /**
   * @brief Loads a certificate or key from a PEM/DER string.
   * @param pemString The raw data.
   * @param type Format identifier ("pem" or "der").
   */
  void load(const String &pemString, const String &type);

  /**
   * @brief Initiates a TLS client connection to a remote host.
   * @param remoteHost Hostname or IP.
   * @param remotePort Destination port.
   * @return true if the connection was started (non-blocking, may need update).
   */
  bool connect(const String &remoteHost, int remotePort);

  void onPacket(Func<void(String)> cb) override;
  void send(const String &data, const Path &target = Path()) override;
  void update() override;

protected:
  Func<void(String)> _tlsPacketListener;

#ifdef XI_TLS_ENABLED
  // --- Per-bind mbedTLS context (shared entropy) ---
  mbedtls_entropy_context _entropy;
  mbedtls_ctr_drbg_context _ctr_drbg;
  mbedtls_ssl_config _conf;
  mbedtls_x509_crt _cacert;
  mbedtls_x509_crt _owncert;
  mbedtls_pk_context _pkey;

  // --- Per-client TLS session ---
  struct TLSSession {
    int clientFd = -1;
    mbedtls_ssl_context ssl;
    bool handshakeDone = false;
    bool isClient = false; ///< true = we connected out; false = accepted in.
    String recvBuf;        ///< Buffered decrypted data.
  };
  Array<TLSSession *> _sessions;

  void _initMbedTLS();
  void _freeMbedTLS();
  TLSSession *_sessionByFd(int fd);
  TLSSession *_createSession(int fd, bool isClientSide);
  void _destroySession(TLSSession *s);
  void _stepHandshake(TLSSession *s);
  void _stepRead(TLSSession *s);

  // mbedTLS BIO callbacks (static, route through userdata pointer).
  static int _bioSend(void *ctx, const unsigned char *buf, size_t len);
  static int _bioRecv(void *ctx, unsigned char *buf, size_t len);
#endif
};

// Factory
XI_EXPORT TLSBind *requestTLSBind(int port = 0, const String &host = "");

} // namespace Resource

#endif // XI_RESOURCE_TLS_HPP
