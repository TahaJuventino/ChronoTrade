#pragma once
#include "logger.h"

#define ASSERT_TRUE(expr) \
  do { \
    if (!(expr)) { \
      PANIC("ASSERT_TRUE failed: " #expr); \
    } \
  } while (0)

#define ASSERT_EQ(a, b) \
  do { \
    if ((a) != (b)) { \
      PANIC("ASSERT_EQ failed: " #a " != " #b); \
    } \
  } while (0)

#define ASSERT_NE(a, b) \
  do { \
    if ((a) == (b)) { \
      PANIC("ASSERT_NE failed: " #a " == " #b); \
    } \
  } while (0)
