# Security: Encryption

The **Security** module provides high-level cryptographic primitives for secure communication and data protection. It is built on top of **Monocypher**, a modern, auditable C library that provides state-of-the-art security with a minimal footprint.

---

## Hash Functions

xic uses **BLAKE2b** for general-purpose hashing. It is faster than MD5 and more secure than SHA-3.

```cpp
using namespace Security;

// High-speed 64-byte hash
String h = hash("The quick brown fox");

// Keyed hash (for MAC or sensitive IDs)
String h2 = hash("Secret message", 64, "my-secret-key");
```

---

## Authenticated Encryption (AEAD)

We primarily use **XChacha20-Poly1305** for authenticated encryption. This ensures that data is both private and hasn't been tampered with.

```cpp
using namespace Security;

AEADOptions options;
options.text = "Sensitive data";
options.ad = "Metadata headers"; // Authenticated but not encrypted

String key = randomBytes(32);
u64 nonce = 12345;

if (aeadSeal(key, nonce, options)) {
    // options.text now contains ciphertext
    // options.tag contains the 16-byte MAC
}
```

---

## Public Key Cryptography

xic supports **Curve25519** for key exchange (X25519) and **Ed25519** for digital signatures.

### Key Exchange
```cpp
using namespace Security;

KeyPair alice = generateKeyPair();
KeyPair bob = generateKeyPair();

// Both compute the same shared master key
String sharedAlice = sharedKey(alice.secretKey, bob.publicKey);
String sharedBob = sharedKey(bob.secretKey, alice.publicKey);
```

### Digital Signatures
```cpp
using namespace Security;

// Sign a message
String sig = signX(alice.secretKey, "Audit log #42");

// Verify authenticity
if (verifyX(alice.publicKey, "Audit log #42", sig)) {
    // verified
}
```

---

## Secure Networking (Proofed Protocol)

The `makeProofed` and `parseProofed` functions implement an opinionated, multi-layered protocol that combines multiple key sets for advanced protection against forward-secrecy compromises.

```cpp
using namespace Security;

// Encrypt for a recipient
String packet = makeProofed({aliceKeys, secondaryKeys}, bobPublicKey);

// Recipient decrypts
Array<String> payload = parseProofed(packet, bobSecretKey);
```

---

## Trust Documents (Writs)

A `Writ` represents a cryptographic trust document (formerly a key certificate) that securely binds a public key to metadata properties, signed by a certifying authority. Writs are used to build verifiable trust chains in decentralized systems.

```cpp
#include <Security/Writ.hpp>
using namespace Security;

// Create a new writ
Writ writ;
writ.publicKey = alice.publicKey;
writ.meta[42] = "Node Identity";
writ.sign(authority.secretKey); // Signs public key + metadata

// Verify writ authenticity
if (writ.verify()) {
    // Verified successfully
}
```

### Writ API Reference

* **Properties**:
  * `String publicKey`: The 32-byte public key being certified.
  * `Map<u64, String> meta`: Key-value metadata bound to the public key.
  * `String signature`: The 64-byte Ed25519 signature verifying the public key and metadata.

* **Methods**:
  * `Writ(const String &bytes)`: Constructs and deserializes a writ from raw signed bytes.
  * `bool has(u64 k) const`: Returns true if metadata key `k` is present.
  * `String payload() const`: Serializes the public key and metadata payload (the bytes that are signed).
  * `String toString() const`: Serializes the entire writ (public key + metadata + signature).
  * `void sign(const String &privateKey)`: Signs the payload using the specified private key.
  * `bool verify() const`: Cryptographically verifies the signature against the public key.

* **Static Methods**:
  * `static String serialize(const Array<Writ> &writs)`: Serializes a list of writs into a single string.
  * `static Array<Writ> parseAll(const String &bytes)`: Parses and deserializes a list of writs from a byte string.
  * `static String childHash(const String &pub)`: Hashes a child public key.
  * `static Array<Array<Writ>> chains(const Array<Writ> &allWrits, const Array<String> &leafPublicKeyHashes, const Array<String> &rootPublicKeys)`: Finds valid cryptographic trust chains connecting leaf keys to root authority keys.

---

## Best Practices

1.  **Nonce Management**: Never reuse a nonce with the same key. For `aeadSeal`, ensure your counter or random nonce is unique for every message.
2.  **Zeroing Memory**: Use `Security::zeros(len)` to create buffers for sensitive data, ensuring they are properly initialized.
3.  **Key Storage**: Never hardcode secret keys in your source code. Use the `Resource::File` module to load them from a secure partition or environment variables.
