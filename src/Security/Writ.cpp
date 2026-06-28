#include "../../include/Security/Writ.hpp"
#include "../../include/Security/Crypto.hpp"
extern "C" {
#include "../../packages/monocypher/monocypher.h"
}

namespace Security {

Writ::Writ(const String &bytes) {
  usz at = 0;
  if (bytes.size() >= 32) {
    publicKey = bytes.begin(0, 32);
    at += 32;
  } else {
    publicKey = bytes;
    while (publicKey.size() < 32) {
      publicKey.push('\0');
    }
    return;
  }
  if (at < bytes.size()) {
    auto remaining = bytes.begin(at, bytes.size());
    usz localAt = 0;
    meta = decltype(meta)::deserialize(remaining, localAt);
    at += localAt;
  }
  if (at < bytes.size()) {
    signature = bytes.begin(at, bytes.size());
  }
  while (signature.size() < 64) {
    signature.push('\0');
  }
  if (signature.size() > 64) {
    signature = signature.begin(0, 64);
  }
}

bool Writ::has(u64 k) const { return meta.has(k); }

String Writ::payload() const {
  String res;
  String paddedPub = publicKey;
  while (paddedPub.size() < 32) {
    paddedPub.push('\0');
  }
  paddedPub = paddedPub.begin(0, 32);
  res += paddedPub;
  res += meta.serialize();
  return res;
}

String Writ::toString() const {
  String res = payload();
  String paddedSig = signature;
  while (paddedSig.size() < 64) {
    paddedSig.push('\0');
  }
  paddedSig = paddedSig.begin(0, 64);
  res += paddedSig;
  return res;
}

void Writ::sign(const String &privateKey) {
  String p = payload();
  signature = signX(privateKey, p);
}

bool Writ::verify() const {
  if (publicKey.size() < 32 || signature.size() < 64)
    return false;
  String p = payload();
  String paddedPub = publicKey.begin(0, 32);
  String paddedSig = signature.begin(0, 64);
  return verifyX(paddedPub, p, paddedSig);
}

String Writ::serialize(const Array<Writ> &writs) {
  String res;
  res.pushVarLong((long long)writs.size());
  for (usz i = 0; i < writs.size(); ++i) {
    res.pushVarString(writs[i].toString());
  }
  return res;
}

Array<Writ> Writ::parseAll(const String &bytes) {
  Array<Writ> res;
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
      res.push(Writ(bytes.begin(at, at + strRes.value)));
      at += strRes.value;
    } else {
      break;
    }
  }
  return res;
}

String Writ::childHash(const String &pub) {
  u8 out[8];
  crypto_blake2b(out, 8, pub.data(), pub.size());
  String h;
  for (int i = 0; i < 8; ++i)
    h.push(out[i]);
  return h;
}

Array<Array<Writ>> Writ::chains(const Array<Writ> &allWrits,
                                const Array<String> &leafPublicKeyHashes,
                                const Array<String> &rootPublicKeys) {
  Array<Array<Writ>> result;
  Map<String, bool> roots;
  for (usz i = 0; i < rootPublicKeys.size(); ++i) {
    roots[rootPublicKeys[i]] = true;
  }
  Map<String, Array<Writ>> parentOf;
  for (usz i = 0; i < allWrits.size(); ++i) {
    const auto &c = allWrits[i];
    if (c.has(0)) {
      String childHash = *c.meta.get(0);
      parentOf[childHash].push(c);
    }
  }
  struct PathNode {
    String currentTargetKeyHash;
    Array<Writ> pathSoFar;
  };
  Array<PathNode> stack;
  for (usz i = 0; i < leafPublicKeyHashes.size(); ++i) {
    PathNode n;
    n.currentTargetKeyHash = leafPublicKeyHashes[i];
    stack.push(n);
  }
  while (stack.size() > 0) {
    PathNode node = stack.pop();
    if (parentOf.has(node.currentTargetKeyHash)) {
      const Array<Writ> &parents = *parentOf.get(node.currentTargetKeyHash);
      for (usz i = 0; i < parents.size(); ++i) {
        if (roots.has(parents[i].publicKey)) {
          Array<Writ> finalPath = node.pathSoFar;
          finalPath.push(parents[i]);
          result.push(finalPath);
          continue;
        }
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
        nextNode.currentTargetKeyHash = Writ::childHash(parents[i].publicKey);
        for (usz j = 0; j < node.pathSoFar.size(); ++j) {
          nextNode.pathSoFar.push(node.pathSoFar[j]);
        }
        nextNode.pathSoFar.push(parents[i]);
        stack.push(nextNode); // push
      }
    }
  }
  return result;
}

} // namespace Security
