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

#ifndef UV_LOG_H
#define UV_LOG_H

#ifdef USE_OHOS_DFX
#include "hilog/log.h"
#define UV_LOGI(fmt, ...) HILOG_IMPL(LOG_CORE, LOG_INFO, 0xD00394F, "UV", fmt, ##__VA_ARGS__)
#define UV_LOGD(fmt, ...) HILOG_IMPL(LOG_CORE, LOG_DEBUG, 0xD00394F, "UV", fmt, ##__VA_ARGS__)
#define UV_LOGW(fmt, ...) HILOG_IMPL(LOG_CORE, LOG_WARN, 0xD00394F, "UV", fmt, ##__VA_ARGS__)
#define UV_LOGE(fmt, ...) HILOG_IMPL(LOG_CORE, LOG_ERROR, 0xD00394F, "UV", fmt, ##__VA_ARGS__)
#define UV_LOGF(fmt, ...) HILOG_IMPL(LOG_CORE, LOG_FATAL, 0xD00394F, "UV", fmt, ##__VA_ARGS__)
#else
enum uv__log_level {
  UV_MIN = 0,
  UV_DEBUG = 3,
  UV_INFO,
  UV_WARN,
  UV_ERROR,
  UV_FATAL,
  UV_MAX,
};

extern int uv__log_impl(enum uv__log_level level, const char* fmt, ...);

#define UV_LOGI(...) LOGI_IMPL(UV_INFO, ##__VA_ARGS__)
#define UV_LOGD(...) LOGI_IMPL(UV_DEBUG, ##__VA_ARGS__)
#define UV_LOGW(...) LOGI_IMPL(UV_WARN, ##__VA_ARGS__)
#define UV_LOGE(...) LOGI_IMPL(UV_ERROR, ##__VA_ARGS__)
#define UV_LOGF(...) LOGI_IMPL(UV_FATAL, ##__VA_ARGS__)

#define LOGI_IMPL(level, ...) uv__log_impl(level, ##__VA_ARGS__)
#endif

#endif
