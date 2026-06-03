/**
 * @file TLS.cpp
 * @brief TLS transport implementation using mbedTLS for the Xi framework.
 *
 * Only compiled when XI_TLS_ENABLED is defined by CMake (mbedTLS found).
 */

#include "../../include/Resource/TLS.hpp"

#ifdef XI_TLS_ENABLED

#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mbedtls/error.h>
#include <mbedtls/pem.h>

namespace Resource {

// -------------------------------------------------------------------------
// BIO Callbacks – bridge mbedTLS to non-blocking OS sockets
// -------------------------------------------------------------------------

int TLSBind::_bioSend(void *ctx, const unsigned char *buf, size_t len) {
  TLSSession *s = static_cast<TLSSession *>(ctx);
  ssize_t ret = ::send(s->clientFd, buf, len, MSG_NOSIGNAL);
  if (ret < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
  }
  return (int)ret;
}

int TLSBind::_bioRecv(void *ctx, unsigned char *buf, size_t len) {
  TLSSession *s = static_cast<TLSSession *>(ctx);
  ssize_t ret = ::recv(s->clientFd, buf, len, 0);
  if (ret < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
  }
  if (ret == 0)
    return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
  return (int)ret;
}

// -------------------------------------------------------------------------
// Internal helpers
// -------------------------------------------------------------------------

void TLSBind::_initMbedTLS() {
  mbedtls_entropy_init(&_entropy);
  mbedtls_ctr_drbg_init(&_ctr_drbg);
  mbedtls_ssl_config_init(&_conf);
  mbedtls_x509_crt_init(&_cacert);
  mbedtls_x509_crt_init(&_owncert);
  mbedtls_pk_init(&_pkey);

  const char *pers = "xi_tls";
  mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy,
                         (const unsigned char *)pers, 6);
}

void TLSBind::_freeMbedTLS() {
  for (usz i = 0; i < _sessions.size(); ++i)
    _destroySession(_sessions[i]);
  _sessions.clear();

  mbedtls_pk_free(&_pkey);
  mbedtls_x509_crt_free(&_owncert);
  mbedtls_x509_crt_free(&_cacert);
  mbedtls_ssl_config_free(&_conf);
  mbedtls_ctr_drbg_free(&_ctr_drbg);
  mbedtls_entropy_free(&_entropy);
}

TLSBind::TLSSession *TLSBind::_sessionByFd(int sfd) {
  for (usz i = 0; i < _sessions.size(); ++i) {
    if (_sessions[i]->clientFd == sfd)
      return _sessions[i];
  }
  return nullptr;
}

TLSBind::TLSSession *TLSBind::_createSession(int sfd, bool isClientSide) {
  TLSSession *s = new TLSSession();
  s->clientFd = sfd;
  s->isClient = isClientSide;
  s->handshakeDone = false;

  mbedtls_ssl_init(&s->ssl);

  // Reconfigure for endpoint type
  int endpoint = isClientSide ? MBEDTLS_SSL_IS_CLIENT : MBEDTLS_SSL_IS_SERVER;
  mbedtls_ssl_config_defaults(&_conf, endpoint, MBEDTLS_SSL_TRANSPORT_STREAM,
                               MBEDTLS_SSL_PRESET_DEFAULT);
  mbedtls_ssl_conf_rng(&_conf, mbedtls_ctr_drbg_random, &_ctr_drbg);
  mbedtls_ssl_conf_ca_chain(&_conf, &_cacert, nullptr);

  // If we have our own cert + key, attach them
  if (cert.data.size() > 0 && privateRSAKey.size() > 0) {
    mbedtls_ssl_conf_own_cert(&_conf, &_owncert, &_pkey);
  }

  // For client side with no CA loaded, we still allow connecting (optional
  // verification). Users should load CAs via resolv.ca for production.
  if (isClientSide && _cacert.version == 0) {
    mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
  } else {
    mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
  }

  mbedtls_ssl_setup(&s->ssl, &_conf);
  mbedtls_ssl_set_bio(&s->ssl, s, _bioSend, _bioRecv, nullptr);

  _sessions.push(s);
  return s;
}

void TLSBind::_destroySession(TLSSession *s) {
  if (!s)
    return;
  mbedtls_ssl_close_notify(&s->ssl);
  mbedtls_ssl_free(&s->ssl);
  if (s->clientFd != -1)
    ::close(s->clientFd);
  delete s;
}

void TLSBind::_stepHandshake(TLSSession *s) {
  int ret = mbedtls_ssl_handshake(&s->ssl);
  if (ret == 0) {
    s->handshakeDone = true;
  } else if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
             ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
    // Handshake failed – remove session
    for (usz i = 0; i < _sessions.size(); ++i) {
      if (_sessions[i] == s) {
        _destroySession(s);
        _sessions.splice(i, 1);
        break;
      }
    }
  }
}

void TLSBind::_stepRead(TLSSession *s) {
  unsigned char buf[16384];
  int ret = mbedtls_ssl_read(&s->ssl, buf, sizeof(buf));
  if (ret > 0) {
    String packet((const u8 *)buf, (usz)ret);

    // Set the sender path for the callback context
    // Reuse the NetBind client tracking from TCPBind
    for (auto &kv : clients) {
      if ((int)kv.value.lastSeen == s->clientFd) {
        lastSenderPath = kv.key;
        break;
      }
    }

    if (_tlsPacketListener.isValid())
      _tlsPacketListener(packet);
  } else if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
             ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
    // Non-blocking, just return
  } else {
    // Connection closed or error – remove session
    for (usz i = 0; i < _sessions.size(); ++i) {
      if (_sessions[i] == s) {
        _destroySession(s);
        _sessions.splice(i, 1);
        break;
      }
    }
  }
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

TLSBind::TLSBind(const NumericalAddress &address) : TCPBind(address) {
  name = "TLSBind";
  _initMbedTLS();
}

TLSBind::~TLSBind() { _freeMbedTLS(); }

void TLSBind::load(const String &pemString, const String &type) {
  if (type == "pem" || type.isEmpty()) {
    // Attempt to parse as CA cert first, then own cert, then private key
    int ret = mbedtls_x509_crt_parse(&_cacert,
                                      (const unsigned char *)pemString.data(),
                                      pemString.size() + 1); // +1 for null
    if (ret != 0) {
      // Try as own cert
      ret = mbedtls_x509_crt_parse(&_owncert,
                                    (const unsigned char *)pemString.data(),
                                    pemString.size() + 1);
    }
    // Try as private key
    mbedtls_pk_parse_key(&_pkey, (const unsigned char *)pemString.data(),
                          pemString.size() + 1, nullptr, 0
#if MBEDTLS_VERSION_MAJOR >= 3
                          ,
                          mbedtls_ctr_drbg_random, &_ctr_drbg
#endif
    );
  } else if (type == "der") {
    mbedtls_x509_crt_parse_der(&_cacert,
                                (const unsigned char *)pemString.data(),
                                pemString.size());
  }
}

bool TLSBind::connect(const String &remoteHost, int remotePort) {
  // Create a TCP socket and connect non-blocking
  int cfd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (cfd == -1)
    return false;

  fcntl(cfd, F_SETFL, O_NONBLOCK);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((u16)remotePort);
  inet_pton(AF_INET, remoteHost.c_str(), &addr.sin_addr);

  int ret = ::connect(cfd, (struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0 && errno != EINPROGRESS) {
    ::close(cfd);
    return false;
  }

  // Track client
  String key =
      remoteHost + ":" + String((long long)remotePort);
  NetClient &c = clients[key];
  c.path = Path(key);
  c.lastSeen = (u64)cfd;
  c.lastSent = millis();

  TLSSession *s = _createSession(cfd, true);

  // Set hostname for SNI
  mbedtls_ssl_set_hostname(&s->ssl, remoteHost.c_str());

  return true;
}

void TLSBind::onPacket(Func<void(String)> cb) {
  _tlsPacketListener = Move(cb);
}

void TLSBind::send(const String &data, const Path &target) {
  String key = target.toString(true);
  // Find session for this target
  if (clients.has(key)) {
    int cfd = (int)clients[key].lastSeen;
    TLSSession *s = _sessionByFd(cfd);
    if (s && s->handshakeDone) {
      const unsigned char *ptr = data.data();
      usz remaining = data.size();
      while (remaining > 0) {
        int ret = mbedtls_ssl_write(&s->ssl, ptr, remaining);
        if (ret > 0) {
          ptr += ret;
          remaining -= (usz)ret;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
          break; // Non-blocking, come back later
        } else {
          break; // Error
        }
      }
    }
  }
}

void TLSBind::update() {
  if (fd == -1)
    return;

  // --- Accept new TCP connections (server mode) ---
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);
  int clientFd = accept(fd, (struct sockaddr *)&client_addr, &addr_len);
  if (clientFd != -1) {
    fcntl(clientFd, F_SETFL, O_NONBLOCK);
    String clientHost = inet_ntoa(client_addr.sin_addr);
    int clientPort = ntohs(client_addr.sin_port);
    String clientKey =
        clientHost + ":" + String((long long)clientPort);

    NetClient &c = clients[clientKey];
    c.path = Path(clientKey);
    c.lastSeen = (u64)clientFd;
    c.lastSent = millis();

    _createSession(clientFd, false);
  }

  // --- Step all sessions ---
  // Iterate in reverse so splices don't skip elements
  for (long long i = (long long)_sessions.size() - 1; i >= 0; --i) {
    TLSSession *s = _sessions[(usz)i];
    if (!s->handshakeDone) {
      _stepHandshake(s);
    } else {
      _stepRead(s);
    }
  }
}

} // namespace Resource

#endif // XI_TLS_ENABLED
