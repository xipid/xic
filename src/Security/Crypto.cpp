/**
 * @file Crypto.cpp
 * @brief Implementation of cryptographic utilities for the Xi framework.
 */

#include "../../include/Security/Crypto.hpp"
#include "../../include/Xi/Random.hpp"
extern "C" {
#include "../../packages/monocypher/monocypher.h"
}

namespace Security {

String zeros(usz len) {
  String s;
  if (len == 0)
    return s;
  u8 *tmp = new u8[len]();
  s.pushEach(tmp, len);
  delete[] tmp;
  return s;
}

String createIetfNonce(u64 nonce) {
  String buffer = zeros(12);
  u8 *d = buffer.data();
  for (int i = 0; i < 8; ++i) {
    d[4 + i] = (nonce >> (i * 8)) & 0xFF;
  }
  return buffer;
}

String streamXor(const String &key, u64 nonce, const String &text,
                 int counter) {
  if (key.size() != 32)
    return String();
  String result = zeros(text.size());
  String cryptoNonce = createIetfNonce(nonce);
  crypto_chacha20_ietf(result.data(), text.data(), text.size(), key.data(),
                       cryptoNonce.data(), counter);
  return result;
}

String createPoly1305Key(const String &key, u64 nonce) {
  return streamXor(key, nonce, zeros(32), 0);
}

String hash(const String &input, int length, const String &key) {
  if (length > 64 || length < 1)
    return String();
  String result = zeros(length);
  if (key.size() == 0)
    crypto_blake2b(result.data(), length, input.data(), input.size());
  else
    crypto_blake2b_keyed(result.data(), length, key.data(), key.size(),
                         input.data(), input.size());
  return result;
}

String randomBytes(usz len) {
  String s = zeros(len);
  secureRandomFill(s.data(), len);
  return s;
}

String kdf(const String &secret, const String &salt, const String &info,
           int length) {
  const int hashLen = 64;
  if (length > 255 * hashLen)
    return String();

  String prk = hash(secret, hashLen, salt); // PRK = Hash(salt, IKM)

  int numBlocks = (length + hashLen - 1) / hashLen;
  String okm;
  String t;

  for (int i = 1; i <= numBlocks; i++) {
    String expandInput;
    expandInput += t;
    expandInput += info;
    expandInput.push((u8)i);
    t = hash(expandInput, hashLen, prk);
    okm += t;
  }
  return okm.begin(0, length);
}

String kdf(const String &secret, const String &info, int length) {
  return kdf(secret, String(), info, length);
}

String publicKey(const String &privateKey) {
  if (privateKey.size() != 32)
    return String();
  String pub = zeros(32);
  crypto_x25519_public_key(pub.data(), privateKey.data());
  return pub;
}

KeyPair generateKeyPair() {
  String secret = randomBytes(32);
  String pub = publicKey(secret);
  return {pub, secret};
}

String sharedKey(const String &privateKey, const String &publicKey) {
  if (privateKey.size() != 32 || publicKey.size() != 32)
    return String();
  String shared = zeros(32);
  crypto_x25519(shared.data(), privateKey.data(), publicKey.data());
  return shared;
}

String makeProofed(const Array<KeyPair> &myKeys, const String &theirPublicKey) {
  String res;
  res.pushVarLong((long long)myKeys.size());
  for (usz i = 0; i < myKeys.size(); i++) {
    res += myKeys[i].publicKey;
    String shared = sharedKey(myKeys[i].secretKey, theirPublicKey);
    String h = hash(shared, 8);
    res += h;
  }
  return res;
}

Array<String> parseProofed(const String &proofed, const String &mySecretKey) {
  Array<String> res;
  usz at = 0;
  auto countRes = proofed.peekVarLong(at);
  if (countRes.error)
    return res;
  at += countRes.bytes;

  for (long long i = 0; i < countRes.value; ++i) {
    if (at + 40 > proofed.size())
      break;

    String pub = proofed.begin(at, at + 32);
    String providedHash = proofed.begin(at + 32, at + 40);
    at += 40;

    String shared = sharedKey(mySecretKey, pub);
    String expectedHash = hash(shared, 8);

    if (providedHash.constantTimeEquals(expectedHash, 8)) {
      res.push(pub);
    }
  }
  return res;
}

bool aeadSeal(const String &key, u64 nonce, AEADOptions &options) {
  if (key.size() != 32)
    return false;
  // 1. Encrypt (Counter 1)
  String ciphertext = streamXor(key, nonce, options.text, 1);

  // 2. Poly Key (Counter 0)
  String oneTimeKey = createPoly1305Key(key, nonce);

  // 3. Auth Data Construction
  u64 adLen = options.ad.size();
  u64 cipherLen = ciphertext.size();
  usz adPad = (16 - (adLen % 16)) % 16;
  usz cipherPad = (16 - (cipherLen % 16)) % 16;

  String dataToAuth;
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
  String tag = zeros(16);
  crypto_poly1305(tag.data(), dataToAuth.data(), dataToAuth.size(),
                  oneTimeKey.data());

  options.text = ciphertext;
  options.tag = tag.begin(0, options.tagLength);
  return true;
}

bool aeadOpen(const String &key, u64 nonce, AEADOptions &options) {
  if (key.size() != 32)
    return false;

  //  1. Poly Key
  String oneTimeKey = createPoly1305Key(key, nonce);

  // 2. Auth Data
  u64 adLen = options.ad.size();
  u64 cipherLen = options.text.size();
  usz adPad = (16 - (adLen % 16)) % 16;
  usz cipherPad = (16 - (cipherLen % 16)) % 16;

  String dataToAuth;
  dataToAuth += options.ad;
  dataToAuth += zeros(adPad);
  dataToAuth += options.text;
  dataToAuth += zeros(cipherPad);

  for (int i = 0; i < 8; ++i)
    dataToAuth.push((u8)((adLen >> (i * 8)) & 0xFF));
  for (int i = 0; i < 8; ++i)
    dataToAuth.push((u8)((cipherLen >> (i * 8)) & 0xFF));

  //  3. Verify
  String calculatedTag = zeros(16);
  crypto_poly1305(calculatedTag.data(), dataToAuth.data(), dataToAuth.size(),
                  oneTimeKey.data());

  if (!options.tag.constantTimeEquals(calculatedTag, options.tagLength))
    return false;

  // 4. Decrypt
  options.text = streamXor(key, nonce, options.text, 1);
  return true;
}

u32 _secureCounter = 0;

void secureRandomFill(u8 *buffer, usz size) {
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

void secureRandomFill(String &s, usz len) {
  if (len == 0 || s.size() < len)
    len = s.size();
  u8 *raw = const_cast<u8 *>(reinterpret_cast<const u8 *>(s.data()));
  if (raw)
    secureRandomFill(raw, len);
}

// -------------------------------------------------------------------------
// XEdDSA Sign & Verify (Using BLAKE2b)
// -------------------------------------------------------------------------

static const u8 L_BYTES[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
    0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
};

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

// -------------------------------------------------------------------------
// Corrected XEdDSA Sign & Verify
// -------------------------------------------------------------------------

String signX(const String &privateKey, const String &text) {
  if (privateKey.size() != 32)
    return String();

  u8 a[32];       // Clamped private scalar
  u8 A[32];       // Target Ed25519 point
  u8 a_prime[32]; // Adjusted scalar

  // 1. Correctly compute the target public key point directly from your X25519 private key
  u8 pub_x[32];
  crypto_x25519_public_key(pub_x, privateKey.data());
  crypto_x25519_to_eddsa(A, pub_x);

  // 2. Trim and process your scalar values
  crypto_eddsa_trim_scalar(a, privateKey.data());
  
  u8 a_padded[64] = {0};
  for (int i = 0; i < 32; ++i) a_padded[i] = a[i];
  u8 a_mod_L[32];
  crypto_eddsa_reduce(a_mod_L, a_padded);

  // 3. Check the sign bit of the native point derived from the scalar
  u8 A_check[32];
  crypto_eddsa_scalarbase(A_check, a_mod_L);

  // CRITICAL FIX: Match the sign bit mapping explicitly with Monocypher's internal coordinate output
  if ((A_check[31] & 0x80) != (A[31] & 0x80)) {
    negate_scalar_mod_L(a_prime, a_mod_L);
  } else {
    for (int i = 0; i < 32; ++i) a_prime[i] = a_mod_L[i];
  }

  // 4. Force target public key sign bit clear for signature transmission standard compatibility
  A[31] &= 0x7F;

  // 5. Build a secure, deterministic random nonce prefix
  u8 secret[64];
  crypto_blake2b(secret, 64, privateKey.data(), 32);
  u8 *prefix = secret + 32;

  crypto_blake2b_ctx ctx;
  crypto_blake2b_init(&ctx, 64);
  crypto_blake2b_update(&ctx, prefix, 32);
  crypto_blake2b_update(&ctx, text.data(), text.size());
  u8 hash_out[64];
  crypto_blake2b_final(&ctx, hash_out);

  u8 r[32];
  crypto_eddsa_reduce(r, hash_out);

  // 6. Compute R point value
  u8 R[32];
  crypto_eddsa_scalarbase(R, r);

  // 7. Calculate signature check hash
  crypto_blake2b_init(&ctx, 64);
  crypto_blake2b_update(&ctx, R, 32);
  crypto_blake2b_update(&ctx, A, 32); // Match unsigned standard frame
  crypto_blake2b_update(&ctx, text.data(), text.size());
  crypto_blake2b_final(&ctx, hash_out);

  u8 h[32];
  crypto_eddsa_reduce(h, hash_out);

  // 8. Finalize S scalar tracking
  u8 S[32];
  crypto_eddsa_mul_add(S, h, a_prime, r);

  String signature = zeros(64);
  u8 *sigData = signature.data();
  for (int i = 0; i < 32; i++) {
    sigData[i] = R[i];
    sigData[i + 32] = S[i];
  }

  crypto_wipe(a, 32);
  crypto_wipe(a_prime, 32);
  crypto_wipe(r, 32);
  crypto_wipe(secret, 64);

  return signature;
}

bool verifyX(const String &publicKey, const String &text, const String &signature) {
  if (publicKey.size() != 32 || signature.size() != 64)
    return false;

  u8 A[32];
  crypto_x25519_to_eddsa(A, publicKey.data());
  
  // Clear the sign bit explicitly to conform with your signing generation scheme standard
  A[31] &= 0x7F;

  crypto_blake2b_ctx ctx;
  crypto_blake2b_init(&ctx, 64);
  crypto_blake2b_update(&ctx, signature.data(), 32); 
  crypto_blake2b_update(&ctx, A, 32);                
  crypto_blake2b_update(&ctx, text.data(), text.size());
  u8 hash_out[64];
  crypto_blake2b_final(&ctx, hash_out);

  u8 h[32];
  crypto_eddsa_reduce(h, hash_out);

  // Monocypher return parity verification check
  return (crypto_eddsa_check_equation((const u8 *)signature.data(), A, h) == 0);
}

} // namespace Security
