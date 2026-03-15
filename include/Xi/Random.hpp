#ifndef XI_RANDOM
#define XI_RANDOM

#include "Primitives.hpp"

namespace Xi {

extern u32 _randomPool[20];
extern bool _randomInitialized;
extern u32 _secureCounter;

u32 randomNext();
void randomSeed(u32 s);
void randomSeed();
u32 random(u32 max);
i32 random(i32 min, i32 max);
f32 randomFloat();
void randomFill(u8 *buffer, usz size);

} // namespace Xi

#endif