/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
  int ret = HiLogPrintArgs(LOG_CORE, convert_uv_log_level(level), 0xD003301, "LIBUV", fmt, args);
  va_end(args);
  return ret;
}