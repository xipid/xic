/**
 * @file Crypto.hpp
 * @brief Cryptographic utilities for the Xi framework using Monocypher.

 */

#ifndef XI_DATA_CRYPTO_HPP
#define XI_DATA_CRYPTO_HPP

#include "../Collection/String.hpp"

using namespace Collection;
using namespace Xi;

/**
 * @namespace LLT
 * @brief Loss-less Transformation.
 */
namespace LLT {

using namespace Xi;

/**
 * @struct AEADOptions
 * @brief Options for Authenticated Encryption with Associated Data (AEAD).
 */
struct XI_EXPORT AEADOptions {
  String text;        ///< Plaintext or ciphertext.
  String ad;          ///< Associated data (not encrypted, but authenticated).
  String tag;         ///< Authentication tag.
  int tagLength = 16; ///< Length of the authentication tag.
};

/**
 * @struct KeyPair
 * @brief Represents a public/private key pair.
 */
struct XI_EXPORT KeyPair {
  String publicKey; ///< The public part of the key pair.
  String secretKey; ///< The secret/private part of the key pair.
};

/**
 * @brief Returns a string of zeros of the specified length.
 */
String zeros(usz len);

/**
 * @brief Creates an IETF-compliant nonce from a 64-bit integer.
 */
String createIetfNonce(u64 nonce);

/**
 * @brief Performs stream XOR encryption/decryption.
 */
String streamXor(const String &key, u64 nonce, const String &text,
                 int counter = 0);

/**
 * @brief Creates a Poly1305 key from a key and nonce.
 */
String createPoly1305Key(const String &key, u64 nonce);

/**
 * @brief Performs BLAKE2b hashing.
 */
String hash(const String &input, int length = 64, const String &key = String());

/**
 * @brief Generates cryptographically secure random bytes.
 */
String randomBytes(usz len);

/**
 * @brief Key Derivation Function (KDF).
 */
String kdf(const String &secret, const String &salt, const String &info,
           int length);

/**
 * @brief Key Derivation Function (KDF) without explicit salt.
 */
String kdf(const String &secret, const String &info, int length);

/**
 * @brief Extracts a public key from a private key.
 */
String publicKey(const String &privateKey);

/**
 * @brief Generates a random X25519 key pair.
 */
KeyPair generateKeyPair();

/**
 * @brief Performs Diffie-Hellman key exchange.
 */
String sharedKey(const String &privateKey, const String &publicKey);

/**
 * @brief Encrypts data for a recipient using a proofed protocol.
 */
String makeProofed(const Array<KeyPair> &myKeys, const String &theirPublicKey);

/**
 * @brief Decrypts data using a proofed protocol.
 */
Array<String> parseProofed(const String &proofed, const String &mySecretKey);

/**
 * @brief Authenticated encryption (Seal).
 */
bool aeadSeal(const String &key, u64 nonce, AEADOptions &options);

/**
 * @brief Authenticated decryption (Open).
 */
bool aeadOpen(const String &key, u64 nonce, AEADOptions &options);

/**
 * @brief Fills a buffer with secure random data.
 */
void secureRandomFill(u8 *buffer, usz size);

/**
 * @brief Fills a string with cryptographically secure random bytes.
 */
void secureRandomFill(String &s, usz len);

/**
 * @brief Negates a scalar modulo L.
 */
void negate_scalar_mod_L(u8 a_out[32], const u8 a[32]);

/**
 * @brief Signs text using Ed25519.
 */
String signX(const String &privateKey, const String &text);

/**
 * @brief Verifies an Ed25519 signature.
 */
bool verifyX(const String &publicKey, const String &text,
             const String &signature);

} // namespace LLT

#endif // XI_DATA_CRYPTO_HPP