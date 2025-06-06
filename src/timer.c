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

#include "uv.h"
#include "uv-common.h"
#include "heap-inl.h"

#include <assert.h>
#include <limits.h>

#ifdef ASYNC_STACKTRACE
#include "dfx/async_stack/libuv_async_stack.h"
#endif

static struct heap *timer_heap(const uv_loop_t* loop) {
#ifdef _WIN32
  return (struct heap*) loop->timer_heap;
#else
  return (struct heap*) &loop->timer_heap;
#endif
}


static int timer_less_than(const struct heap_node* ha,
                           const struct heap_node* hb) {
  const uv_timer_t* a;
  const uv_timer_t* b;

  a = container_of(ha, uv_timer_t, heap_node);
  b = container_of(hb, uv_timer_t, heap_node);

  if (a->timeout < b->timeout)
    return 1;
  if (b->timeout < a->timeout)
    return 0;

  /* Compare start_id when both have the same timeout. start_id is
   * allocated with loop->timer_counter in uv_timer_start().
   */
  return a->start_id < b->start_id;
}


int uv_timer_init(uv_loop_t* loop, uv_timer_t* handle) {
#if defined(USE_OHOS_DFX) && defined(__aarch64__)
  uv__multi_thread_check_unify(loop, __func__);
#endif
  uv__handle_init(loop, (uv_handle_t*)handle, UV_TIMER);
  handle->timer_cb = NULL;
  handle->timeout = 0;
  handle->repeat = 0;
  return 0;
}


int uv_timer_start(uv_timer_t* handle,
                   uv_timer_cb cb,
                   uint64_t timeout,
                   uint64_t repeat) {
  uint64_t clamped_timeout;
#if defined(USE_OHOS_DFX) && defined(__aarch64__)
  uv__multi_thread_check_unify(handle->loop, __func__);
#endif

  if (uv__is_closing(handle) || cb == NULL)
    return UV_EINVAL;

  if (uv__is_active(handle))
    uv_timer_stop(handle);

  clamped_timeout = handle->loop->time + timeout;
  if (clamped_timeout < timeout)
    clamped_timeout = (uint64_t) -1;

  handle->timer_cb = cb;
  handle->timeout = clamped_timeout;
  handle->repeat = repeat;
  /* start_id is the second index to be compared in timer_less_than() */
  handle->start_id = handle->loop->timer_counter++;

#ifdef ASYNC_STACKTRACE
  handle->u.reserved[3] = (void*)LibuvCollectAsyncStack();
#endif

  heap_insert(timer_heap(handle->loop),
              (struct heap_node*) &handle->heap_node,
              timer_less_than);
  uv__handle_start(handle);
#ifdef __linux__
  if (uv_check_data_valid(handle->loop) == 0) {
    uv_async_send(&handle->loop->wq_async);
  }
#endif
  return 0;
}


int uv_timer_stop(uv_timer_t* handle) {
#if defined(USE_OHOS_DFX) && defined(__aarch64__)
  uv__multi_thread_check_unify(handle->loop, __func__);
#endif
  if (!uv__is_active(handle))
    return 0;

  heap_remove(timer_heap(handle->loop),
              (struct heap_node*) &handle->heap_node,
              timer_less_than);
  uv__handle_stop(handle);

  return 0;
}


int uv_timer_again(uv_timer_t* handle) {
  if (handle->timer_cb == NULL)
    return UV_EINVAL;

  if (handle->repeat) {
    uv_timer_stop(handle);
    uv_timer_start(handle, handle->timer_cb, handle->repeat, handle->repeat);
  }

  return 0;
}


void uv_timer_set_repeat(uv_timer_t* handle, uint64_t repeat) {
  handle->repeat = repeat;
}


uint64_t uv_timer_get_repeat(const uv_timer_t* handle) {
  return handle->repeat;
}


uint64_t uv_timer_get_due_in(const uv_timer_t* handle) {
  if (handle->loop->time >= handle->timeout)
    return 0;

  return handle->timeout - handle->loop->time;
}


int uv__next_timeout(const uv_loop_t* loop) {
  const struct heap_node* heap_node;
  const uv_timer_t* handle;
  uint64_t diff;

  heap_node = heap_min(timer_heap(loop));
  if (heap_node == NULL)
    return -1; /* block indefinitely */

  handle = container_of(heap_node, uv_timer_t, heap_node);
  if (handle->timeout <= loop->time)
    return 0;

  diff = handle->timeout - loop->time;
  if (diff > INT_MAX)
    diff = INT_MAX;

  return (int) diff;
}


void uv__run_timers(uv_loop_t* loop) {
  struct heap_node* heap_node;
  uv_timer_t* handle;

  for (;;) {
    heap_node = heap_min(timer_heap(loop));
    if (heap_node == NULL)
      break;

    handle = container_of(heap_node, uv_timer_t, heap_node);
    if (handle->timeout > loop->time)
      break;

    uv_timer_stop(handle);
    uv_timer_again(handle);
#ifdef ASYNC_STACKTRACE
    LibuvSetStackId((uint64_t)handle->u.reserved[3]);
#endif
    handle->timer_cb(handle);
  }
}


void uv__timer_close(uv_timer_t* handle) {
  uv_timer_stop(handle);
}
