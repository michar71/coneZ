using System;

namespace Mayhem.Models;

public static class ParamMath
{
    // LARP: Linear interpolation with optional offset/window/stride sampling.
    public static int Larp(
        int xPos,
        int xMin,
        int xMax,
        int minVal,
        int maxVal,
        int offset,
        int window,
        int stride)
    {
        if (xMin == xMax)
        {
            return minVal;
        }

        var range = xMax - xMin;
        var offsetInt = (range / 2) * offset / 100;
        var windowInt = offsetInt * window / 100;

        if (stride <= 0)
        {
            stride = 1;
        }

        if (windowInt <= 0)
        {
            windowInt = stride;
        }

        var start = xPos - (windowInt / 2);
        var end = xPos + (windowInt / 2);
        var den = (xMax - offsetInt) - (xMin + offsetInt);

        float sum = 0;
        var count = 0;

        for (var i = start; i <= end; i += stride)
        {
            if (i < xMin)
            {
                sum += minVal;
                count++;
                continue;
            }

            if (i > xMax)
            {
                sum += maxVal;
                count++;
                continue;
            }

            float t;
            if (den == 0)
            {
                t = 0;
            }
            else
            {
                t = (float)(i - (xMin + offsetInt)) / den;
            }

            t = Math.Clamp(t, 0f, 1f);
            var value = Lerp(minVal, maxVal, t);
            sum += value;
            count++;
        }

        if (count <= 0)
        {
            return minVal;
        }

        return (int)MathF.Round(sum / count, MidpointRounding.AwayFromZero);
    }

    private static int Lerp(int a, int b, float t)
    {
        var value = a + ((b - a) * t);
        return (int)MathF.Round(value, MidpointRounding.AwayFromZero);
    }
}
