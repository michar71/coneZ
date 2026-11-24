#include "curve.h"
#include "compat.h"
#include <stdint.h>

// Upper bound on samples averaged by larp()/larpf(). The arguments come straight
// from scripts (WASM imports), and these run inside a host call that a script
// `stop` cannot interrupt, so the loops must be bounded no matter what is
// passed. Real use is a few dozen samples; past the cap the samples are spread
// over the same window with a wider spacing.
#define LARP_MAX_SAMPLES 4096

float conez_lerp(float a, float b, float t)
{
  return a + t * (b - a);
}

int larp(int x_pos, int x_min, int x_max, int min_val, int max_val,
         int offset, int window, int stride)
{
  if (x_min == x_max)
    return min_val;

  // Calculate offset and window in terms of the range
  int range = x_max - x_min;
  int offset_int = (range / 2) * offset / 100;
  int window_int = offset_int * window / 100;

  // Ensure stride is at least 1 to avoid infinite loop
  if (stride < 1)
    stride = 1;

  float sum = 0;
  int count = 0;

  // 64-bit loop variable: with x_pos or the window near INT_MAX, "i += stride"
  // overflowed and the loop never ended. And a huge window with a small stride
  // is billions of iterations, so widen the stride to stay under the cap.
  int64_t half = window_int / 2;
  int64_t lo = (int64_t)x_pos - half;
  int64_t hi = (int64_t)x_pos + half;
  int64_t istride = stride;
  if (hi >= lo && (hi - lo) / istride >= LARP_MAX_SAMPLES)
    istride = (hi - lo) / (LARP_MAX_SAMPLES - 1) + 1;

  for (int64_t i = lo; i <= hi; i += istride)
  {
    if (i < x_min)
    {
      sum += min_val;
    }
    else if (i > x_max)
    {
      sum += max_val;
    }
    else
    {
      int active_min = x_min + offset_int;
      int active_max = x_max - offset_int;
      float t = (float)(i - active_min) / (active_max - active_min);
      t = constrain(t, 0.0f, 1.0f);
      sum += conez_lerp(min_val, max_val, t);
    }
    count++;
  }

  if (count > 0)
    return round(sum / count);

  return min_val;
}

float larpf(float x_pos, float x_min, float x_max, float min_val, float max_val,
            float offset, float window, int stride)
{
  if (x_min == x_max)
    return min_val;

  float range = x_max - x_min;
  float offset_f = (range / 2.0f) * offset / 100.0f;
  float window_f = offset_f * window / 100.0f;

  if (stride < 1)
    stride = 1;

  float step = window_f / stride;
  if (step < 0.001f)
    step = 1.0f;

  float sum = 0;
  int count = 0;

  // Sample count worked out up front and driven by an INTEGER index. The old
  // "for (float s = ...; s <= ...; s += step)" never advanced once step was
  // below half a float ULP of s -- e.g. x_pos = millis() after ~28 h (1e8, ULP
  // 8) with step 2 -- and WasmTask spun until reboot.
  int n = 0;
  if (window_f >= 0.0f) {             // false for a negative window and for NaN
    float q = window_f / step;
    if (q >= (float)(LARP_MAX_SAMPLES - 1)) {
      n = LARP_MAX_SAMPLES;
      step = window_f / (float)(LARP_MAX_SAMPLES - 1);
    } else {
      n = (int)q + 1;
    }
  }
  float start = x_pos - (window_f / 2.0f);

  for (int k = 0; k < n; k++)
  {
    float s = start + (float)k * step;
    if (s < x_min)
    {
      sum += min_val;
    }
    else if (s > x_max)
    {
      sum += max_val;
    }
    else
    {
      float active_min = x_min + offset_f;
      float active_max = x_max - offset_f;
      float t = (s - active_min) / (active_max - active_min);
      t = constrain(t, 0.0f, 1.0f);
      sum += conez_lerp(min_val, max_val, t);
    }
    count++;
  }

  if (count > 0)
    return sum / count;

  return min_val;
}
