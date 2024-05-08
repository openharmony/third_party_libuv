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

typedef void(*LibuvSetStackIdFunc)(uint64_t stackId);
typedef uint64_t(*LibuvCollectAsyncStackFunc)();
static LibuvCollectAsyncStackFunc g_collectAsyncStackFunc = NULL;
static LibuvSetStackIdFunc g_setStackIdFunc = NULL;
typedef enum {
    ASYNC_DFX_NOT_INIT,
    ASYNC_DFX_DISABLE,
    ASYNC_DFX_ENABLE
} AsyncDfxInitStatus;

static AsyncDfxInitStatus g_enabledLibuvAsyncStackStatus = ASYNC_DFX_NOT_INIT;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static void LoadDfxAsyncStackLib()
{
    g_enabledLibuvAsyncStackStatus = ASYNC_DFX_DISABLE;
    const char* debuggableEnv = getenv("HAP_DEBUGGABLE");
    if ((debuggableEnv == NULL) || (strcmp(debuggableEnv, "true") != 0)) {
        return;
    }

    // if async stack is not enabled, the lib should not be unloaded
    void* asyncStackLibHandle = dlopen("libasync_stack.z.so", RTLD_NOW);
    if (asyncStackLibHandle == NULL) {
        return;
    }

    g_collectAsyncStackFunc = (LibuvCollectAsyncStackFunc)(dlsym(asyncStackLibHandle, "CollectAsyncStack"));
    if (g_collectAsyncStackFunc == NULL) {
        dlclose(asyncStackLibHandle);
        asyncStackLibHandle = NULL;
        return;
    }

    g_setStackIdFunc = (LibuvSetStackIdFunc)(dlsym(asyncStackLibHandle, "SetStackId"));
    if (g_setStackIdFunc == NULL) {
        g_collectAsyncStackFunc = NULL;
        dlclose(asyncStackLibHandle);
        asyncStackLibHandle = NULL;
        return;
    }

    g_enabledLibuvAsyncStackStatus = ASYNC_DFX_ENABLE;
}

static AsyncDfxInitStatus LibuvAsyncStackInit()
{
    if (g_enabledLibuvAsyncStackStatus == ASYNC_DFX_NOT_INIT) {
        pthread_mutex_lock(&g_mutex);
        if (g_enabledLibuvAsyncStackStatus == ASYNC_DFX_NOT_INIT) {
            LoadDfxAsyncStackLib();
        }
        pthread_mutex_unlock(&g_mutex);
    }
    return g_enabledLibuvAsyncStackStatus;
}

uint64_t LibuvCollectAsyncStack(void)
{
    if (LibuvAsyncStackInit() == ASYNC_DFX_ENABLE) {
        return g_collectAsyncStackFunc();
    }

    return 0;
}

void LibuvSetStackId(uint64_t stackId)
{
    if (LibuvAsyncStackInit() == ASYNC_DFX_ENABLE) {
        return g_setStackIdFunc(stackId);
    }
}
