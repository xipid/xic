#ifndef XI_SECURITY_WRIT_HPP
#define XI_SECURITY_WRIT_HPP 1

#include <Collection/Array.hpp>
#include <Collection/Map.hpp>
#include <Collection/String.hpp>

using namespace Xi;
using namespace Collection;

namespace Security {

/**
 * @struct Writ
 * @brief Represents a cryptographic trust document (formerly Cert) containing a public key, metadata, and signature.
 */
struct XI_EXPORT Writ {
  String publicKey; // 32 Bytes
  Map<u64, String> meta;
  String signature; // 64 Bytes

  Writ() = default;

  // Deserialize from signed bytes
  Writ(const String &bytes);

  bool has(u64 k) const;

  // Serializes the payload for signing/verifying (publicKey + meta)
  String payload() const;

  // Serialize the full writ (publicKey + meta + signature)
  String toString() const;

  // Signs the payload
  void sign(const String &privateKey);

  // Verifies the signature
  bool verify() const;

  // -- Static Methods --

  static String serialize(const Array<Writ> &writs);

  static Array<Writ> parseAll(const String &bytes);

  static String childHash(const String &pub);

  static Array<Array<Writ>>
  chains(const Array<Writ> &allWrits,
         const Array<String> &leafPublicKeyHashes,
         const Array<String> &rootPublicKeys);
};

} // namespace Security

#endif // XI_SECURITY_WRIT_HPP
