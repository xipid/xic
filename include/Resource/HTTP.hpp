/**
 * @file HTTP.hpp
 * @brief HTTP/1.1 transport and DNS-over-HTTPS resolver for the Xi framework.
 *
 * Compiled only when XI_TLS_ENABLED is defined (mbedTLS headers present).
 * HTTPBind extends TLSBind, inheriting non-blocking TLS+TCP transport.
 */

#ifndef XI_RESOURCE_HTTP_HPP
#define XI_RESOURCE_HTTP_HPP

#include "TLS.hpp"

namespace Resource {

using namespace Xi;
using namespace Collection;

/**
 * @struct HTTPClient
 * @brief Per-connection HTTP state, tracking headers, method, body progress.
 *
 * This struct is passed to the user's onPacket callback so they can inspect
 * request/response state for each chunk that arrives.
 */
struct XI_EXPORT HTTPClient {
  String id;                  ///< Unique client key (host:port).
  Map<String, String> headers; ///< Accumulated headers.
  bool began = false;         ///< True once the body section has started.
  bool end = false;           ///< True when the connection has fully closed.

  String method;              ///< HTTP method (GET, POST, etc.).
  String url;                 ///< Request URL / path.
  i64 status = 0;             ///< Response status code.
  String statusMessage;       ///< Response status text.

  // --- Internal parser state ---
  String _headerBuf;          ///< Buffer for partial header lines.
  bool _headersDone = false;  ///< True once \r\n\r\n has been seen.
  i64 _contentLength = -1;    ///< Content-Length if provided (-1 = unknown).
  i64 _bodyReceived = 0;      ///< Bytes of body received so far.
  bool _chunked = false;      ///< Transfer-Encoding: chunked.
  i64 _chunkRemaining = 0;    ///< Bytes remaining in current chunk.
  bool _chunkSizeExpected = true; ///< Expecting a chunk-size line next.
  bool _requestSent = false;  ///< True once the initial request line/headers are sent.
};

/**
 * @class DNSResolver
 * @brief Singleton DNS-over-HTTPS resolver using Cloudflare (1.1.1.1).
 *
 * - resolve(url): Blocking. Resolves hostname to IP, returns modified URL.
 * - has(hostname): Non-blocking. Returns true if cached. If not cached,
 *   fires an async lookup in the background and returns false until arrival.
 *   Duplicate requests for the same hostname are suppressed.
 */
class XI_EXPORT DNSResolver {
public:
  Array<TLSCert> ca; ///< CA certs propagated to all new TLS/HTTP binds.

  /**
   * @brief Get the singleton instance.
   */
  static DNSResolver &instance();

  /**
   * @brief Blocking resolve. Translates hostname in URL to IP.
   * @param url Full URL like "https://example.com:443/path".
   * @return URL with hostname replaced by IP, e.g. "https://1.2.3.4:443/path".
   */
  String resolve(const String &url);

  /**
   * @brief Non-blocking cache check.
   * @param hostname Bare hostname (e.g., "example.com").
   * @return true if IP is cached. If false, a background fetch is started
   *         (no duplicates). Subsequent calls return false until resolved.
   */
  bool has(const String &hostname);

  /**
   * @brief Retrieves the cached IP for a hostname (empty if not cached).
   */
  String ip(const String &hostname) const;

  /**
   * @brief Pumps any in-flight DNS requests. Call from your event loop.
   */
  void update();

private:
  DNSResolver();
  ~DNSResolver();

  Map<String, String> _cache;       ///< hostname -> IP.
  Map<String, bool> _inflight;      ///< hostname -> true if request pending.

  String _resolveBlocking(const String &hostname);
  void _startAsyncLookup(const String &hostname);

  // Fallback: plain UDP DNS to 1.1.1.1 (port 53) for bootstrap.
  String _resolveUDP(const String &hostname);
};

/**
 * @class HTTPBind
 * @brief High-level HTTP/1.1 binding extending TLSBind.
 *
 * Works as both client and server simultaneously. The role is determined
 * entirely by who initiates the connection – no additional flags.
 *
 * Usage (server):
 *   HTTPBind http(80);
 *   http.onPacket([](HTTPClient &cli, String chunk) { ... });
 *
 * Usage (client):
 *   HTTPBind http;
 *   HTTPClient cli;
 *   http.fetch(cli, "https://example.com/path");
 *   http.onPacket([](HTTPClient &cli, String chunk) { ... });
 */
class XI_EXPORT HTTPBind : public TLSBind {
public:
  /**
   * @brief Constructs an HTTPBind on the given port.
   * @param port Local port (0 = client-only ephemeral).
   * @param host Local host/IP (empty = INADDR_ANY).
   */
  HTTPBind(int port = 0, const String &host = "");
  virtual ~HTTPBind();

  /**
   * @brief Registers a callback for incoming HTTP data chunks.
   * @param cb Callback receiving the per-client state and the raw chunk.
   */
  void onPacket(Func<void(HTTPClient &, String)> cb);

  /**
   * @brief Initiates an HTTP(S) request to a URL. Resolves DNS automatically.
   * @param cli Client state to populate.
   * @param url Target URL.
   */
  void fetch(HTTPClient &cli, const String &url);

  /**
   * @brief Convenience wrapper for GET request.
   */
  void get(HTTPClient &cli, const String &url);

  /**
   * @brief Sets the HTTP method for the next request on this client.
   */
  void method(HTTPClient &cli, const String &m);

  /**
   * @brief Sends a single header line.
   */
  void header(HTTPClient &cli, const String &key, const String &value);

  /**
   * @brief Sends multiple headers from a map.
   */
  void header(HTTPClient &cli, const Map<String, String> &headers);

  /**
   * @brief Sends a status line (server-side response).
   */
  void status(HTTPClient &cli, int code, const String &message = "OK");

  /**
   * @brief Signals the end of headers and the beginning of the body.
   */
  void begin(HTTPClient &cli);

  /**
   * @brief Sends raw body data.
   */
  void send(HTTPClient &cli, const String &data);

  /**
   * @brief Closes the connection and forgets the client entirely.
   */
  void destroy(HTTPClient &cli);

  /**
   * @brief Main non-blocking update loop. Call from your event loop.
   */
  void update() override;

private:
  Func<void(HTTPClient &, String)> _httpPacketListener;
  Map<String, HTTPClient *> _httpClients; ///< Active HTTP client states.

  void _parseChunk(HTTPClient *cli, const String &raw);
  void _parseHeaderLine(HTTPClient *cli, const String &line);
  HTTPClient *_getOrCreateClient(const String &key);
};

// Factory
XI_EXPORT HTTPBind *requestHTTPBind(int port = 0, const String &host = "");

} // namespace Resource

#endif // XI_RESOURCE_HTTP_HPP
