#pragma once

static const float c_pi = 3.14159265359f;

template <typename T>
inline T clamp(T v, T min, T max)
{
    if (v < min)
        return min;
    else if (v > max)
        return max;
    else return v;
}

template <typename T>
inline T lerp(const T& a, const T& b, float t)
{
    return a * (1.0f - t) + b * t;
}

inline float DegreesToRadians(float degrees)
{
    return degrees * c_pi / 180.0f;
}