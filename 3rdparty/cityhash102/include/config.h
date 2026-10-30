#pragma once
#if defined(__GNUC__) || defined(__clang__)
#define HAVE_BUILTIN_EXPECT 1
#else
#define HAVE_BUILTIN_EXPECT 0
#endif
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define WORDS_BIGENDIAN 1
#endif
