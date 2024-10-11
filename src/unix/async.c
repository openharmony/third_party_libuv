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

/* This file contains both the uv__async internal infrastructure and the
 * user-facing uv_async_t functions.
 */

#include "uv.h"
#include "internal.h"
#include "uv_log.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>  /* snprintf() */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>  /* sched_yield() */

#ifdef __linux__
#include <sys/eventfd.h>
#endif

#ifdef USE_FFRT
#include "ffrt.h"
#include "c/executor_task.h"
#endif

static void uv__async_send(uv_async_t* handle);
static int uv__async_start(uv_loop_t* loop);


int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  int err;

  err = uv__async_start(loop);
  if (err)
    return err;

  uv__handle_init(loop, (uv_handle_t*)handle, UV_ASYNC);
  handle->async_cb = async_cb;
  handle->pending = 0;

  uv__queue_insert_tail(&loop->async_handles, &handle->queue);
  uv__handle_start(handle);

  return 0;
}


int uv_async_send(uv_async_t* handle) {
  _Atomic int* pending;

#ifdef USE_OHOS_DFX
  if (handle == NULL) {
    UV_LOGF("handle is NULL");
    return -1;
  }
#endif

  pending = (_Atomic int*) &handle->pending;
  

  /* Do a cheap read first. */
  if (atomic_load_explicit(pending, memory_order_relaxed) != 0)
    return 0;

  /* Wake up the other thread's event loop. */
  if (atomic_exchange(pending, 1) != 0)
    return 0;

  /* Wake up the other thread's event loop. */
  uv__async_send(handle);
  return 0;
}



void uv__async_close(uv_async_t* handle) {
  atomic_exchange((_Atomic int*) &handle->pending, 0);
  uv__queue_remove(&handle->queue);
  uv__handle_stop(handle);
}


static void uv__async_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  char buf[1024];
  ssize_t r;
  struct uv__queue queue;
  struct uv__queue* q;
  uv_async_t* h;
  _Atomic int *pending;

  assert(w == &loop->async_io_watcher);

  for (;;) {
    r = read(w->fd, buf, sizeof(buf));

    if (r == sizeof(buf))
      continue;

    if (r != -1)
      break;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;

    if (errno == EINTR)
      continue;

#ifdef PRINT_ERRNO_ABORT
    UV_ERRNO_ABORT("errno is %d, loop addr is %zu, fd is %d (%s:%s:%d)",
      errno, (size_t)loop, w->fd, __FILE__, __func__, __LINE__);
#else
    abort();
#endif
  }

  uv__queue_move(&loop->async_handles, &queue);
  while (!uv__queue_empty(&queue)) {
    q = uv__queue_head(&queue);
    h = uv__queue_data(q, uv_async_t, queue);

    uv__queue_remove(q);
    uv__queue_insert_tail(&loop->async_handles, q);

    /* Atomically fetch and clear pending flag */
    pending = (_Atomic int*) &h->pending;
    if (atomic_exchange(pending, 0) == 0)
      continue;

    if (h->async_cb == NULL)
      continue;

    h->async_cb(h);
  }
}


static void uv__async_send(uv_async_t* handle) {
  const void* buf;
  ssize_t len;
  int fd;
  int r;

  uv_loop_t* loop = handle->loop;
  if (loop == NULL) {
    UV_LOGF("loop is NULL");
    return;
  }

  buf = "";
  len = 1;
  fd = loop->async_wfd;

#if defined(__linux__)
  if (fd == -1) {
    static const uint64_t val = 1;
    buf = &val;
    len = sizeof(val);
    fd = loop->async_io_watcher.fd;  /* eventfd */
  }
#endif

  do
    r = write(fd, buf, len);
  while (r == -1 && errno == EINTR && atomic_load_explicit((_Atomic int*) &handle->pending, memory_order_relaxed) == 1);

  if (r == len)
    return;

  if (r == -1)
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return;

#ifdef PRINT_ERRNO_ABORT
    UV_ERRNO_ABORT("errno is %d, loop addr is %zu, fd is %d (%s:%s:%d)",
      errno, (size_t)loop, fd, __FILE__, __func__, __LINE__);
#else
    abort();
#endif
}


static int uv__async_start(uv_loop_t* loop) {
  int pipefd[2];
  int err;

  if (loop->async_io_watcher.fd != -1)
    return 0;

#ifdef __linux__
  err = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (err < 0)
    return UV__ERR(errno);

  pipefd[0] = err;
  pipefd[1] = -1;
#ifdef USE_OHOS_DFX
  fdsan_exchange_owner_tag(pipefd[0], 0, uv__get_addr_tag((void *)&loop->async_io_watcher));
#endif
#else
  err = uv__make_pipe(pipefd, UV_NONBLOCK_PIPE);
  if (err < 0)
    return err;
#endif

  uv__io_init(&loop->async_io_watcher, uv__async_io, pipefd[0]);
  uv__io_start(loop, &loop->async_io_watcher, POLLIN);
  loop->async_wfd = pipefd[1];
  UV_LOGI("open:%{public}zu, pipefd[0]:%{public}d", (size_t)loop, pipefd[0]);
  return 0;
}


int uv__async_fork(uv_loop_t* loop) {
  if (loop->async_io_watcher.fd == -1) /* never started */
    return 0;

  uv__async_stop(loop);

  return uv__async_start(loop);
}


void uv__async_stop(uv_loop_t* loop) {
  if (loop->async_io_watcher.fd == -1)
    return;

  if (loop->async_wfd != -1) {
    if (loop->async_wfd != loop->async_io_watcher.fd) {
      UV_LOGI("close:%{public}zu, async_wfd:%{public}d", (size_t)loop, loop->async_wfd);
      uv__close(loop->async_wfd);
    }
    loop->async_wfd = -1;
  }

  uv__io_stop(loop, &loop->async_io_watcher, POLLIN);
#ifdef USE_FFRT
  if (ffrt_get_cur_task() != NULL) {
    uv__epoll_ctl(loop->backend_fd, EPOLL_CTL_DEL, loop->async_io_watcher.fd, NULL);
  }
#endif

#if defined(__linux__) && defined(USE_OHOS_DFX)
  fdsan_close_with_tag(loop->async_io_watcher.fd, uv__get_addr_tag((void *)&loop->async_io_watcher));
#else
  uv__close(loop->async_io_watcher.fd);
#endif
  UV_LOGI("close:%{public}zu, async_io_wfd:%{public}d", (size_t)loop, loop->async_io_watcher.fd);
  loop->async_io_watcher.fd = -1;
}

