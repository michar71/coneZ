#ifndef _conez_curve_h
#define _conez_curve_h

#include <Arduino.h>

// Linear interpolation: returns a + t * (b - a).
float lerp(float a, float b, float t);

// Smoothed clamped linear interpolation.
// Maps x_pos from [x_min, x_max] to [min_val, max_val] with optional
// offset (narrows the ramp zone, creating flat shoulders) and
// window/stride averaging (smooths the transition corners).
//   offset:  percentage (0-100) of half-range to shrink from each end
//   window:  percentage (0-100) of offset region used as smoothing width
//   stride:  step size for samples within the smoothing window
int larp(int x_pos, int x_min, int x_max, int min_val, int max_val,
         int offset, int window, int stride);

// Float version of larp. stride is the number of subdivisions of
// the smoothing window (not an absolute step size).
float larpf(float x_pos, float x_min, float x_max, float min_val, float max_val,
            float offset, float window, int stride);

#endif
