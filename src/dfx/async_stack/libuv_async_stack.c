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
#include "libuv_async_stack.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
static UvCollectAsyncStackFunc g_collectAsyncStackFunc = NULL;
static UvSetStackIdFunc g_setStackIdFunc = NULL;

void LibuvSetAsyncStackFunc(UvCollectAsyncStackFunc collectAsyncStackFunc, UvSetStackIdFunc setStackIdFunc)
{
    g_collectAsyncStackFunc = collectAsyncStackFunc;
    g_setStackIdFunc = setStackIdFunc;
}

uint64_t LibuvCollectAsyncStack(void)
{
    if (g_collectAsyncStackFunc != NULL) {
        return g_collectAsyncStackFunc();
    }

    return 0;
}

void LibuvSetStackId(uint64_t stackId)
{
    if (g_collectAsyncStackFunc != NULL) {
        return g_setStackIdFunc(stackId);
    }
}
