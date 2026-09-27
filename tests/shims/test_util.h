/**
  ******************************************************************************
  * @file    test_util.h
  * @brief   极简测试断言框架
  ******************************************************************************
  */
#ifndef __TEST_UTIL_H__
#define __TEST_UTIL_H__

#include <stdio.h>

extern int g_checks;
extern int g_failures;

#define TEST_CASE(name)   do { printf("-- %s\n", (name)); } while (0)

#define CHECK(cond, ...)                                                  \
  do {                                                                    \
    g_checks++;                                                           \
    if (!(cond)) {                                                        \
      g_failures++;                                                       \
      printf("   [FAIL] %s:%d  ", __FILE__, __LINE__);                    \
      printf(__VA_ARGS__);                                                \
      printf("\n");                                                       \
    }                                                                     \
  } while (0)

#define CHECK_U8(actual, expected, what)                                  \
  CHECK((uint8_t)(actual) == (uint8_t)(expected),                         \
        "%s: 期望 0x%02X，实际 0x%02X", (what),                           \
        (unsigned)(uint8_t)(expected), (unsigned)(uint8_t)(actual))

#define CHECK_INT(actual, expected, what)                                 \
  CHECK((long)(actual) == (long)(expected),                               \
        "%s: 期望 %ld，实际 %ld", (what), (long)(expected), (long)(actual))

/** 打印收尾统计，返回进程退出码 */
int test_summary(const char *suite_name);

#endif /* __TEST_UTIL_H__ */
