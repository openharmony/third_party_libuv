/* Copyright libuv project contributors. All rights reserved.
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
#include <string.h>

#include "../src/strtok.h"
#include "../src/strtok.c"

struct strtok_test_case {
  const char* str;
  const char* sep;
};

const char* tokens[] = {
  "abc",
  NULL,

  "abc",
  "abf",
  NULL,

  "This",
  "is.a",
  "test",
  "of",
  "the",
  "string",
  "tokenizer",
  "function.",
  NULL,

  "Hello",
  "This-is-a-nice",
  "-string",
  NULL
};

#define ASSERT_STRCMP(x, y) \
  ASSERT((x != NULL && y != NULL && strcmp(x, y) == 0) || (x == y && x == NULL))

TEST_IMPL(strtok) {
  return 0;
}
