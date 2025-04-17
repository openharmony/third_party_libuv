/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
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

#include "uv-common.h"
#include "uv_log.h"
#include "uv_trace.h"

#if !defined(_WIN32)
# include "unix/internal.h"
#else
# include "win/internal.h"
#endif

#include <stdlib.h>
#ifdef USE_FFRT
#include <assert.h>
#include "ffrt_inner.h"
#endif
#include <stdio.h>
#ifdef ASYNC_STACKTRACE
#include "dfx/async_stack/libuv_async_stack.h"
#endif

#define MAX_THREADPOOL_SIZE 1024
#define UV_TRACE_NAME "UV_TRACE"

#ifdef USE_OHOS_DFX
#define MIN_REQS_THRESHOLD 100
#define MAX_REQS_THRESHOLD 300
#define CURSOR 5
#endif

#ifdef USE_FFRT
static uv_rwlock_t g_closed_uv_loop_rwlock;
#endif
static uv_once_t once = UV_ONCE_INIT;
static uv_cond_t cond;
static uv_mutex_t mutex;
static unsigned int idle_threads;
static unsigned int nthreads;
static uv_thread_t* threads;
static uv_thread_t default_threads[4];
static struct uv__queue exit_message;
static struct uv__queue wq;
static struct uv__queue run_slow_work_message;
static struct uv__queue slow_io_pending_wq;


#ifdef USE_FFRT
static void init_closed_uv_loop_rwlock_once(void) {
  uv_rwlock_init(&g_closed_uv_loop_rwlock);
}


void rdlock_closed_uv_loop_rwlock(void) {
  uv_rwlock_rdlock(&g_closed_uv_loop_rwlock);
}


void rdunlock_closed_uv_loop_rwlock(void) {
  uv_rwlock_rdunlock(&g_closed_uv_loop_rwlock);
}


int is_uv_loop_good_magic(const uv_loop_t* loop) {
  if (loop->magic == UV_LOOP_MAGIC) {
    return 1;
  }
  UV_LOGE("loop:(%{public}zu:%{public}#x) invalid", (size_t)loop, loop->magic);
  return 0;
}
#endif


void on_uv_loop_close(uv_loop_t* loop) {
  time_t t1, t2;
  time(&t1);
#ifdef USE_FFRT
  uv_rwlock_wrlock(&g_closed_uv_loop_rwlock);
  loop->magic = ~UV_LOOP_MAGIC;
  uv_rwlock_wrunlock(&g_closed_uv_loop_rwlock);
#endif
  time(&t2);
  UV_LOGI("loop:(%{public}zu) closed in %{public}zds", (size_t)loop, (ssize_t)(t2 - t1));
}


#ifdef USE_FFRT
static void uv__cancelled(struct uv__work* w, int qos) {
#else
static void uv__cancelled(struct uv__work* w) {
#endif
  abort();
}


#ifndef USE_FFRT
static unsigned int slow_io_work_running;

static unsigned int slow_work_thread_threshold(void) {
  return (nthreads + 1) / 2;
}


/* To avoid deadlock with uv_cancel() it's crucial that the worker
 * never holds the global mutex and the loop-local mutex at the same time.
 */
static void worker(void* arg) {
  struct uv__work* w;
  struct uv__queue* q;
  int is_slow_work;

  uv_sem_post((uv_sem_t*) arg);
  arg = NULL;

  uv_mutex_lock(&mutex);
  for (;;) {
    /* `mutex` should always be locked at this point. */

    /* Keep waiting while either no work is present or only slow I/O
       and we're at the threshold for that. */
    while (uv__queue_empty(&wq) ||
           (uv__queue_head(&wq) == &run_slow_work_message &&
            uv__queue_next(&run_slow_work_message) == &wq &&
            slow_io_work_running >= slow_work_thread_threshold())) {
      idle_threads += 1;
      uv_cond_wait(&cond, &mutex);
      idle_threads -= 1;
    }

    q = uv__queue_head(&wq);
    if (q == &exit_message) {
      uv_cond_signal(&cond);
      uv_mutex_unlock(&mutex);
      break;
    }

    uv__queue_remove(q);
    uv__queue_init(q);  /* Signal uv_cancel() that the work req is executing. */

    is_slow_work = 0;
    if (q == &run_slow_work_message) {
      /* If we're at the slow I/O threshold, re-schedule until after all
         other work in the queue is done. */
      if (slow_io_work_running >= slow_work_thread_threshold()) {
        uv__queue_insert_tail(&wq, q);
        continue;
      }

      /* If we encountered a request to run slow I/O work but there is none
         to run, that means it's cancelled => Start over. */
      if (uv__queue_empty(&slow_io_pending_wq))
        continue;

      is_slow_work = 1;
      slow_io_work_running++;

      q = uv__queue_head(&slow_io_pending_wq);
      uv__queue_remove(q);
      uv__queue_init(q);

      /* If there is more slow I/O work, schedule it to be run as well. */
      if (!uv__queue_empty(&slow_io_pending_wq)) {
        uv__queue_insert_tail(&wq, &run_slow_work_message);
        if (idle_threads > 0)
          uv_cond_signal(&cond);
      }
    }

    uv_mutex_unlock(&mutex);

    w = uv__queue_data(q, struct uv__work, wq);
    w->work(w);
    uv_mutex_lock(&w->loop->wq_mutex);
    w->work = NULL;  /* Signal uv_cancel() that the work req is done
                        executing. */
    uv__queue_insert_tail(&w->loop->wq, &w->wq);
    uv_async_send(&w->loop->wq_async);
    uv_mutex_unlock(&w->loop->wq_mutex);

    /* Lock `mutex` since that is expected at the start of the next
     * iteration. */
    uv_mutex_lock(&mutex);
    if (is_slow_work) {
      /* `slow_io_work_running` is protected by `mutex`. */
      slow_io_work_running--;
    }
  }
}
#endif


static void post(struct uv__queue* q, enum uv__work_kind kind) {
  uv_mutex_lock(&mutex);
  if (kind == UV__WORK_SLOW_IO) {
    /* Insert into a separate queue. */
    uv__queue_insert_tail(&slow_io_pending_wq, q);
    if (!uv__queue_empty(&run_slow_work_message)) {
      /* Running slow I/O tasks is already scheduled => Nothing to do here.
         The worker that runs said other task will schedule this one as well. */
      uv_mutex_unlock(&mutex);
      return;
    }
    q = &run_slow_work_message;
  }

  uv__queue_insert_tail(&wq, q);
  if (idle_threads > 0)
    uv_cond_signal(&cond);
  uv_mutex_unlock(&mutex);
}


#ifdef __MVS__
/* TODO(itodorov) - zos: revisit when Woz compiler is available. */
__attribute__((destructor))
#endif
void uv__threadpool_cleanup(void) {
  unsigned int i;

  if (nthreads == 0)
    return;

#ifndef __MVS__
  /* TODO(gabylb) - zos: revisit when Woz compiler is available. */
  post(&exit_message, UV__WORK_CPU);
#endif

  for (i = 0; i < nthreads; i++)
    if (uv_thread_join(threads + i))
      abort();

  if (threads != default_threads)
    uv__free(threads);

  uv_mutex_destroy(&mutex);
  uv_cond_destroy(&cond);

  threads = NULL;
  nthreads = 0;
}


#ifndef USE_FFRT
static void init_threads(void) {
  uv_thread_options_t config;
  unsigned int i;
  const char* val;
  uv_sem_t sem;

  nthreads = ARRAY_SIZE(default_threads);
  val = getenv("UV_THREADPOOL_SIZE");
  if (val != NULL)
    nthreads = atoi(val);
  if (nthreads == 0)
    nthreads = 1;
  if (nthreads > MAX_THREADPOOL_SIZE)
    nthreads = MAX_THREADPOOL_SIZE;

  threads = default_threads;
  if (nthreads > ARRAY_SIZE(default_threads)) {
    threads = uv__malloc(nthreads * sizeof(threads[0]));
    if (threads == NULL) {
      nthreads = ARRAY_SIZE(default_threads);
      threads = default_threads;
    }
  }

  if (uv_cond_init(&cond))
    abort();

  if (uv_mutex_init(&mutex))
    abort();

  uv__queue_init(&wq);
  uv__queue_init(&slow_io_pending_wq);
  uv__queue_init(&run_slow_work_message);

  if (uv_sem_init(&sem, 0))
    abort();

  config.flags = UV_THREAD_HAS_STACK_SIZE;
  config.stack_size = 8u << 20;  /* 8 MB */

  for (i = 0; i < nthreads; i++)
    if (uv_thread_create_ex(threads + i, &config, worker, &sem))
      abort();

  for (i = 0; i < nthreads; i++)
    uv_sem_wait(&sem);

  uv_sem_destroy(&sem);
}


#ifndef _WIN32
static void reset_once(void) {
  uv_once_t child_once = UV_ONCE_INIT;
  memcpy(&once, &child_once, sizeof(child_once));
}
#endif


static void init_once(void) {
#ifndef _WIN32
  /* Re-initialize the threadpool after fork.
   * Note that this discards the global mutex and condition as well
   * as the work queue.
   */
  if (pthread_atfork(NULL, NULL, &reset_once))
    abort();
#endif
#ifdef USE_FFRT
  init_closed_uv_loop_rwlock_once();
#endif
  init_threads();
}


void uv__work_submit(uv_loop_t* loop,
                     struct uv__work* w,
                     enum uv__work_kind kind,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w, int status)) {
  uv_once(&once, init_once);
  w->loop = loop;
  w->work = work;
  w->done = done;
  post(&w->wq, kind);
}
#endif


static void uv__print_active_reqs(uv_loop_t* loop, const char* flag) {
#ifdef USE_OHOS_DFX
  unsigned int count = loop->active_reqs.count;
  if (count == MIN_REQS_THRESHOLD || count == MIN_REQS_THRESHOLD + CURSOR ||
      count == MAX_REQS_THRESHOLD || count == MAX_REQS_THRESHOLD + CURSOR) {
    UV_LOGW("loop:%{public}zu, flag:%{public}s, active reqs:%{public}u", (size_t)loop, flag, count);
  }
#else
  return;
#endif
}


#ifdef USE_FFRT
static void uv__task_done_wrapper(void* work, int status) {
  struct uv__work* w = (struct uv__work*)work;
  uv__print_active_reqs(w->loop, "complete");
  w->done(w, status);
}


void uv__work_submit_to_eventloop(uv_req_t* req, struct uv__work* w, int qos) {
  uv_loop_t* loop = w->loop;
  rdlock_closed_uv_loop_rwlock();
  if (!is_uv_loop_good_magic(loop)) {
    rdunlock_closed_uv_loop_rwlock();
    UV_LOGE("uv_loop(%{public}zu:%{public}#x), task is invalid",
            (size_t)loop, loop->magic);
    return;
  }

  uv_mutex_lock(&loop->wq_mutex);
  w->work = NULL; /* Signal uv_cancel() that the work req is done executing. */

  if (uv_check_data_valid((struct uv_loop_data*)(loop->data)) == 0) {
    int status = (w->work == uv__cancelled) ? UV_ECANCELED : 0;
    struct uv_loop_data* addr = (struct uv_loop_data*)((uint64_t)loop->data -
      (UV_EVENT_MAGIC_OFFSET << UV_EVENT_MAGIC_OFFSETBITS));
    uv_mutex_unlock(&loop->wq_mutex);
    if (req->type == UV_WORK) {
      addr->post_task_func((char*)req->reserved[1], uv__task_done_wrapper, (void*)w, status, qos);
    } else {
      addr->post_task_func(NULL, uv__task_done_wrapper, (void*)w, status, qos);
    }
  } else {
    uv__loop_internal_fields_t* lfields = uv__get_internal_fields(loop);
    uv__queue_insert_tail(&(lfields->wq_sub[qos]), &w->wq);
    uv_mutex_unlock(&loop->wq_mutex);
    uv_async_send(&loop->wq_async);
  }
  rdunlock_closed_uv_loop_rwlock();
}
#endif


/* TODO(bnoordhuis) teach libuv how to cancel file operations
 * that go through io_uring instead of the thread pool.
 */
static int uv__work_cancel(uv_loop_t* loop, uv_req_t* req, struct uv__work* w) {
  int cancelled;

#ifdef USE_FFRT
  rdlock_closed_uv_loop_rwlock();
  if (!is_uv_loop_good_magic(w->loop)) {
    rdunlock_closed_uv_loop_rwlock();
    return 0;
  }
#endif

#ifndef USE_FFRT
  uv_mutex_lock(&mutex);
  uv_mutex_lock(&w->loop->wq_mutex);

  cancelled = !uv__queue_empty(&w->wq) && w->work != NULL;
  if (cancelled)
    uv__queue_remove(&w->wq);

  uv_mutex_unlock(&w->loop->wq_mutex);
  uv_mutex_unlock(&mutex);
#else
  uv_mutex_lock(&w->loop->wq_mutex);
  cancelled = !uv__queue_empty(&w->wq) && w->work != NULL
    && ffrt_executor_task_cancel(w, (ffrt_qos_t)(intptr_t)req->reserved[0]);
  uv_mutex_unlock(&w->loop->wq_mutex);
#endif

  if (!cancelled) {
#ifdef USE_FFRT
    rdunlock_closed_uv_loop_rwlock();
#endif
    return UV_EBUSY;
  }

  w->work = uv__cancelled;
  uv_mutex_lock(&loop->wq_mutex);
#ifndef USE_FFRT
  uv__queue_insert_tail(&loop->wq, &w->wq);
  uv_async_send(&loop->wq_async);
#else
  uv__loop_internal_fields_t* lfields = uv__get_internal_fields(w->loop);
  int qos = (ffrt_qos_t)(intptr_t)req->reserved[0];

  if (uv_check_data_valid((struct uv_loop_data*)(w->loop->data)) == 0) {
    int status = (w->work == uv__cancelled) ? UV_ECANCELED : 0;
    struct uv_loop_data* addr = (struct uv_loop_data*)((uint64_t)w->loop->data -
      (UV_EVENT_MAGIC_OFFSET << UV_EVENT_MAGIC_OFFSETBITS));
    if (req->type == UV_WORK) {
      addr->post_task_func((char*)req->reserved[1], uv__task_done_wrapper, (void*)w, status, qos);
    } else {
      addr->post_task_func(NULL, uv__task_done_wrapper, (void*)w, status, qos);
    }
  } else {
    uv__queue_insert_tail(&(lfields->wq_sub[qos]), &w->wq);
    uv_async_send(&loop->wq_async);
  }
#endif
  uv_mutex_unlock(&loop->wq_mutex);
#ifdef USE_FFRT
  rdunlock_closed_uv_loop_rwlock();
#endif

  return 0;
}


void uv__work_done(uv_async_t* handle) {
  struct uv__work* w;
  uv_loop_t* loop;
  struct uv__queue* q;
  struct uv__queue wq;
  int err;
  int nevents;

  loop = container_of(handle, uv_loop_t, wq_async);
#ifdef USE_FFRT
  rdlock_closed_uv_loop_rwlock();
  if (!is_uv_loop_good_magic(loop)) {
    rdunlock_closed_uv_loop_rwlock();
    return;
  }
  rdunlock_closed_uv_loop_rwlock();
#endif

#ifdef USE_OHOS_DFX
  uv__print_active_reqs(loop, "complete");
  if (uv_check_data_valid((struct uv_loop_data*)(loop->data)) == 0) {
    return;
  }
#endif

  uv_mutex_lock(&loop->wq_mutex);
#ifndef USE_FFRT
  uv__queue_move(&loop->wq, &wq);
#else
  uv__loop_internal_fields_t* lfields = uv__get_internal_fields(loop);
  int i;
  uv__queue_init(&wq);
  for (i = 4; i >= 0; i--) {
    if (!uv__queue_empty(&lfields->wq_sub[i])) {
      uv__queue_append(&lfields->wq_sub[i], &wq);
    }
  }
#endif
  uv_mutex_unlock(&loop->wq_mutex);

  nevents = 0;
  uv_start_trace(UV_TRACE_TAG, UV_TRACE_NAME);
  while (!uv__queue_empty(&wq)) {
    q = uv__queue_head(&wq);
    uv__queue_remove(q);

    w = container_of(q, struct uv__work, wq);
    err = (w->work == uv__cancelled) ? UV_ECANCELED : 0;
    w->done(w, err);
    nevents++;
  }
  uv_end_trace(UV_TRACE_TAG);

  /* This check accomplishes 2 things:
   * 1. Even if the queue was empty, the call to uv__work_done() should count
   *    as an event. Which will have been added by the event loop when
   *    calling this callback.
   * 2. Prevents accidental wrap around in case nevents == 0 events == 0.
   */
  if (nevents > 1) {
    /* Subtract 1 to counter the call to uv__work_done(). */
    uv__metrics_inc_events(loop, nevents - 1);
    if (uv__get_internal_fields(loop)->current_timeout == 0)
      uv__metrics_inc_events_waiting(loop, nevents - 1);
  }
}


#ifdef USE_FFRT
static void uv__queue_work(struct uv__work* w, int qos) {
#else
static void uv__queue_work(struct uv__work* w) {
#endif
  uv_work_t* req = container_of(w, uv_work_t, work_req);
#ifdef ASYNC_STACKTRACE
  LibuvSetStackId((uint64_t)req->reserved[3]);
#endif
  req->work_cb(req);
#ifdef USE_FFRT
  uv__work_submit_to_eventloop(req, w, qos);
#endif
}


static void uv__queue_done(struct uv__work* w, int err) {
  uv_work_t* req;

  if (w == NULL) {
    UV_LOGE("uv_work_t is NULL");
    return;
  }

  req = container_of(w, uv_work_t, work_req);
#ifdef ASYNC_STACKTRACE
  LibuvSetStackId((uint64_t)req->reserved[3]);
#endif
  uv__req_unregister(req->loop, req);

  if (req->after_work_cb == NULL)
    return;
#ifdef USE_FFRT
  if (req->reserved[1] != NULL) {
    free((char*)req->reserved[1]);
    req->reserved[1] = NULL;
  }
#endif
  req->after_work_cb(req, err);
}


#ifdef USE_FFRT
void uv__ffrt_work(ffrt_executor_task_t* data, ffrt_qos_t qos)
{
  struct uv__work* w = (struct uv__work *)data;
  w->work(w, (int)qos);
}

static void init_once(void)
{
  init_closed_uv_loop_rwlock_once();
  /* init uv work statics queue */
  ffrt_executor_task_register_func(uv__ffrt_work, ffrt_uv_task);
}


/* ffrt uv__work_submit */
void uv__work_submit(uv_loop_t* loop,
                     uv_req_t* req,
                     struct uv__work* w,
                     enum uv__work_kind kind,
                     void (*work)(struct uv__work *w, int qos),
                     void (*done)(struct uv__work *w, int status)) {
  uv_once(&once, init_once);
  ffrt_task_attr_t attr;
  ffrt_task_attr_init(&attr);

  switch(kind) {
    case UV__WORK_CPU:
      ffrt_task_attr_set_qos(&attr, ffrt_qos_default);
      break;
    case UV__WORK_FAST_IO:
      ffrt_task_attr_set_qos(&attr, ffrt_qos_default);
      break;
    case UV__WORK_SLOW_IO:
      ffrt_task_attr_set_qos(&attr, ffrt_qos_background);
      break;
    default:
#ifdef USE_OHOS_DFX
      UV_LOGI("Unknown work kind");
#endif
      return;
  }

  w->loop = loop;
  w->work = work;
  w->done = done;

  req->reserved[0] = (void *)(intptr_t)ffrt_task_attr_get_qos(&attr);
  ffrt_executor_task_submit((ffrt_executor_task_t *)w, &attr);
  ffrt_task_attr_destroy(&attr);
}


/* ffrt uv__work_submit */
void uv__work_submit_with_qos(uv_loop_t* loop,
                              uv_req_t* req,
                              struct uv__work* w,
                              ffrt_qos_t qos,
                              void (*work)(struct uv__work *w, int qos),
                              void (*done)(struct uv__work *w, int status)) {
    uv_once(&once, init_once);
    ffrt_task_attr_t attr;
    ffrt_task_attr_init(&attr);
    ffrt_task_attr_set_qos(&attr, qos);

    w->loop = loop;
    w->work = work;
    w->done = done;

    req->reserved[0] = (void *)(intptr_t)ffrt_task_attr_get_qos(&attr);
    ffrt_executor_task_submit((ffrt_executor_task_t *)w, &attr);
    ffrt_task_attr_destroy(&attr);
}
#endif


int uv_queue_work(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  if (work_cb == NULL)
    return UV_EINVAL;
#ifdef USE_FFRT
  req->reserved[1] = NULL;
#endif
  uv__print_active_reqs(loop, "execute");
  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;

#ifdef ASYNC_STACKTRACE
  /* The req->reserved[3] is used for DFX only. */
  req->reserved[3] = (void*)LibuvCollectAsyncStack();
#endif
  uv__work_submit(loop,
#ifdef USE_FFRT
                  (uv_req_t*)req,
#endif
                  &req->work_req,
                  UV__WORK_CPU,
                  uv__queue_work,
                  uv__queue_done
);
  return 0;
}


int uv_queue_work_internal(uv_loop_t* loop,
                           uv_work_t* req,
                           uv_work_cb work_cb,
                           uv_after_work_cb after_work_cb,
                           const char* task_name) {
#ifdef USE_FFRT
  if (work_cb == NULL)
    return UV_EINVAL;

  int ret = uv__copy_taskname((uv_req_t*)req, task_name);
  if (ret != 0) {
    req->reserved[1] = NULL;
  }

  uv__print_active_reqs(loop, "execute");
  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;

#ifdef ASYNC_STACKTRACE
  /* The req->reserved[3] is used for DFX only. */
  req->reserved[3] = (void*)LibuvCollectAsyncStack();
#endif
  uv__work_submit(loop,
                  (uv_req_t*)req,
                  &req->work_req,
                  UV__WORK_CPU,
                  uv__queue_work,
                  uv__queue_done);
  return 0;
#else
  return uv_queue_work(loop, req, work_cb, after_work_cb);
#endif
}


int uv_queue_work_with_qos(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb,
                  uv_qos_t qos) {
#ifdef USE_FFRT
  if (work_cb == NULL)
    return UV_EINVAL;

  req->reserved[1] = NULL;
  STATIC_ASSERT(uv_qos_background == ffrt_qos_background);
  STATIC_ASSERT(uv_qos_utility == ffrt_qos_utility);
  STATIC_ASSERT(uv_qos_default == ffrt_qos_default);
  STATIC_ASSERT(uv_qos_user_initiated == ffrt_qos_user_initiated);
  STATIC_ASSERT(uv_qos_user_interactive == ffrt_qos_deadline_request);
  if (qos < ffrt_qos_background || qos > ffrt_qos_deadline_request) {
    return UV_EINVAL;
  }

  uv__print_active_reqs(loop, "execute");
  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;

#ifdef ASYNC_STACKTRACE
  /* The req->reserved[3] is used for DFX only. */
  req->reserved[3] = (void*)LibuvCollectAsyncStack();
#endif
  uv__work_submit_with_qos(loop,
                  (uv_req_t*)req,
                  &req->work_req,
                  (ffrt_qos_t)qos,
                  uv__queue_work,
                  uv__queue_done);
  return 0;
#else
  return uv_queue_work(loop, req, work_cb, after_work_cb);
#endif
}


int uv_queue_work_with_qos_internal(uv_loop_t* loop,
                           uv_work_t* req,
                           uv_work_cb work_cb,
                           uv_after_work_cb after_work_cb,
                           uv_qos_t qos,
                           const char* task_name) {
#ifdef USE_FFRT
  if (work_cb == NULL)
    return UV_EINVAL;

  int ret = uv__copy_taskname((uv_req_t*)req, task_name);
  if (ret != 0) {
    req->reserved[1] = NULL;
  }

  STATIC_ASSERT(uv_qos_background == ffrt_qos_background);
  STATIC_ASSERT(uv_qos_utility == ffrt_qos_utility);
  STATIC_ASSERT(uv_qos_default == ffrt_qos_default);
  STATIC_ASSERT(uv_qos_user_initiated == ffrt_qos_user_initiated);
  STATIC_ASSERT(uv_qos_user_interactive == ffrt_qos_deadline_request);
  if (qos < ffrt_qos_background || qos > ffrt_qos_deadline_request) {
    return UV_EINVAL;
  }

  uv__print_active_reqs(loop, "execute");
  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;

#ifdef ASYNC_STACKTRACE
  /* The req->reserved[3] is used for DFX only. */
  req->reserved[3] = (void*)LibuvCollectAsyncStack();
#endif
  uv__work_submit_with_qos(loop,
                  (uv_req_t*)req,
                  &req->work_req,
                  (ffrt_qos_t)qos,
                  uv__queue_work,
                  uv__queue_done);
  return 0;
#else
  return uv_queue_work_with_qos(loop, req, work_cb, after_work_cb, qos);
#endif
}


int uv_cancel(uv_req_t* req) {
  struct uv__work* wreq;
  uv_loop_t* loop;

  switch (req->type) {
  case UV_FS:
    loop =  ((uv_fs_t*) req)->loop;
    wreq = &((uv_fs_t*) req)->work_req;
    break;
  case UV_GETADDRINFO:
    loop =  ((uv_getaddrinfo_t*) req)->loop;
    wreq = &((uv_getaddrinfo_t*) req)->work_req;
    break;
  case UV_GETNAMEINFO:
    loop = ((uv_getnameinfo_t*) req)->loop;
    wreq = &((uv_getnameinfo_t*) req)->work_req;
    break;
  case UV_RANDOM:
    loop = ((uv_random_t*) req)->loop;
    wreq = &((uv_random_t*) req)->work_req;
    break;
  case UV_WORK:
    loop =  ((uv_work_t*) req)->loop;
    wreq = &((uv_work_t*) req)->work_req;
    break;
  default:
    return UV_EINVAL;
  }

  return uv__work_cancel(loop, req, wreq);
}
