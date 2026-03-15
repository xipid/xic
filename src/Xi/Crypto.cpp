#include <Xi/Crypto.hpp>

namespace Xi {

Xi::String zeros(usz len) {
  Xi::String s;
  if (len == 0)
    return s;
  u8 *tmp = new u8[len]();
  s.pushEach(tmp, len);
  delete[] tmp;
  return s;
}

Xi::String createIetfNonce(u64 nonce) {
  Xi::String buffer = zeros(12);
  u8 *d = buffer.data();
  for (int i = 0; i < 8; ++i) {
    d[4 + i] = (nonce >> (i * 8)) & 0xFF;
  }
  return buffer;
}

Xi::String streamXor(const Xi::String &key, u64 nonce,
                             const Xi::String &text, int counter) {
  if (key.size() != 32)
    return Xi::String();
  Xi::String result = zeros(text.size());
  Xi::String cryptoNonce = createIetfNonce(nonce);
  crypto_chacha20_ietf(result.data(), text.data(), text.size(), key.data(),
                       cryptoNonce.data(), counter);
  return result;
}

Xi::String createPoly1305Key(const Xi::String &key, u64 nonce) {
  return streamXor(key, nonce, zeros(32), 0);
}

Xi::String hash(const Xi::String &input, int length,
                       const Xi::String &key) {
  if (length > 64 || length < 1)
    return Xi::String();
  Xi::String result = zeros(length);
  if (key.size() == 0)
    crypto_blake2b(result.data(), length, input.data(), input.size());
  else
    crypto_blake2b_keyed(result.data(), length, key.data(), key.size(),
                         input.data(), input.size());
  return result;
}

Xi::String randomBytes(usz len) {
  Xi::String s = zeros(len);
  Xi::secureRandomFill(s.data(), len);
  return s;
}

Xi::String kdf(const Xi::String &secret, const Xi::String &salt,
                      const Xi::String &info, int length) {
  const int hashLen = 64;
  if (length > 255 * hashLen)
    return Xi::String();

  Xi::String prk = hash(secret, hashLen, salt); // PRK = Hash(salt, IKM)

  int numBlocks = (length + hashLen - 1) / hashLen;
  Xi::String okm;
  Xi::String t;

  for (int i = 1; i <= numBlocks; i++) {
    Xi::String expandInput;
    expandInput += t;
    expandInput += info;
    expandInput.push((u8)i);
    t = hash(expandInput, hashLen, prk);
    okm += t;
  }
  return okm.begin(0, length);
}

Xi::String kdf(const Xi::String &secret, const Xi::String &info,
                      int length) {
  return kdf(secret, Xi::String(), info, length);
}

Xi::String publicKey(const Xi::String &privateKey) {
  if (privateKey.size() != 32)
    return Xi::String();
  Xi::String pub = zeros(32);
  crypto_x25519_public_key(pub.data(), privateKey.data());
  return pub;
}

KeyPair generateKeyPair() {
  Xi::String secret = randomBytes(32);
  Xi::String pub = publicKey(secret);
  return {pub, secret};
}

Xi::String sharedKey(const Xi::String &privateKey,
                            const Xi::String &publicKey) {
  if (privateKey.size() != 32 || publicKey.size() != 32)
    return Xi::String();
  Xi::String shared = zeros(32);
  crypto_x25519(shared.data(), privateKey.data(), publicKey.data());
  return shared;
}

Xi::String makeProofed(const Array<KeyPair> &myKeys,
                              const Xi::String &theirPublicKey) {
  Xi::String res;
  res.pushVarLong((long long)myKeys.size());
  for (usz i = 0; i < myKeys.size(); i++) {
    res += myKeys[i].publicKey;
    Xi::String shared = sharedKey(myKeys[i].secretKey, theirPublicKey);
    Xi::String h = hash(shared, 8);
    res += h;
  }
  return res;
}

Array<Xi::String> parseProofed(const Xi::String &proofed,
                                      const Xi::String &mySecretKey) {
  Array<Xi::String> res;
  usz at = 0;
  auto countRes = proofed.peekVarLong(at);
  if (countRes.error)
    return res;
  at += countRes.bytes;

  for (long long i = 0; i < countRes.value; ++i) {
    if (at + 40 > proofed.size())
      break;

    Xi::String pub = proofed.begin(at, at + 32);
    Xi::String providedHash = proofed.begin(at + 32, at + 40);
    at += 40;

    Xi::String shared = sharedKey(mySecretKey, pub);
    Xi::String expectedHash = hash(shared, 8);

    if (providedHash.constantTimeEquals(expectedHash, 8)) {
      res.push(pub);
    }
  }
  return res;
}

bool aeadSeal(const Xi::String &key, u64 nonce, AEADOptions &options) {
  if (key.size() != 32)
    return false;
  // 1. Encrypt (Counter 1)
  Xi::String ciphertext = streamXor(key, nonce, options.text, 1);

  // 2. Poly Key (Counter 0)
  Xi::String oneTimeKey = createPoly1305Key(key, nonce);

  // 3. Auth Data Construction
  u64 adLen = options.ad.size();
  u64 cipherLen = ciphertext.size();
  usz adPad = (16 - (adLen % 16)) % 16;
  usz cipherPad = (16 - (cipherLen % 16)) % 16;

  Xi::String dataToAuth;
  dataToAuth += options.ad;
  dataToAuth += zeros(adPad);
  dataToAuth += ciphertext;
  dataToAuth += zeros(cipherPad);

  // Explicitly shift as u64 to avoid UB on 32-bit systems (ESP32)
  for (int i = 0; i < 8; ++i)
    dataToAuth.push((u8)((adLen >> (i * 8)) & 0xFF));
  for (int i = 0; i < 8; ++i)
    dataToAuth.push((u8)((cipherLen >> (i * 8)) & 0xFF));

  //  4. Calc Tag
  Xi::String tag = zeros(16);
  crypto_poly1305(tag.data(), dataToAuth.data(), dataToAuth.size(),
                  oneTimeKey.data());

  options.text = ciphertext;
  options.tag = tag.begin(0, options.tagLength);
  return true;
}

bool aeadOpen(const Xi::String &key, u64 nonce, AEADOptions &options) {
  if (key.size() != 32)
    return false;

  //  1. Poly Key
  Xi::String oneTimeKey = createPoly1305Key(key, nonce);

  // 2. Auth Data
  u64 adLen = options.ad.size();
  u64 cipherLen = options.text.size();
  usz adPad = (16 - (adLen % 16)) % 16;
  usz cipherPad = (16 - (cipherLen % 16)) % 16;

  Xi::String dataToAuth;
  dataToAuth += options.ad;
  dataToAuth += zeros(adPad);
  dataToAuth += options.text;
  dataToAuth += zeros(cipherPad);

  for (int i = 0; i < 8; ++i)
    dataToAuth.push((u8)((adLen >> (i * 8)) & 0xFF));
  for (int i = 0; i < 8; ++i)
    dataToAuth.push((u8)((cipherLen >> (i * 8)) & 0xFF));

  //  3. Verify
  Xi::String calculatedTag = zeros(16);
  crypto_poly1305(calculatedTag.data(), dataToAuth.data(), dataToAuth.size(),
                  oneTimeKey.data());

  if (!options.tag.constantTimeEquals(calculatedTag, options.tagLength))
    return false;

  // 4. Decrypt
  options.text = streamXor(key, nonce, options.text, 1);
  return true;
}

void secureRandomFill(u8 *buffer, usz size) {
  if (!_randomInitialized)
    randomSeed();
  const u8 *key = reinterpret_cast<const u8 *>(&_randomPool[4]);
  const u8 *nonce = reinterpret_cast<const u8 *>(&_randomPool[12]);
#if defined(__GNUC__) || defined(__clang__)
  __builtin_memset(buffer, 0, size);
#else
  for (usz i = 0; i < size; ++i)
    buffer[i] = 0;
#endif
  crypto_chacha20_ietf(buffer, buffer, size, key, nonce, _secureCounter);
  u32 blocks = (u32)((size + 63) / 64);
  _secureCounter += blocks;
}

// -------------------------------------------------------------------------
// XEdDSA Sign & Verify (Using BLAKE2b)
// -------------------------------------------------------------------------

// Ed25519 Curve order (L), little endian.
static const u8 L_BYTES[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
    0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
};

// Helper: negate scalar a mod L => a' = L - a
void negate_scalar_mod_L(u8 a_out[32], const u8 a[32]) {
  u32 carry = 0;
  for (int i = 0; i < 32; i++) {
    i32 temp = (i32)L_BYTES[i] - a[i] - carry;
    if (temp < 0) {
      temp += 256;
      carry = 1;
    } else {
      carry = 0;
    }
    a_out[i] = temp & 0xFF;
  }
}

Xi::String signX(const Xi::String &privateKey, const Xi::String &text) {
  if (privateKey.size() != 32)
    return Xi::String();

  u8 a[32]; // private scalar
  u8 A[32]; // corresponding Ed25519 public key
  u8 a_prime[32];

  // 1. Get Ed25519 public key from X25519 private key
  u8 pub_x[32];
  crypto_x25519_public_key(pub_x, privateKey.data());
  crypto_x25519_to_eddsa(A, pub_x);

  // 2. Clamp scalar `a`
  crypto_eddsa_trim_scalar(a, privateKey.data());

  // 3. Reduce `a` modulo L
  u8 a_padded[64] = {0};
  for (int i = 0; i < 32; ++i)
    a_padded[i] = a[i];
  u8 a_mod_L[32];
  crypto_eddsa_reduce(a_mod_L, a_padded);

  // 4. To match XEdDSA definition we need to see if A has negative sign bit.
  u8 A_check[32];
  crypto_eddsa_scalarbase(A_check,
                          a_mod_L); // Use reduced scalar for correctness
  // Monocypher's public keys have their sign bit in the most significant bit.
  if (A_check[31] & 0x80) {
    negate_scalar_mod_L(a_prime, a_mod_L);
  } else {
    for (int i = 0; i < 32; ++i)
      a_prime[i] = a_mod_L[i];
  }

  // 4. Generate deterministic random nonce r
  // r = BLAKE2b(secret_prefix || text) mod L
  u8 secret[64];
  crypto_blake2b(secret, 64, privateKey.data(), 32);
  u8 *prefix = secret + 32; // The second 32 bytes

  crypto_blake2b_ctx ctx;
  crypto_blake2b_init(&ctx, 64);
  crypto_blake2b_update(&ctx, prefix, 32);
  crypto_blake2b_update(&ctx, text.data(), text.size());
  u8 hash_out[64];
  crypto_blake2b_final(&ctx, hash_out);

  u8 r[32];
  crypto_eddsa_reduce(r, hash_out); // r = hash mod L

  // 5. R = rB
  u8 R[32];
  crypto_eddsa_scalarbase(R, r);

  // 6. h = BLAKE2b(R || A || text) mod L
  A[31] &= 0x7F; // XEdDSA requires hashing the unsigned generated A

  crypto_blake2b_init(&ctx, 64);
  crypto_blake2b_update(&ctx, R, 32);
  crypto_blake2b_update(&ctx, A, 32);
  crypto_blake2b_update(&ctx, text.data(), text.size());
  crypto_blake2b_final(&ctx, hash_out);

  u8 h[32];
  crypto_eddsa_reduce(h, hash_out); // h = hash mod L

  // 7. S = (r + h * a_prime) mod L
  u8 S[32];
  crypto_eddsa_mul_add(S, h, a_prime, r);

  Xi::String signature = zeros(64);
  for (int i = 0; i < 32; i++) {
    signature.data()[i] = R[i];
    signature.data()[i + 32] = S[i];
  }

  crypto_wipe(a, 32);
  crypto_wipe(a_prime, 32);
  crypto_wipe(r, 32);
  crypto_wipe(secret, 64);

  return signature;
}

bool verifyX(const Xi::String &publicKey, const Xi::String &text,
                    const Xi::String &signature) {
  if (publicKey.size() != 32 || signature.size() != 64)
    return false;

  // 1. Convert X25519 public key to Ed25519 public key A
  u8 A[32];
  crypto_x25519_to_eddsa(A, publicKey.data());

  // XEdDSA requires both hashing and checking against the unsigned A
  A[31] &= 0x7F;

  // 2. h = BLAKE2b(R || A || text) mod L
  crypto_blake2b_ctx ctx;
  crypto_blake2b_init(&ctx, 64);
  crypto_blake2b_update(&ctx, signature.data(), 32); // R
  crypto_blake2b_update(&ctx, A, 32);                // unsigned A
  crypto_blake2b_update(&ctx, text.data(), text.size());
  u8 hash_out[64];
  crypto_blake2b_final(&ctx, hash_out);

  u8 h[32];
  crypto_eddsa_reduce(h, hash_out);

  // 3. check R == sB - hA
  // Monocypher's internal function does exactly this for Ed25519
  // crypto_eddsa_check_equation(signature, public_key, h)
  if (crypto_eddsa_check_equation((const u8 *)signature.data(), A, h) == 0) {
    return true;
  }
  return false;
}

} // namespace Xi
