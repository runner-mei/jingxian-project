
#ifndef _UnitTestting_H_
#define _UnitTestting_H_

#include "jingxian/config.h"

#if !defined (JINGXIAN_LACKS_PRAGMA_ONCE)
# pragma once
#endif /* JINGXIAN_LACKS_PRAGMA_ONCE */


// Include files
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h> 
#include <assert.h>
#include <errno.h> 


extern int Test_Flags_Verbose;

#define WRITE_TO_STDERR(buf, len) fwrite(buf, 1, len, stderr)

#define CHECK(condition)                                                \
  do {                                                                  \
    if (!(condition)) {                                                 \
      WRITE_TO_STDERR("Check failed: " #condition "\n",                 \
                      sizeof("Check failed: " #condition "\n")-1);      \
	  abort();                                                          \
      exit(1);                                                          \
    }                                                                   \
  } while (0)


#define RAW_CHECK(condition, message)                                          \
  do {                                                                         \
    if (!(condition)) {                                                        \
      WRITE_TO_STDERR("Check failed: " #condition ": " message "\n",           \
                      sizeof("Check failed: " #condition ": " message "\n")-1);\
	  abort();                                                                 \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)


#define PCHECK(condition)                                               \
  do {                                                                  \
    if (!(condition)) {                                                 \
      const int err_no = errno;                                         \
      WRITE_TO_STDERR("Check failed: " #condition ": ",                 \
                      sizeof("Check failed: " #condition ": ")-1);      \
      WRITE_TO_STDERR(strerror(err_no), strlen(strerror(err_no)));      \
      WRITE_TO_STDERR("\n", sizeof("\n")-1);                            \
	  abort();                                                          \
      exit(1);                                                          \
    }                                                                   \
  } while (0)

#define CHECK_OP(op, val1, val2)                                        \
  do {                                                                  \
    if (!((val1) op (val2))) {                                          \
      fprintf(stderr, "Check failed: %s %s %s\n", #val1, #op, #val2);   \
	  abort();                                                          \
      exit(1);                                                          \
    }                                                                   \
  } while (0)

#define CHECK_EQ(val1, val2) CHECK_OP(==, val1, val2)
#define CHECK_NE(val1, val2) CHECK_OP(!=, val1, val2)
#define CHECK_LE(val1, val2) CHECK_OP(<=, val1, val2)
#define CHECK_LT(val1, val2) CHECK_OP(< , val1, val2)
#define CHECK_GE(val1, val2) CHECK_OP(>=, val1, val2)
#define CHECK_GT(val1, val2) CHECK_OP(> , val1, val2)

#define EXPECT_EQ(val1, val2) CHECK_EQ(val1, val2)
#define EXPECT_NE(val1, val2) CHECK_NE(val1, val2)
#define EXPECT_LE(val1, val2) CHECK_LE(val1, val2)
#define EXPECT_LT(val1, val2) CHECK_LT(val1, val2)
#define EXPECT_GE(val1, val2) CHECK_GE(val1, val2)
#define EXPECT_GT(val1, val2) CHECK_GT(val1, val2)


#define ASSERT_TRUE(cond)      CHECK(cond)
#define ASSERT_FALSE(cond)     CHECK(!(cond))

#define EXPECT_TRUE(cond)     CHECK(cond)
#define EXPECT_FALSE(cond)    CHECK(!(cond))
#define EXPECT_STREQ(a, b)    CHECK(strcmp(a, b) == 0)

#define CHECK_ERR(invocation)  PCHECK((invocation) != -1)

#ifdef NDEBUG
#define DCHECK_EQ(val1, val2)
#define DCHECK_NE(val1, val2)
#define DCHECK_LE(val1, val2)
#define DCHECK_LT(val1, val2)
#define DCHECK_GE(val1, val2)
#define DCHECK_GT(val1, val2)
#else
#define DCHECK_EQ(val1, val2)  CHECK_EQ(val1, val2)
#define DCHECK_NE(val1, val2)  CHECK_NE(val1, val2)
#define DCHECK_LE(val1, val2)  CHECK_LE(val1, val2)
#define DCHECK_LT(val1, val2)  CHECK_LT(val1, val2)
#define DCHECK_GE(val1, val2)  CHECK_GE(val1, val2)
#define DCHECK_GT(val1, val2)  CHECK_GT(val1, val2)
#endif


#ifdef ERROR
#undef ERROR      // may conflict with ERROR macro on windows
#endif

enum LogSeverity {INFO = -1, WARNING = -2, ERROR = -3, FATAL = -4};

inline void LogPrintf(int severity, const char* pat, va_list ap)
{
  char buf[600];
  vsnprintf_s(buf, sizeof(char), sizeof(buf), pat, ap);
  if (buf[0] != '\0' && buf[strlen(buf)-1] != '\n') {
    assert(strlen(buf)+1 < sizeof(buf));
    strcat_s(buf,600, "\n");
  }
  WRITE_TO_STDERR(buf, strlen(buf));
  if ((severity) == FATAL)
    abort();
}

#define VLOG_IS_ON(severity) (Test_Flags_Verbose >= severity)


#define LOG_PRINTF(severity, pat) do {          \
  if (VLOG_IS_ON(severity)) {                   \
    va_list ap;                                 \
    va_start(ap, pat);                          \
    LogPrintf(severity, pat, ap);               \
    va_end(ap);                                 \
  }                                             \
} while (0)


inline void LOG_OUTPUT(int lvl, const char* pat, ...)      { LOG_PRINTF(lvl, pat); }
inline void LOG_OUTPUT_IF(int lvl, bool cond, const char* pat, ...)
{
  if (cond)
	  LOG_PRINTF(lvl, pat);
}

#include <windows.h>
typedef HANDLE RawFD;
const RawFD kIllegalRawFD = INVALID_HANDLE_VALUE;


RawFD RawOpenForWriting(const char* filename);   // uses default permissions
void RawWrite(RawFD fd, const char* buf, size_t len);
void RawClose(RawFD fd);


#define TEST(a, b)                                      \
  struct Test_##a##_##b {                               \
    Test_##a##_##b() { ADD_RUN_TEST(&Run); }			\
    static void Run();                                  \
  };                                                    \
  static Test_##a##_##b g_test_##a##_##b;               \
  void Test_##a##_##b::Run()

void ADD_RUN_TEST(void (*func)());
int RUN_ALL_TESTS();

#endif // _UnitTestting_H_