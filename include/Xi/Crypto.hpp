#ifndef XI_CRYPTO_HPP
#define XI_CRYPTO_HPP

#include "Log.hpp"
#include "Random.hpp"
#include "String.hpp"

extern "C" {
#include "../../packages/monocypher/monocypher.h"
}

namespace Xi {

// -------------------------------------------------------------------------
// Structs
// -------------------------------------------------------------------------

struct XI_EXPORT AEADOptions {
  Xi::String text;
  Xi::String ad;
  Xi::String tag;
  int tagLength = 16;
};

struct XI_EXPORT KeyPair {
  Xi::String publicKey;
  Xi::String secretKey;
};

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

Xi::String zeros(usz len);

Xi::String createIetfNonce(u64 nonce);

Xi::String streamXor(const Xi::String &key, u64 nonce, const Xi::String &text,
                     int counter = 0);

Xi::String createPoly1305Key(const Xi::String &key, u64 nonce);

Xi::String hash(const Xi::String &input, int length = 64,
                const Xi::String &key = Xi::String());

Xi::String randomBytes(usz len);

Xi::String kdf(const Xi::String &secret, const Xi::String &salt,
               const Xi::String &info, int length);

Xi::String kdf(const Xi::String &secret, const Xi::String &info, int length);

Xi::String publicKey(const Xi::String &privateKey);

KeyPair generateKeyPair();

Xi::String sharedKey(const Xi::String &privateKey, const Xi::String &publicKey);

/**
 * Proofed Protocol Make
 * Returns a serialized array of [PublicKey 32Bytes] [Blake2b(ECDH) 8Bytes]
 */
Xi::String makeProofed(const Array<KeyPair> &myKeys,
                       const Xi::String &theirPublicKey);

Array<Xi::String> parseProofed(const Xi::String &proofed,
                               const Xi::String &mySecretKey);

bool aeadSeal(const Xi::String &key, u64 nonce, AEADOptions &options);

bool aeadOpen(const Xi::String &key, u64 nonce, AEADOptions &options);

void secureRandomFill(u8 *buffer, usz size);

void negate_scalar_mod_L(u8 a_out[32], const u8 a[32]);

Xi::String signX(const Xi::String &privateKey, const Xi::String &text);

bool verifyX(const Xi::String &publicKey, const Xi::String &text,
             const Xi::String &signature);

} // namespace Xi

#endif // XI_CRYPTO_HPP