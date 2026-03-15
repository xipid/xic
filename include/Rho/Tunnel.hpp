#ifndef RHO_TUNNEL_HPP
#define RHO_TUNNEL_HPP

#include "../Xi/Array.hpp"
#include "../Xi/Crypto.hpp"
#include "../Xi/Func.hpp"
#include "../Xi/Map.hpp"
#include "../Xi/String.hpp"

namespace Xi {
struct Packet {
  Xi::String payload;
  u64 channel = 1;
  bool bypassHOL = false;
  bool important = true;
  u64 id = 0;
  u64 fragmentStartID = 0;
  u8 fragmentStatus = 0;
  Packet()
      : channel(1), bypassHOL(false), important(true), id(0),
        fragmentStartID(0), fragmentStatus(0) {}
  Packet(const Xi::String &p, u64 c = 1)
      : payload(p), channel(c), bypassHOL(false), important(true), id(0),
        fragmentStartID(0), fragmentStatus(0) {}
};

struct FromTo {
  u64 from;
  u64 to;
};
struct InflightBundle {
  u64 id;
  Xi::String data;
  bool important;
};

using PacketListener = Xi::Func<void(Packet)>;
using MapListener = Xi::Func<void(Xi::Map<u64, Xi::String>)>;
using VoidListener = Xi::Func<void()>;

class Tunnel {
public:
  Xi::String name = "Tunnel";
  Xi::String key;
  bool isSecure = false, isWindowed = false, isAsleep = false;
  bool isServer = false, switchReady = false;
  Xi::Map<u8, Xi::String> meta, otherMeta;
  u64 lastSent = 0, lastSentHeartbeat = 0, lastSeen = 0, lastReceived = 0;
  bool destroyAfterFlush = false, windowAfterFlush = false,
       secureAfterFlush = false, secureXAfterFlush = false;
  u64 aliveTimeout = 60000, disconnectTimeout = 120000;
  u64 lastSentNonce = 0, lastReceivedNonce = 0, receiveWindowMask = 0;
  Xi::Array<InflightBundle> inflightBundles, nonImportantInflightBundles;
  Xi::Array<InflightBundle> priorityResendQueue;
  usz resendPosition = 0;
  Xi::Array<u64> droppedBundles;
  Xi::Map<u64, Xi::String> reassemblyBuffer;
  Xi::Array<Packet> outbox;

  Tunnel() { clear(); }
  void clear() {
    u64 now = Xi::millis();
    lastSent = now;
    lastSentHeartbeat = now;
    lastSeen = now;
    isAsleep = false;
    isServer = false;
    switchReady = false;
    meta.clear();
    otherMeta.clear();
    destroyAfterFlush = false;
    lastSentNonce = 0;
    lastReceivedNonce = 0;
    receiveWindowMask = 0;
    aliveTimeout = 8000;
  }
  Xi::KeyPair ephemeralKeypair;
  Xi::String theirEphemeralPublic, intendedEpheHash;
  PacketListener packetListener;
  MapListener probeListener, announceListener, disconnectListener;
  VoidListener switchRequestListener, destroyListener, readyListener;

  void initEphemeral() { ephemeralKeypair = Xi::generateKeyPair(); }

  void enableSecurity(Xi::String s) {
    key = s;
    isSecure = true;
    lastSentNonce = 0;
    lastReceivedNonce = 0;
    receiveWindowMask = 0;
    outbox.clear();
  }
  void enableWindowing(int windowSize = 64) {
    isWindowed = true;
    lastSentNonce = 0;
    lastReceivedNonce = 0;
    receiveWindowMask = 0;
    outbox.clear();
  }
  void enableSecurityAfterFlush(const Xi::String &k) {
    if (k.length() == 32) {
      key = k;
      secureAfterFlush = true;
    }
  }
  void enableSecurityXAfterFlush() { secureXAfterFlush = true; }
  void enableWindowingAfterFlush() { windowAfterFlush = true; }
  void setAliveTimeout(u64 t) {
    aliveTimeout = t;
    if (t == 0)
      isAsleep = false;
  }
  void setDisconnectTimeout(u64 t) { disconnectTimeout = t; }

  void update() {
    u64 now = Xi::millis();
    if (disconnectTimeout > 0 && !destroyAfterFlush &&
        (now > lastSeen + disconnectTimeout)) {
      Xi::Map<u64, Xi::String> reason;
      reason.put(0, "Timeout");
      disconnect(reason);
      if (disconnectListener.isValid())
        disconnectListener(reason);
      destroyAfterFlush = true;
    }
  }

  void onPacket(PacketListener cb) { packetListener = Xi::Move(cb); }
  void onProbe(MapListener cb) { probeListener = Xi::Move(cb); }
  void onAnnounce(MapListener cb) { announceListener = Xi::Move(cb); }
  void onDisconnect(MapListener cb) { disconnectListener = Xi::Move(cb); }
  void onDestroy(VoidListener cb) { destroyListener = Xi::Move(cb); }
  void onSwitchRequest(VoidListener cb) {
    switchRequestListener = Xi::Move(cb);
  }
  void onReady(VoidListener cb) { readyListener = Xi::Move(cb); }

  void push(Packet pkt) { outbox.push(pkt); }
  void push(Xi::String s, u64 c = 1) { push(Packet(s, c)); }

  void probe(Xi::Map<u64, Xi::String> data) {
    Packet p;
    p.channel = 0;
    p.important = true;
    p.payload.pushVarLong(10);
    p.payload += data.serialize();
    push(p);
  }
  void announce(Xi::Map<u64, Xi::String> data) {
    Packet p;
    p.channel = 0;
    p.important = true;
    p.payload.pushVarLong(11);
    p.payload += data.serialize();
    push(p);
  }
  void ready(Xi::Map<u8, Xi::String> data) {
    Packet p;
    p.channel = 0;
    p.important = true;
    p.payload.pushVarLong(12);
    p.payload += data.serialize();
    push(p);
  }
  void disconnect(Xi::Map<u64, Xi::String> reason) {
    Packet p;
    p.channel = 0;
    p.important = true;
    p.payload.pushVarLong(1000); // 100 was shared with handshake
    p.payload += reason.serialize();
    push(p);
  }

  Xi::String generateSwitchRequest(Xi::String theirEpheKey) {
    if (theirEpheKey.length() != 32)
      return Xi::String();
    theirEphemeralPublic = theirEpheKey;
    if (!ephemeralKeypair.publicKey.length())
      ephemeralKeypair = Xi::generateKeyPair();
    Xi::String req;
    Xi::String serializedMeta;
    serializedMeta += meta.serialize();

    if (isSecure && key.length() == 32) {
      Xi::String theirHash = Xi::hash(theirEpheKey, 8),
                 myPub = ephemeralKeypair.publicKey,
                 toSign = theirHash + myPub + serializedMeta;
      Xi::String polyKey = Xi::createPoly1305Key(key, 0xFFFFFFFFFFFFFFFFULL);
      Xi::String fullTag = Xi::zeros(16);
      crypto_poly1305(fullTag.data(), toSign.data(), toSign.length(),
                      polyKey.data());
      req.pushEach(fullTag.data(), 8);
    } else
      req += Xi::zeros(8);
    req += Xi::hash(theirEpheKey, 8);
    req += ephemeralKeypair.publicKey;
    req += serializedMeta;
    Xi::String res;
    res.pushVarLong(20);
    res += req;
    return res;
  }

  void sendClientSwitchRequest(const Xi::Map<u64, Xi::String> &serverResponse) {
    const Xi::String *pub = serverResponse.get(2);
    if (pub && pub->length() == 32) {
      Xi::String req = generateSwitchRequest(*pub);
      push(Packet(req, 0));
    }
  }

  bool enableSecurityX() {
    if (theirEphemeralPublic.length() != 32 ||
        ephemeralKeypair.secretKey.length() != 32)
      return false;

    Xi::String shared =
        Xi::sharedKey(ephemeralKeypair.secretKey, theirEphemeralPublic);
    Xi::String newKey = Xi::kdf(shared, "RhoPufferV1", 32);

    enableSecurity(newKey);

    if (isServer) {
      switchReady = true;

      if (meta.size() > 0) {
        ready(meta);
      }

      if (readyListener.isValid())
        readyListener();
    } else {
      if (meta.size() > 0) {
        ready(meta);
      }
    }

    return true;
  }

  void parsePacket(const Xi::String &raw) {
    usz cursor = 0;
    u8 header = raw[cursor++];
    Packet p;
    p.fragmentStatus = header & 0x03;
    bool hasChannel = (header >> 2) & 1;
    p.bypassHOL = (header >> 3) & 1;
    if (isWindowed) {
      auto res = raw.peekVarLong(cursor);
      if (!res.error) {
        p.id = (u64)res.value;
        cursor += res.bytes;
      }
    } else
      p.id = 0;

    if (hasChannel) {
      auto res = raw.peekVarLong(cursor);
      if (!res.error) {
        p.channel = (u64)res.value;
        cursor += res.bytes;
      }
    } else
      p.channel = 1;
    if (p.fragmentStatus != 0) {
      auto res = raw.peekVarLong(cursor);
      if (!res.error) {
        p.fragmentStartID = (u64)res.value;
        cursor += res.bytes;
      }
    }
    if (cursor < raw.length())
      p.payload = raw.substring(cursor, raw.length());
    dispatchPacket(p);
  }

  void dispatchPacket(const Packet &p) {
    if (p.channel == 0) {
      usz pAt = 0;
      auto typeRes = p.payload.peekVarLong(pAt);
      if (typeRes.error)
        return;
      u64 type = (u64)typeRes.value;
      pAt += typeRes.bytes;

      if (type == 0) {
        if (!isWindowed)
          return;
        auto countRes = p.payload.peekVarLong(pAt);
        if (countRes.error)
          return;
        pAt += countRes.bytes;
        u64 count = (u64)countRes.value;
        for (u64 i = 0; i < count; ++i) {
          auto fRes = p.payload.peekVarLong(pAt);
          if (fRes.error)
            break;
          pAt += fRes.bytes;
          auto tRes = p.payload.peekVarLong(pAt);
          if (tRes.error)
            break;
          pAt += tRes.bytes;
          for (u64 x = (u64)fRes.value; x <= (u64)tRes.value; ++x)
            removeInflight(x);
        }
        // 2. NACKs (Unavailable packets) -> Selective Resend
        auto countRes2 = p.payload.peekVarLong(pAt);
        if (countRes2.error)
          return;
        pAt += countRes2.bytes;
        u64 count2 = (u64)countRes2.value;
        for (u64 i = 0; i < count2; ++i) {
          auto fRes = p.payload.peekVarLong(pAt);
          if (fRes.error)
            break;
          pAt += fRes.bytes;
          auto tRes = p.payload.peekVarLong(pAt);
          if (tRes.error)
            break;
          pAt += tRes.bytes;
          // Selective Repeat: Find the exact missed bundles and queue them
          for (usz j = 0; j < inflightBundles.size(); ++j) {
            if (inflightBundles[j].id >= (u64)fRes.value &&
                inflightBundles[j].id <= (u64)tRes.value) {
              InflightBundle ib;
              ib.id = inflightBundles[j].id;
              ib.data = Xi::String(inflightBundles[j].data.data(),
                                   inflightBundles[j].data.length());
              ib.important = true;
              priorityResendQueue.push(Xi::Move(ib));
            }
          }
        }
      } else if (type == 10) {
        if (probeListener.isValid()) {
          probeListener(Xi::Map<u64, Xi::String>::deserialize(p.payload, pAt));
        }
      } else if (type == 11) {
        if (announceListener.isValid())
          announceListener(
              Xi::Map<u64, Xi::String>::deserialize(p.payload, pAt));
      } else if (type == 1000) {
        if (disconnectListener.isValid())
          disconnectListener(
              Xi::Map<u64, Xi::String>::deserialize(p.payload, pAt));
      } else if (type == 20) {
        pAt += 8;
        if (pAt + 8 <= p.payload.length()) {
          intendedEpheHash = p.payload.substring(pAt, pAt + 8);
          pAt += 8;
        }
        if (pAt + 32 <= p.payload.length()) {
          theirEphemeralPublic = p.payload.substring(pAt, pAt + 32);
          pAt += 32;
        }
        otherMeta = Xi::Map<u8, Xi::String>::deserialize(p.payload, pAt);
        if (switchRequestListener.isValid())
          switchRequestListener();
      } else if (type == 12) {
        if (pAt < p.payload.length())
          otherMeta = Xi::Map<u8, Xi::String>::deserialize(p.payload, pAt);
        if (!switchReady) {
          switchReady = true;
          if (readyListener.isValid())
            readyListener();
        }
      }
    } else {
      if (packetListener.isValid()) {
        packetListener(p);
      }
    }
  }
  void parse(const Xi::String &bundle) {
    if (isAsleep)
      isAsleep = false;
    usz at = 0;
    u64 bID = 0;
    if (isWindowed) {
      auto res = bundle.peekVarLong(at);
      if (res.error)
        return;
      bID = (u64)res.value;
      at += res.bytes;
      if (hasReceived(bID))
        return;
    } else
      bID = lastReceivedNonce + 1; // Expected next

    Xi::String plain;
    Xi::String payload = bundle.substring(at, bundle.length());
    if (isSecure) {
      if (payload.length() < 9)
        return;
      Xi::String aad;
      if (isWindowed)
        aad.pushVarLong((long long)bID);

      Xi::AEADOptions opt;
      opt.tag = payload.substring(0, 8);
      opt.text = payload.substring(8, payload.length());
      opt.ad = aad;
      opt.tagLength = 8;

      if (!Xi::aeadOpen(key, bID, opt)) {
        return;
      }
      plain = opt.text;
    } else
      plain = payload;

    if (plain.length() == 0)
      return;
    // Success! Update nonce tracker.
    lastSeen = Xi::millis();
    if (isWindowed)
      pretendReceived(bID);
    else
      lastReceivedNonce = bID;

    usz pAt = 0;
    if (pAt >= plain.length())
      return; // Safety
    u8 hb = plain[pAt++];
    bool padded = (hb >> 2) & 1, single = (hb >> 3) & 1;
    Xi::String content;
    if (padded) {
      auto res = plain.peekVarLong(pAt);
      if (res.error)
        return;
      pAt += res.bytes;
      u64 pLen = (u64)res.value;
      if (pAt + (usz)pLen <= plain.length())
        content = plain.substring(pAt, pAt + (usz)pLen);
      else
        return;
    } else {
      if (pAt > plain.length())
        return; // Should not happen
      content = plain.substring(pAt, plain.length());
    }
    if (single)
      parsePacket(content);
    else {
      usz sAt = 0;
      while (sAt < content.length()) {
        auto res = content.peekVarLong(sAt);
        if (res.error)
          break;
        sAt += res.bytes;
        u64 pkL = (u64)res.value;
        if (sAt + (usz)pkL > content.length())
          break;
        parsePacket(content.substring(sAt, sAt + (usz)pkL));
        sAt += (usz)pkL;
      }
    }

    if (isSecure && !switchReady) {
      switchReady = true;
      if (readyListener.isValid())
        readyListener();
    }
  }

  void build(usz bBS = 32, usz bMS = 1400) {
    if (isAsleep)
      return;

    //
    while (outbox.size() > 0) {
      Xi::String py;
      py.push(0);
      bool single = false, important = false;
      usz consumed = 0;
      Xi::String tF;
      serializePacket(tF, outbox[0]);

      usz overhead = 1 + (isWindowed ? 9 : 0) + 8 + bBS,
          avail = (bMS > overhead) ? bMS - overhead : 0;

      if (isWindowed && tF.length() > avail) {

        Packet p = outbox.shift();
        usz fS = (avail > 15) ? avail - 15 : 1, off = 0;
        while (off < p.payload.length()) {
          usz len =
              (p.payload.length() - off > fS) ? fS : p.payload.length() - off;
          Packet f(p.payload.substring(off, off + (usz)len), p.channel);
          f.id = p.id;
          f.important = p.important;
          f.fragmentStartID = p.id;
          f.fragmentStatus = (off == 0)
                                 ? (p.payload.size() <= off + len ? 0 : 1)
                                 : (p.payload.size() <= off + len ? 3 : 2);
          outbox.unshift(f);
          off += len;
        }
        continue;
      }
      if (outbox.size() == 1 || !isWindowed) {
        single = true;
        py += tF;
        important |= outbox[0].important;
        consumed = 1;
      } else {
        for (usz i = 0; i < outbox.size(); ++i) {
          Xi::String t;
          serializePacket(t, outbox[i]);
          if (py.size() + t.size() + 9 > avail)
            break;
          py.pushVarLong((long long)t.size());
          py += t;
          important |= outbox[i].important;
          consumed++;
        }
      }
      for (usz k = 0; k < consumed; ++k)
        outbox.shift();
      Xi::String fP;
      fP.push(0);
      bool pad = false;
      usz dL = py.length() - 1;
      Xi::String lV;
      lV.pushVarLong((long long)dL);
      usz cT = 1 + lV.length() + dL, rem = cT % bBS;
      if (rem != 0) {
        pad = true;
        fP += lV;
        fP.pushEach(py.data() + 1, dL);
        fP += Xi::zeros(bBS - rem);
      } else
        fP.pushEach(py.data() + 1, dL);
      u8 h = 0;
      if (isSecure)
        h |= 1;
      if (pad)
        h |= (1 << 2);
      if (single)
        h |= (1 << 3);
      fP[0] = h; // Fixed invalid cast

      Xi::String bD;
      u64 cBID = isWindowed ? ++lastSentNonce : 0;
      if (isWindowed)
        bD.pushVarLong((long long)cBID);
      if (isSecure) {
        Xi::String aad;
        if (isWindowed)
          aad.pushVarLong((long long)cBID);
        Xi::AEADOptions opt;
        opt.text = fP;
        opt.ad = aad;
        opt.tagLength = 8;
        if (Xi::aeadSeal(key, cBID, opt)) {
          bD += opt.tag;
          bD += opt.text;
        }
      } else {
        bD.pushEach(fP.data(), fP.length());
      }

      InflightBundle ib;
      ib.id = cBID;
      ib.data = Xi::Move(bD);
      ib.important = isWindowed ? important : false;
      if (ib.important)
        inflightBundles.push(Xi::Move(ib));
      else
        nonImportantInflightBundles.push(Xi::Move(ib));
    }
  }

  // Common methods
  bool hasReceived(u64 id) const {
    if (id == 0)
      return true;
    if (id > lastReceivedNonce)
      return false;
    u64 diff = lastReceivedNonce - id;
    if (diff >= 64)
      return true;
    return (receiveWindowMask >> diff) & 1;
  }
  void pretendReceived(u64 id) {
    if (id == 0)
      return;
    if (id > lastReceivedNonce) {
      u64 diff = id - lastReceivedNonce;
      if (diff >= 64)
        receiveWindowMask = 1;
      else {
        receiveWindowMask <<= diff;
        receiveWindowMask |= 1;
      }
      lastReceivedNonce = id;
    } else {
      u64 diff = lastReceivedNonce - id;
      if (diff < 64)
        receiveWindowMask |= ((u64)1 << diff);
    }
  }
  void removeInflight(u64 id) {
    for (usz i = 0; i < inflightBundles.size(); ++i)
      if (inflightBundles[i].id == id) {
        inflightBundles.splice(i, 1);
        if (resendPosition > i)
          resendPosition--;
        return;
      }
  }
  void resendFrom(u64 x) {
    resendPosition = 0;
    for (usz i = 0; i < inflightBundles.size(); ++i)
      if (inflightBundles[i].id >= x) {
        resendPosition = i;
        break;
      }
  }
  Xi::Array<FromTo> showReceived() const {
    Xi::Array<FromTo> res;
    if (lastReceivedNonce == 0)
      return res;
    FromTo cur;
    cur.to = lastReceivedNonce;
    cur.from = lastReceivedNonce;
    bool inRange = true;
    u64 mask = receiveWindowMask;
    for (int k = 1; k < 64; ++k) {
      u64 id = lastReceivedNonce - k;
      if (id == 0)
        break;
      bool have = (mask >> k) & 1;
      if (have) {
        if (inRange)
          cur.from = id;
        else {
          inRange = true;
          cur.to = id;
          cur.from = id;
        }
      } else if (inRange) {
        res.push(cur);
        inRange = false;
      }
    }
    if (inRange)
      res.push(cur);
    return res;
  }
  Xi::Array<FromTo> showUnavailable() {
    Xi::Array<FromTo> res;
    for (usz i = 0; i < droppedBundles.size(); ++i) {
      FromTo ft;
      ft.from = droppedBundles[i];
      ft.to = droppedBundles[i];
      res.push(ft);
    }
    droppedBundles.clear();
    return res;
  }
  void serializePacket(Xi::String &b, const Packet &p) {
    u8 h = (p.fragmentStatus & 0x03);
    if (p.channel != 1)
      h |= (1 << 2);
    if (p.bypassHOL)
      h |= (1 << 3);
    b.push(h);
    if (isWindowed)
      b.pushVarLong((long long)p.id);
    if (p.channel != 1)
      b.pushVarLong((long long)p.channel);
    if (p.fragmentStatus != 0)
      b.pushVarLong((long long)p.fragmentStartID);
    b += p.payload;
  }

  bool readyToSend() const {
    if (isAsleep)
      return false;
    u64 now = Xi::millis();
    u64 hI = aliveTimeout > 0 ? (u64)(aliveTimeout / 2.5) : 0;
    // Fix: Heartbeat only applicable if windowed (connection established)
    bool hb =
        (isWindowed && aliveTimeout > 0) &&
        ((now > lastSent + aliveTimeout) || (now > lastSentHeartbeat + hI));
    return nonImportantInflightBundles.size() > 0 ||
           priorityResendQueue.size() > 0 ||
           (resendPosition < inflightBundles.size()) || outbox.size() > 0 || hb;
  }

  Xi::String flush(usz bBS = 32, usz bMS = 1400) {

    if (isAsleep)
      return Xi::String();
    u64 now = Xi::millis();
    if (destroyAfterFlush && inflightBundles.size() == 0 &&
        nonImportantInflightBundles.size() == 0 && outbox.size() == 0) {
      if (destroyListener.isValid()) {
        destroyListener();
        return Xi::String();
      }
    }
    if (aliveTimeout > 0 && isWindowed) {
      u64 hI = (u64)(aliveTimeout / 2.5);
      if ((now > lastSent + aliveTimeout) || (now > lastSentHeartbeat + hI)) {
        Packet h;
        h.channel = 0;
        h.important = false;
        h.payload.pushVarLong(0);
        auto rec = showReceived();
        h.payload.pushVarLong((long long)rec.size());
        for (auto &f : rec) {
          h.payload.pushVarLong((long long)f.from);
          h.payload.pushVarLong((long long)f.to);
        }
        auto un = showUnavailable();
        h.payload.pushVarLong((long long)un.size());
        for (auto &f : un) {
          h.payload.pushVarLong((long long)f.from);
          h.payload.pushVarLong((long long)f.to);
        }
        outbox.unshift(h);
        lastSentHeartbeat = now;
      }
    }
    if (outbox.size() > 0)
      build(bBS, bMS);

    Xi::String ret;
    if (nonImportantInflightBundles.size() > 0) {
      InflightBundle ib = nonImportantInflightBundles.shift();
      ret = Xi::Move(ib.data);
    } else if (priorityResendQueue.size() > 0) {
      InflightBundle ib = priorityResendQueue.shift();
      ret = Xi::Move(ib.data);
    } else if (resendPosition < inflightBundles.size()) {
      InflightBundle &ib = inflightBundles[resendPosition++];
      ret = Xi::String(ib.data.data(), ib.data.length());
    }

    if (ret.length() > 0) {
      lastSent = Xi::millis();
      if (!isWindowed) {
        inflightBundles.clear();
        resendPosition = 0;
      }
    }

    if (aliveTimeout > 0 && (Xi::millis() - lastSeen > aliveTimeout))
      isAsleep = true;
    if (secureAfterFlush) {
      isSecure = true;
      secureAfterFlush = false;
    }
    if (windowAfterFlush) {
      isWindowed = true;
      windowAfterFlush = false;
    }
    if (secureXAfterFlush) {
      enableSecurityX();
      secureXAfterFlush = false;
    }

    return ret;
  }
};
} // namespace Xi
#endif
