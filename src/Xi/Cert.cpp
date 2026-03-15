#include <Xi/Cert.hpp>
#include <Xi/Crypto.hpp>

namespace Xi {

Cert::Cert(const Xi::String &bytes) {
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

bool Cert::has(u64 k) const { return meta.has(k); }

Xi::String Cert::payload() const {
  Xi::String res;
  Xi::String paddedPub = publicKey;
  while (paddedPub.size() < 32) {
    paddedPub.push('\0');
  }
  paddedPub = paddedPub.begin(0, 32);
  res += paddedPub;
  res += meta.serialize();
  return res;
}

Xi::String Cert::toString() const {
  Xi::String res = payload();
  Xi::String paddedSig = signature;
  while (paddedSig.size() < 64) {
    paddedSig.push('\0');
  }
  paddedSig = paddedSig.begin(0, 64);
  res += paddedSig;
  return res;
}

void Cert::sign(const Xi::String &privateKey) {
  Xi::String p = payload();
  signature = Xi::signX(privateKey, p);
}

bool Cert::verify() const {
  if (publicKey.size() < 32 || signature.size() < 64)
    return false;
  Xi::String p = payload();
  Xi::String paddedPub = publicKey.begin(0, 32);
  Xi::String paddedSig = signature.begin(0, 64);
  return Xi::verifyX(paddedPub, p, paddedSig);
}

Xi::String Cert::serialize(const Xi::Array<Cert> &certs) {
  Xi::String res;
  res.pushVarLong((long long)certs.size());
  for (usz i = 0; i < certs.size(); ++i) {
    res.pushVarString(certs[i].toString());
  }
  return res;
}

Xi::Array<Cert> Cert::parseAll(const Xi::String &bytes) {
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

Xi::String Cert::childHash(const Xi::String &pub) {
  u8 out[8];
  crypto_blake2b(out, 8, pub.data(), pub.size());
  Xi::String h;
  for (int i = 0; i < 8; ++i)
    h.push(out[i]);
  return h;
}

Xi::Array<Xi::Array<Cert>> Cert::chains(const Xi::Array<Cert> &allCerts,
                                       const Xi::Array<Xi::String> &leafPublicKeyHashes,
                                       const Xi::Array<Xi::String> &rootPublicKeys) {
  Xi::Array<Xi::Array<Cert>> result;
  Xi::Map<Xi::String, bool> roots;
  for (usz i = 0; i < rootPublicKeys.size(); ++i) {
    roots[rootPublicKeys[i]] = true;
  }
  Xi::Map<Xi::String, Xi::Array<Cert>> parentOf;
  for (usz i = 0; i < allCerts.size(); ++i) {
    const auto &c = allCerts[i];
    if (c.has(0)) {
      Xi::String childHash = *c.meta.get(0);
      parentOf[childHash].push(c);
    }
  }
  struct PathNode {
    Xi::String currentTargetKeyHash;
    Xi::Array<Cert> pathSoFar;
  };
  Xi::Array<PathNode> stack;
  for (usz i = 0; i < leafPublicKeyHashes.size(); ++i) {
    PathNode n;
    n.currentTargetKeyHash = leafPublicKeyHashes[i];
    stack.push(n);
  }
  while (stack.size() > 0) {
    PathNode node = stack.pop();
    if (parentOf.has(node.currentTargetKeyHash)) {
      const Xi::Array<Cert> &parents = *parentOf.get(node.currentTargetKeyHash);
      for (usz i = 0; i < parents.size(); ++i) {
        if (roots.has(parents[i].publicKey)) {
          Xi::Array<Cert> finalPath = node.pathSoFar;
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
        nextNode.currentTargetKeyHash = Cert::childHash(parents[i].publicKey);
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

} // namespace Xi
