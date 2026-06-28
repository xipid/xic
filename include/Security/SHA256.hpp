#ifndef XI_SECURITY_SHA256_HPP
#define XI_SECURITY_SHA256_HPP

#include "../Collection/String.hpp"

namespace Security {

using namespace Xi;
using namespace Collection;

class XI_EXPORT SHA256 {
public:
    static constexpr usz BlockSize = 64;
    static constexpr usz DigestSize = 32;

    SHA256();
    
    void update(const void* data, usz len);
    void update(const String& str);
    void final(u8 digest[DigestSize]);
    String final();

    static String hash(const String& input);
    static void hash(const void* data, usz len, u8 digest[DigestSize]);

private:
    u32 state[8];
    u64 count;
    u8 buffer[BlockSize];

    void transform(const u8 block[BlockSize]);

    static const u32 K[64];
};

} // namespace Security

#endif // XI_SECURITY_SHA256_HPP
