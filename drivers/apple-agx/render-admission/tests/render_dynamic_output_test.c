#include "render_dynamic_output.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 256u
#define HEIGHT 160u
#define BACKGROUND 0xff101820u
#define FOREGROUND 0xff1acc66u

static unsigned progress_calls;

static int progress(void *context) {
  (void)context;
  ++progress_calls;
  return 1;
}

static void write_pixel(unsigned char *bytes, unsigned x, unsigned y,
                        unsigned value) {
  unsigned char *pixel = bytes + (y * WIDTH + x) * 4u;
  pixel[0] = (unsigned char)value;
  pixel[1] = (unsigned char)(value >> 8u);
  pixel[2] = (unsigned char)(value >> 16u);
  pixel[3] = (unsigned char)(value >> 24u);
}

static void build_triangle(unsigned char *bytes) {
  for (unsigned y = 0u; y < HEIGHT; ++y)
    for (unsigned x = 0u; x < WIDTH; ++x)
      write_pixel(bytes, x, y, BACKGROUND);
  for (unsigned y = 16u; y < 144u; ++y) {
    unsigned dy = y - 16u;
    unsigned left = 26u + (dy * 102u) / 128u;
    unsigned right = 230u - (dy * 102u) / 128u;
    for (unsigned x = left; x < right; ++x)
      write_pixel(bytes, x, y, FOREGROUND);
  }
}

int main(void) {
  unsigned char *bytes = (unsigned char *)malloc(WIDTH * HEIGHT * 4u);
  ADMISSION_DYNAMIC_OUTPUT_EXPECTATION expectation = {
      WIDTH, HEIGHT, WIDTH * 4u, BACKGROUND,
      128u, 80u, 24u, 14u, 232u, 146u,
      12000u, 14000u, 0xa5u};
  ADMISSION_DYNAMIC_OUTPUT_RESULT result;
  assert(bytes != NULL);
  build_triangle(bytes);
  progress_calls = 0u;
  assert(AdmissionDynamicOutputVerify(
      bytes, WIDTH * HEIGHT * 4u, &expectation, 4096u,
      progress, NULL, &result));
  assert(result.Valid == 1u && result.ForegroundColor == FOREGROUND);
  assert(result.ForegroundPixels >= 12000u &&
         result.ForegroundPixels <= 14000u);
  assert(result.BackgroundPixels + result.ForegroundPixels == WIDTH * HEIGHT);
  assert(result.FirstInvalidPixel == 0xffffffffu && result.PoisonPixels == 0u);
  assert(result.BytesExamined == WIDTH * HEIGHT * 4u && result.Fnv1a != 0ULL);
  assert(progress_calls != 0u);

  write_pixel(bytes, 0u, 0u, FOREGROUND);
  assert(AdmissionDynamicOutputVerify(
      bytes, WIDTH * HEIGHT * 4u, &expectation, 0u,
      NULL, NULL, &result));
  assert(result.Valid == 0u && result.FirstInvalidPixel == 0u);
  build_triangle(bytes);
  write_pixel(bytes, 100u, 80u, 0xffabcdefu);
  assert(AdmissionDynamicOutputVerify(
      bytes, WIDTH * HEIGHT * 4u, &expectation, 0u,
      NULL, NULL, &result));
  assert(result.Valid == 0u);
  build_triangle(bytes);
  write_pixel(bytes, 100u, 80u, 0xa5a5a5a5u);
  assert(AdmissionDynamicOutputVerify(
      bytes, WIDTH * HEIGHT * 4u, &expectation, 0u,
      NULL, NULL, &result));
  assert(result.Valid == 0u && result.PoisonPixels == 1u);
  free(bytes);
  return 0;
}
