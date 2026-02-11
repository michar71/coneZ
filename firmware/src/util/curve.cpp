#include "curve.h"

float lerp(float a, float b, float t)
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

  for (int i = x_pos - (window_int / 2); i <= x_pos + (window_int / 2); i += stride)
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
      sum += lerp(min_val, max_val, t);
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

  for (float s = x_pos - (window_f / 2.0f); s <= x_pos + (window_f / 2.0f); s += step)
  {
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
      sum += lerp(min_val, max_val, t);
    }
    count++;
  }

  if (count > 0)
    return sum / count;

  return min_val;
}
