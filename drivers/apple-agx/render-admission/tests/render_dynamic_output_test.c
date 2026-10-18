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

static unsigned tiled_64_offset(unsigned x, unsigned y) {
  return ((x & 1u) << 0u) | ((y & 1u) << 1u) |
         ((x & 2u) << 1u) | ((y & 2u) << 2u) |
         ((x & 4u) << 2u) | ((y & 4u) << 3u) |
         ((x & 8u) << 3u) | ((y & 8u) << 4u) |
         ((x & 16u) << 4u) | ((y & 16u) << 5u) |
         ((x & 32u) << 5u) | ((y & 32u) << 6u);
}

static void write_tiled_pixel(unsigned char *bytes, unsigned x, unsigned y,
                              unsigned value) {
  unsigned char *pixel = bytes + tiled_64_offset(x, y) * 4u;
  memcpy(pixel, &value, sizeof(value));
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

static void test_native_16x16_expectation(void) {
  unsigned char bytes[16u * 16u * 4u];
  ADMISSION_DYNAMIC_OUTPUT_EXPECTATION expectation;
  ADMISSION_DYNAMIC_OUTPUT_RESULT result;
  unsigned background = BACKGROUND;
  unsigned foreground = 0xff0000ffu;
  for (unsigned y = 0u; y < 16u; ++y)
    for (unsigned x = 0u; x < 16u; ++x) {
      unsigned char *pixel = bytes + (y * 16u + x) * 4u;
      memcpy(pixel, &background, sizeof(background));
    }
  for (unsigned y = 3u; y <= 13u; ++y) {
    unsigned halfWidth = (y - 3u) / 2u + 1u;
    unsigned left = 8u - halfWidth;
    unsigned right = 8u + halfWidth;
    for (unsigned x = left; x < right; ++x)
      memcpy(bytes + (y * 16u + x) * 4u,
             &foreground, sizeof(foreground));
  }
  assert(AdmissionDynamicOutputDescribeExpectation(
      16u, 16u, 64u, BACKGROUND, foreground,
      AdmissionDynamicOutputLayoutLinear, &expectation));
  assert(expectation.MinimumForegroundPixels == 72u &&
         expectation.MaximumForegroundPixels == 72u);
  assert(AdmissionDynamicOutputVerify(
      bytes, sizeof(bytes), &expectation, 0u, NULL, NULL, &result));
  assert(result.Valid == 1u && result.ForegroundPixels == 72u &&
         result.ObservedForegroundColor == foreground);
  assert(!AdmissionDynamicOutputDescribeExpectation(
      16u, 16u, 80u, BACKGROUND, foreground,
      AdmissionDynamicOutputLayoutLinear, &expectation));
}

static void test_completed_output_snapshot(void) {
  unsigned char bytes[ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT_CAPACITY];
  ADMISSION_DYNAMIC_OUTPUT_EXPECTATION expectation;
  ADMISSION_DYNAMIC_OUTPUT_RESULT result;
  ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT snapshot;
  unsigned background = 0xff112233u;
  unsigned foreground = 0x80808080u;
  for (unsigned y = 0u; y < 16u; ++y)
    for (unsigned x = 0u; x < 16u; ++x)
      write_tiled_pixel(bytes, x, y, background);
  for (unsigned y = 3u; y <= 13u; ++y) {
    unsigned halfWidth = (y - 3u) / 2u + 1u;
    unsigned left = 8u - halfWidth;
    unsigned right = 8u + halfWidth;
    for (unsigned x = left; x < right; ++x)
      write_tiled_pixel(bytes, x, y, foreground);
  }
  AdmissionDynamicOutputSnapshotInitialize(&snapshot);
  assert(AdmissionDynamicOutputSnapshotCapture(
      &snapshot, 271u, 7u, 0x1500fa0000ULL, 0x9bd140000ULL,
      bytes, sizeof(bytes), AdmissionDynamicOutputLayoutAgxTiled64,
      foreground));
  memset(bytes, 0xa5, sizeof(bytes));
  assert(snapshot.Version == ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT_VERSION &&
         snapshot.Bytes == sizeof(snapshot) && snapshot.Valid == 1u &&
         snapshot.Fence == 271u && snapshot.Generation == 7u &&
         snapshot.DataBytes == sizeof(bytes) && snapshot.Status == 0u &&
         snapshot.SourceGpuVa == 0x1500fa0000ULL &&
         snapshot.SourcePhysical == 0x9bd140000ULL &&
         snapshot.ExpectedLayout == AdmissionDynamicOutputLayoutAgxTiled64 &&
         snapshot.ExpectedForegroundColor == foreground &&
         snapshot.Fnv1a == 0xdd2c90074f6ee435ULL);
  assert(AdmissionDynamicOutputDescribeExpectation(
      16u, 16u, 64u, background, foreground,
      AdmissionDynamicOutputLayoutAgxTiled64, &expectation));
  assert(AdmissionDynamicOutputVerify(
      snapshot.Data, snapshot.DataBytes, &expectation, 0u,
      NULL, NULL, &result));
  assert(result.Valid == 1u && result.ObservedForegroundColor == foreground &&
         result.ForegroundPixels == 72u && result.BackgroundPixels == 184u &&
         result.Fnv1a == snapshot.Fnv1a);
  assert(AdmissionDynamicOutputSnapshotRecordVerification(&snapshot, &result));
  assert(snapshot.ObservedForegroundColor == foreground &&
         snapshot.VerificationValid == 1u);
  assert(!AdmissionDynamicOutputSnapshotCapture(
      &snapshot, 272u, 8u, 0x1500fb0000ULL, 0x9bd150000ULL,
      bytes, sizeof(bytes), AdmissionDynamicOutputLayoutAgxTiled64,
      foreground));
  AdmissionDynamicOutputSnapshotInitialize(&snapshot);
  assert(!AdmissionDynamicOutputSnapshotCapture(
      &snapshot, 0u, 7u, 0x1500fa0000ULL, 0x9bd140000ULL,
      bytes, sizeof(bytes), AdmissionDynamicOutputLayoutAgxTiled64,
      foreground));
  assert(!AdmissionDynamicOutputSnapshotCapture(
      &snapshot, 271u, 0u, 0x1500fa0000ULL, 0x9bd140000ULL,
      bytes, sizeof(bytes), AdmissionDynamicOutputLayoutAgxTiled64,
      foreground));
  assert(!AdmissionDynamicOutputSnapshotCapture(
      &snapshot, 271u, 7u, 0x1500fa0000ULL, 0x9bd140000ULL,
      bytes, sizeof(bytes) - 1u, AdmissionDynamicOutputLayoutAgxTiled64,
      foreground));
}

int main(void) {
  unsigned char *bytes = (unsigned char *)malloc(WIDTH * HEIGHT * 4u);
  ADMISSION_DYNAMIC_OUTPUT_EXPECTATION expectation = {
      WIDTH, HEIGHT, WIDTH * 4u, BACKGROUND, FOREGROUND,
      AdmissionDynamicOutputLayoutLinear,
      128u, 80u, 24u, 14u, 232u, 146u,
      12000u, 14000u, 0xa5u};
  ADMISSION_DYNAMIC_OUTPUT_RESULT result;
  test_native_16x16_expectation();
  test_completed_output_snapshot();
  assert(bytes != NULL);
  build_triangle(bytes);
  progress_calls = 0u;
  assert(AdmissionDynamicOutputVerify(
      bytes, WIDTH * HEIGHT * 4u, &expectation, 4096u,
      progress, NULL, &result));
  assert(result.Valid == 1u && result.ObservedForegroundColor == FOREGROUND);
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
