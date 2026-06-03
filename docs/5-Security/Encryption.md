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

## Best Practices

1.  **Nonce Management**: Never reuse a nonce with the same key. For `aeadSeal`, ensure your counter or random nonce is unique for every message.
2.  **Zeroing Memory**: Use `Security::zeros(len)` to create buffers for sensitive data, ensuring they are properly initialized.
3.  **Key Storage**: Never hardcode secret keys in your source code. Use the `Resource::File` module to load them from a secure partition or environment variables.
