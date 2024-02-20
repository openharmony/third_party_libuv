/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv_log.h"
#include "hilog/log.h"

#include <stdarg.h>

extern int HiLogPrintArgs(const LogType type, const LogLevel level, const unsigned int domain, const char *tag,
    const char *fmt, va_list ap);

LogLevel convert_uv_log_level(enum uv__log_level level) {
  switch (level)
  {
    case UV_DEBUG:
      return LOG_DEBUG;
    case UV_INFO:
      return LOG_INFO;
    case UV_WARN:
      return LOG_WARN;
    case UV_ERROR:
      return LOG_ERROR;
    case UV_FATAL:
      return LOG_FATAL;
    default:
      return LOG_LEVEL_MIN;
  }
}

int uv__log_impl(enum uv__log_level level, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int ret = HiLogPrintArgs(LOG_TYPE_MIN, convert_uv_log_level(level), 0xD003900, "UV", fmt, args);
  va_end(args);
  return ret;
}
