#ifndef XI_CERT_HPP
#define XI_CERT_HPP 1

#include "Array.hpp"
#include "Crypto.hpp"
#include "Map.hpp"
#include "String.hpp"

namespace Xi {

struct Cert {
  Xi::String publicKey; // 32 Bytes
  Xi::Map<u64, Xi::String> meta;
  Xi::String signature; // 64 Bytes

  Cert() = default;

  // Deserialize from signed bytes
  Cert(const Xi::String &bytes) {
    usz at = 0;

    // Extract Public Key (32 bytes max)
    if (bytes.size() >= 32) {
      publicKey = bytes.begin(0, 32);
      at += 32;
    } else {
      // Missing or short, pad with zeros to prevent crashes
      publicKey = bytes;
      while (publicKey.size() < 32) {
        publicKey.push('\0');
      }
      return;
    }

    // Extract Meta
    if (at < bytes.size()) {
      auto remaining = bytes.begin(at, bytes.size());
      usz localAt = 0;
      meta = decltype(meta)::deserialize(remaining, localAt);
      at += localAt;
    }

    // Extract Signature (64 bytes max)
    if (at < bytes.size()) {
      signature = bytes.begin(at, bytes.size());
    }

    // Pad signature if it's short to prevent crashes
    while (signature.size() < 64) {
      signature.push('\0');
    }

    // If it's too long, truncate it
    if (signature.size() > 64) {
      signature = signature.begin(0, 64);
    }
  }

  bool has(u64 k) const { return meta.has(k); }

  // Serializes the payload for signing/verifying (publicKey + meta)
  Xi::String payload() const {
    Xi::String res;

    // 1. Public Key (Fixed 32 Bytes)
    Xi::String paddedPub = publicKey;
    while (paddedPub.size() < 32) {
      paddedPub.push('\0');
    }
    paddedPub = paddedPub.begin(0, 32);
    res += paddedPub;

    // 2. Meta
    meta.serialize(res);
    return res;
  }

  // Serialize the full certificate (publicKey + meta + signature)
  Xi::String toString() const {
    Xi::String res = payload();

    Xi::String paddedSig = signature;
    while (paddedSig.size() < 64) {
      paddedSig.push('\0');
    }
    paddedSig = paddedSig.begin(0, 64);
    res += paddedSig;

    return res;
  }

  // Signs the payload
  void sign(const Xi::String &privateKey) {
    Xi::String p = payload();
    signature = Xi::signX(privateKey, p);
  }

  // Verifies the signature
  bool verify() const {
    if (publicKey.size() < 32 || signature.size() < 64)
      return false;

    Xi::String p = payload();

    // Ensure properly sized parameters
    Xi::String paddedPub = publicKey.begin(0, 32);
    Xi::String paddedSig = signature.begin(0, 64);

    return Xi::verifyX(paddedPub, p, paddedSig);
  }

  // -- Static Methods --

  static Xi::String serialize(const Xi::Array<Cert> &certs) {
    Xi::String res;
    res.pushVarLong((long long)certs.size());
    for (usz i = 0; i < certs.size(); ++i) {
      res.pushVarString(certs[i].toString());
    }
    return res;
  }

  static Xi::Array<Cert> parseAll(const Xi::String &bytes) {
    Xi::Array<Cert> res;
    usz at = 0;

    auto countRes = bytes.peekVarLong(at);
    if (countRes.error)
      return res;
    at += countRes.bytes;

    for (long long i = 0; i < countRes.value; ++i) {
      auto strRes = bytes.peekVarLong(at);
      if (strRes.error)
        break;
      at += strRes.bytes;

      if (at + strRes.value <= bytes.size()) {
        res.push(Cert(bytes.begin(at, at + strRes.value)));
        at += strRes.value;
      } else {
        break;
      }
    }
    return res;
  }

  static Xi::String childHash(const Xi::String &pub) {
    u8 out[8];
    ::crypto_blake2b(out, 8, pub.data(), pub.size());
    Xi::String h;
    for (int i = 0; i < 8; ++i)
      h.push(out[i]);
    return h;
  }

  static Xi::Array<Xi::Array<Cert>>
  chains(const Xi::Array<Cert> &allCerts,
         const Xi::Array<Xi::String> &leafPublicKeyHashes,
         const Xi::Array<Xi::String> &rootPublicKeys) {
    Xi::Array<Xi::Array<Cert>> result;

    // Fast lookups for roots
    Xi::Map<Xi::String, bool> roots;
    for (usz i = 0; i < rootPublicKeys.size(); ++i) {
      roots[rootPublicKeys[i]] = true;
    }

    // parentOf[childPublicKeyHash] = Array of Certs that are parents of this
    // child
    Xi::Map<Xi::String, Xi::Array<Cert>> parentOf;

    for (usz i = 0; i < allCerts.size(); ++i) {
      const auto &c = allCerts[i];
      if (c.has(0)) {
        Xi::String childHash = *c.meta.get(0);
        parentOf[childHash].push(c);
      }
    }

    // BFS/DFS trace up from leaf
    struct PathNode {
      Xi::String currentTargetKeyHash;
      Xi::Array<Cert> pathSoFar;
    };

    Xi::Array<PathNode> stack;

    // Initialize stack with all leaf hashes.
    for (usz i = 0; i < leafPublicKeyHashes.size(); ++i) {
      PathNode n;
      n.currentTargetKeyHash = leafPublicKeyHashes[i];
      stack.push(n);
    }

    while (stack.size() > 0) {
      PathNode node = stack[stack.size() - 1]; // pop
      stack.remove(stack.size() - 1);

      // Find who authorized this target key hash
      if (parentOf.has(node.currentTargetKeyHash)) {
        const Xi::Array<Cert> &parents =
            *parentOf.get(node.currentTargetKeyHash);
        for (usz i = 0; i < parents.size(); ++i) {

          // Check if this parent is a root
          if (roots.has(parents[i].publicKey)) {
            Xi::Array<Cert> finalPath = node.pathSoFar;
            finalPath.push(parents[i]);
            result.push(finalPath);
            continue;
          }

          // Cycle detection: ensure we don't visit the same parent public key
          // twice in a path
          bool hasCycle = false;
          for (usz k = 0; k < node.pathSoFar.size(); ++k) {
            if (node.pathSoFar[k].publicKey == parents[i].publicKey) {
              hasCycle = true;
              break;
            }
          }
          if (hasCycle)
            continue;

          PathNode nextNode;
          // The parent's public key hash becomes the new target to trace upward
          nextNode.currentTargetKeyHash = childHash(parents[i].publicKey);
          // Duplicate path
          for (usz j = 0; j < node.pathSoFar.size(); ++j) {
            nextNode.pathSoFar.push(node.pathSoFar[j]);
          }
          // Add this parent cert to the path
          nextNode.pathSoFar.push(parents[i]);
          stack.push(nextNode); // push
        }
      }
    }

    return result;
  }
};

} // namespace Xi

#endif // XI_CERT_HPP
