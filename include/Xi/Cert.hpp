#ifndef XI_CERT_HPP
#define XI_CERT_HPP 1

#include "Array.hpp"
#include "Map.hpp"
#include "String.hpp"

namespace Xi {

struct XI_EXPORT Cert {
  Xi::String publicKey; // 32 Bytes
  Xi::Map<u64, Xi::String> meta;
  Xi::String signature; // 64 Bytes

  Cert() = default;

  // Deserialize from signed bytes
  Cert(const Xi::String &bytes);

  bool has(u64 k) const;

  // Serializes the payload for signing/verifying (publicKey + meta)
  Xi::String payload() const;

  // Serialize the full certificate (publicKey + meta + signature)
  Xi::String toString() const;

  // Signs the payload
  void sign(const Xi::String &privateKey);

  // Verifies the signature
  bool verify() const;

  // -- Static Methods --

  static Xi::String serialize(const Xi::Array<Cert> &certs);

  static Xi::Array<Cert> parseAll(const Xi::String &bytes);

  static Xi::String childHash(const Xi::String &pub);

  static Xi::Array<Xi::Array<Cert>>
  chains(const Xi::Array<Cert> &allCerts,
         const Xi::Array<Xi::String> &leafPublicKeyHashes,
         const Xi::Array<Xi::String> &rootPublicKeys);
};

} // namespace Xi

#endif // XI_CERT_HPP
