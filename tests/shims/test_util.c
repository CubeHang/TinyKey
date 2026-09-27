/**
  ******************************************************************************
  * @file    test_util.c
  * @brief   极简测试断言框架实现
  ******************************************************************************
  */
#include "test_util.h"

int g_checks = 0;
int g_failures = 0;

int test_summary(const char *suite_name)
{
  printf("\n== %s: %d 项检查，%d 项失败 ==\n", suite_name, g_checks, g_failures);

  if (g_failures == 0)
  {
    printf("结果: PASS\n");
    return 0;
  }

  printf("结果: FAIL\n");
  return 1;
}
