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

#ifndef UV_LOG_H
#define UV_LOG_H

#ifdef USE_OHOS_DFX
#include "hilog/log.h"
#define UV_LOGI(fmt, ...) HILOG_IMPL(LOG_CORE, LOG_INFO, 0xD003301, "LIBUV", fmt, ##__VA_ARGS__)
#define UV_LOGD(fmt, ...) HILOG_IMPL(LOG_CORE, LOG_DEBUG, 0xD003301, "LIBUV", fmt, ##__VA_ARGS__)
#define UV_LOGW(fmt, ...) HILOG_IMPL(LOG_CORE, LOG_WARN, 0xD003301, "LIBUV", fmt, ##__VA_ARGS__)
#define UV_LOGE(fmt, ...) HILOG_IMPL(LOG_CORE, LOG_ERROR, 0xD003301, "LIBUV", fmt, ##__VA_ARGS__)
#define UV_LOGF(fmt, ...) HILOG_IMPL(LOG_CORE, LOG_FATAL, 0xD003301, "LIBUV", fmt, ##__VA_ARGS__)
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
