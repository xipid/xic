#ifndef RHO_RAILWAY_HPP
#define RHO_RAILWAY_HPP

#include "../Xi/Array.hpp"
#include "../Xi/Crypto.hpp"
#include "../Xi/Func.hpp"
#include "../Xi/Map.hpp"
#include "../Xi/String.hpp"

namespace Xi {
struct RawCart {
  u8 header;
  u64 nonce;
  Xi::String hmac;
  Xi::String cipherText;
};

class RailwayStation {
public:
  // Identity
  Xi::String name = "Station";

  // Routing State
  u64 rail = 0;
  bool anycast = false;
  bool allDrain = false;

  // Stats
  u64 lastSeen = 0;
  u64 lastSent = 0;

  // Encryption
  bool isSecure = false;
  Xi::String key;
  u64 nonceCounter = 0;
  u64 slidingWindow = 0;

  // Collision Avoidance
  Xi::Array<u64> availableRails;

  // Meta
  Xi::Map<u64, Xi::String> meta;
  Xi::Map<u64, Xi::String> theirMeta;

  // Topology
  Xi::Array<RailwayStation *> parentStations;

  static Xi::String serializeCart(u8 header, u64 nonce, const Xi::String &hmac,
                                  const Xi::String &cipherText) {
    Xi::String raw;
    raw.push(header);
    if ((header & 1) != 0) {
      raw.pushVarLong((long long)nonce);
      raw += hmac;
    }
    raw += cipherText;
    return raw;
  }

  static RawCart deserializeCart(const Xi::String &raw) {
    RawCart rc = {0, 0, Xi::String(), Xi::String()};
    usz cursor = 0;
    if (raw.size() == 0)
      return rc;
    rc.header = raw[cursor++];
    if ((rc.header & 1) != 0) {
      auto res = raw.peekVarLong(cursor);
      if (!res.error) {
        rc.nonce = (u64)res.value;
        cursor += res.bytes;
      }
      if (cursor + 8 <= raw.length()) {
        rc.hmac = raw.substring(cursor, cursor + 8);
        cursor += 8;
      }
    }
    if (cursor < raw.size()) {
      rc.cipherText = raw.substring(cursor, raw.size());
    }
    return rc;
  }

  RailwayStation() {
    anycast = false;
    allDrain = true;
    for (int i = 0; i < 10; i++) {
      availableRails.push((u64)Xi::millis() + (u64)i * 12345ULL);
    }
  }

  u64 enrail() {
    if (availableRails.size() == 0)
      return 0;
    int idx = Xi::millis() % availableRails.size();
    rail = availableRails[idx];
    return rail;
  }

  // --- Listeners ---

  Xi::Func<void(Xi::String, u64, RailwayStation *)> cartListener;
  void onCart(Xi::Func<void(Xi::String, u64, RailwayStation *)> cb) {
    cartListener = Xi::Move(cb);
  }
  void offCart() {
    cartListener = Xi::Func<void(Xi::String, u64, RailwayStation *)>();
  }

  Xi::Func<void(u8, u64, Xi::String, Xi::String, RailwayStation *)>
      rawCartListener;
  void onRawCart(
      Xi::Func<void(u8, u64, Xi::String, Xi::String, RailwayStation *)> cb) {
    rawCartListener = Xi::Move(cb);
  }
  void offRawCart() {
    rawCartListener =
        Xi::Func<void(u8, u64, Xi::String, Xi::String, RailwayStation *)>();
  }

  Xi::Func<void(u8, u64, Xi::String, Xi::String, RailwayStation *)>
      outboxRawCartListener;
  void onOutboxRawCartListener(
      Xi::Func<void(u8, u64, Xi::String, Xi::String, RailwayStation *)> cb) {
    outboxRawCartListener = Xi::Move(cb);
  }

  // --- Topology ---

  void addStation(RailwayStation &otherStation) {
    parentStations.push(&otherStation);

    otherStation.onCart([this](Xi::String data, u64 r, RailwayStation *origin) {
      if (this->cartListener.isValid()) {
        this->cartListener(data, r, origin);
      }
    });

    otherStation.onRawCart([this](u8 header, u64 nonce, Xi::String hmac,
                                  Xi::String cipherText,
                                  RailwayStation *origin) {
      this->pushRaw(header, nonce, hmac, cipherText, origin);
    });
  }

  void removeStation(RailwayStation &otherStation) {
    for (usz i = 0; i < parentStations.size(); ++i) {
      if (parentStations[i] == &otherStation) {
        parentStations.splice(i, 1);
        break;
      }
    }
    otherStation.offCart();
    otherStation.offRawCart();
  }

  // --- Sending ---

  usz sendIndex = 0;

  void pushOutboxRawCart(u8 header, u64 nonce, Xi::String hmac,
                         Xi::String cipherText, RailwayStation *origin) {
    if (outboxRawCartListener.isValid()) {
      outboxRawCartListener(header, nonce, hmac, cipherText, origin);
    } else if (parentStations.size() > 0) {
      RailwayStation *target =
          parentStations[sendIndex % parentStations.size()];
      sendIndex++;
      target->pushOutboxRawCart(header, nonce, hmac, cipherText, origin);
    }
  }

  void push(Xi::String data) {
    if (parentStations.size() == 0 && !outboxRawCartListener.isValid())
      return;

    u8 header = 0;
    if (isSecure)
      header |= 1;
    if (anycast)
      header |= 4;

    Xi::String metaBlob;
    Xi::Map<u64, Xi::String> delta;
    for (auto &kv : meta) {
      const Xi::String *existing = theirMeta.get(kv.key);
      if (existing && existing->constantTimeEquals(kv.value))
        continue;
      delta.put(kv.key, kv.value);
    }

    if (delta.size() > 0) {
      header |= 2;
      metaBlob += delta.serialize();
    }

    Xi::String boxedData;
    boxedData.pushVarLong((long long)data.length());
    boxedData += data;

    Xi::String innerPayload = boxedData + metaBlob;

    // <HeaderByte: Byte> <Nonce: VarLong if isSecure> <HMAC: 8 Bytes> <Rail:
    // VarLong> { encryptedIfSecure: <Data: String> <Meta: Map> } The spec
    // defines building the final raw cart as putting header, nonce, hmac, rail,
    // inner payload. However, pushOutboxRawCart expects only header, nonce,
    // hmac, cipherText. So cipherText must include Rail + InnerPayload in the
    // final byte buffer going down, Or we modify pushOutboxRawCart to include
    // Rail? "poly1305(HeaderByte + Nonce + Rail + Data + Meta)" Let's pack Rail
    // + Data + Meta into `cipherText` for routing down.

    Xi::String unencryptedInner;
    unencryptedInner.pushVarLong((long long)rail);
    unencryptedInner += innerPayload;

    u64 usedNonce = 0;
    Xi::String hmac;
    Xi::String cipherText;

    if (isSecure) {
      usedNonce = ++nonceCounter;
      Xi::AEADOptions opt;
      opt.text = unencryptedInner;

      opt.ad = Xi::String();
      opt.ad += (char)header;
      opt.ad.pushVarLong((long long)usedNonce);

      opt.tagLength = 8;
      if (Xi::aeadSeal(key, usedNonce, opt)) {
        hmac = opt.tag;
        cipherText = opt.text;
      } else {
        return; // Crypto error
      }
    } else {
      cipherText = unencryptedInner;
      hmac = Xi::String();
      for (int i = 0; i < 8; i++)
        hmac.push('\0');
    }

    lastSent = Xi::millis();
    pushOutboxRawCart(header, usedNonce, hmac, cipherText, this);
  }

  // --- Receiving ---

  void pushRaw(u8 header, u64 nonce, Xi::String hmac, Xi::String cipherText,
               RailwayStation *origin) {
    if (rawCartListener.isValid()) {
      rawCartListener(header, nonce, hmac, cipherText, origin);
    }

    bool cartIsSecure = (header & 1) != 0;
    bool cartHasMeta = (header & 2) != 0;
    bool cartAnycast = (header & 4) != 0;

    Xi::String plain;

    if (isSecure) {
      if (!cartIsSecure)
        return;

      Xi::AEADOptions opt;
      opt.text = cipherText;
      opt.tag = hmac;
      opt.tagLength = 8;

      opt.ad = Xi::String();
      opt.ad += (char)header;
      opt.ad.pushVarLong((long long)nonce);

      if (Xi::aeadOpen(key, nonce, opt)) {
        plain = opt.text;
      } else {
        return; // HMAC failed
      }
    } else {
      plain = cipherText;
    }

    if (plain.size() == 0)
      return;

    usz cursor = 0;
    auto railRes = plain.peekVarLong(cursor);
    if (railRes.error)
      return;
    u64 cartRail = (u64)railRes.value;
    cursor += railRes.bytes;

    // Filter
    bool accept = false;
    if (this->rail == 0) {
      if (!cartAnycast || this->allDrain)
        accept = true;
    } else {
      if (this->rail == cartRail &&
          (this->anycast == cartAnycast || this->allDrain))
        accept = true;
    }

    if (!accept)
      return;

    // Collision avoidance update
    for (usz i = 0; i < availableRails.size(); ++i) {
      if (availableRails[i] == cartRail) {
        availableRails.splice(i, 1);
        availableRails.push((u64)Xi::millis() + 1337);
        break;
      }
    }

    auto sizeRes = plain.peekVarLong(cursor);
    if (sizeRes.error)
      return;
    cursor += sizeRes.bytes;
    u64 dataSize = (u64)sizeRes.value;
    Xi::String decodedData;
    if (cursor + (usz)dataSize <= plain.length()) {
      decodedData = plain.substring(cursor, cursor + (usz)dataSize);
      cursor += (usz)dataSize;
    }

    if (cartHasMeta && cursor < plain.length()) {
      auto decodedMeta = Xi::Map<u64, Xi::String>::deserialize(plain, cursor);
      for (auto &kv : decodedMeta) {
        theirMeta.put(kv.key, kv.value);
      }
    }

    lastSeen = Xi::millis();
    if (cartListener.isValid()) {
      cartListener(decodedData, cartRail, origin);
    }
  }
};

} // namespace Xi

#endif
