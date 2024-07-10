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

/*
 * This file is private to libuv. It provides common functionality to both
 * Windows and Unix backends.
 */

#ifndef UV_THREADPOOL_H_
#define UV_THREADPOOL_H_

#ifdef UV_STATISTIC
enum uv_work_state {
  WAITING = 0,
  WORK_EXECUTING,
  WORK_END,
  DONE_EXECUTING,
  DONE_END,
};

/* used for dump uv work infomation */
struct uv_work_dump_info {
  uint64_t queue_time;
  void* builtin_return_address[3]; // backtrace the caller.

  enum uv_work_state state;

  uint64_t execute_start_time;
  uint64_t execute_end_time;
  uint64_t done_start_time;
  uint64_t done_end_time;

  struct uv__work* work;

  struct uv__queue wq;
};

struct uv__statistic_work {
  void (*work)(struct uv__statistic_work *w);
  struct uv_work_dump_info* info;
  enum uv_work_state state;
  uint64_t time;
  struct uv__queue wq;
};
#endif

struct uv__work {
  void (*work)(struct uv__work *w);
  void (*done)(struct uv__work *w, int status);
  struct uv_loop_s* loop;
  struct uv__queue wq;
#ifdef UV_STATISTIC
  struct uv_work_dump_info* info;
#endif
};

#endif /* UV_THREADPOOL_H_ */
