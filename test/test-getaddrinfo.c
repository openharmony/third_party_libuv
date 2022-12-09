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

#include "uv.h"
#include "task.h"
#include <stdlib.h>

#define CONCURRENT_COUNT    10

static int fail_cb_called;


static void getaddrinfo_fail_cb(uv_getaddrinfo_t* req,
                                int status,
                                struct addrinfo* res) {

  ASSERT(fail_cb_called == 0);
  ASSERT(status < 0);
  ASSERT_NULL(res);
  uv_freeaddrinfo(res);  /* Should not crash. */
  fail_cb_called++;
}


TEST_IMPL(getaddrinfo_fail) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif

  uv_getaddrinfo_t req;

  ASSERT(UV_EINVAL == uv_getaddrinfo(uv_default_loop(),
                                     &req,
                                     (uv_getaddrinfo_cb) abort,
                                     NULL,
                                     NULL,
                                     NULL));

  /* Use a FQDN by ending in a period */
  ASSERT(0 == uv_getaddrinfo(uv_default_loop(),
                             &req,
                             getaddrinfo_fail_cb,
                             "example.invalid.",
                             NULL,
                             NULL));
  ASSERT(0 == uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT(fail_cb_called == 1);

  MAKE_VALGRIND_HAPPY();
  return 0;
}


TEST_IMPL(getaddrinfo_fail_sync) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif
  uv_getaddrinfo_t req;

  /* Use a FQDN by ending in a period */
  ASSERT(0 > uv_getaddrinfo(uv_default_loop(),
                            &req,
                            NULL,
                            "example.invalid.",
                            NULL,
                            NULL));
  uv_freeaddrinfo(req.addrinfo);

  MAKE_VALGRIND_HAPPY();
  return 0;
}

