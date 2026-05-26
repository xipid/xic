/**
 * @file Random.hpp
 * @brief Pseudo-random number generation utilities for the Xi framework.

 */

#ifndef XI_CORE_RANDOM_HPP
#define XI_CORE_RANDOM_HPP

#include "../Collection/String.hpp"
#include "Primitives.hpp"

using namespace Collection;

namespace Xi {

/** @brief Internal random pool state. */
extern u32 _randomPool[20];
/** @brief Flag indicating if the random generator has been initialized. */
extern bool _randomInitialized;
/** @brief Counter for secure random operations. */
extern u32 _secureCounter;

/**
 * @brief Generates the next pseudo-random 32-bit unsigned integer.
 */
u32 randomNext();

/**
 * @brief Seeds the random number generator with a specific value.
 */
void randomSeed(u32 s, bool overwrite = false);

/**
 * @brief Seeds the random number generator using system entropy.
 */
void randomSeed(bool overwrite = false);

/**
 * @brief Generates a random integer in the range [0, max).
 */
u32 random(u32 max);

/**
 * @brief Generates a random integer in the range [min, max].
 */
i32 random(i32 min, i32 max);

/**
 * @brief Generates a random float in the range [0.0, 1.0].
 */
f32 randomFloat();

/**
 * @brief Fills a buffer with random bytes.
 */
void randomFill(u8 *buffer, usz size);

/**
 * @brief Fills a string with pseudo-random bytes.
 */
void randomFill(String &s, usz len = 0);

/**
 * @brief Fills a string with cryptographically secure random bytes.
 */
void secureRandomFill(String &s, usz len = 0);

/**
 * @brief Utility for seeding the PRNG using string hash.
 */
void randomSeed(String str);

} // namespace Xi

#endif // XI_CORE_RANDOM_HPP