#pragma once

#include <array>

typedef std::array<float, 2> Vector2;
typedef std::array<float, 3> Vector3;

// indexed as [row][column]
typedef std::array<Vector2, 2> Matrix22;
typedef std::array<Vector3, 3> Matrix33;

static const Matrix22 c_identity22 = 
{
    {
        {1.0f, 0.0f},
        {0.0f, 1.0f}
    }
};

static const Matrix33 c_identity33 = 
{
    {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    }
};

Matrix22 Rotation22 (float thetaRadians)
{
    float sinTheta = std::sinf(thetaRadians);
    float cosTheta = std::cosf(thetaRadians);

    return Matrix22
    {
        {
            {cosTheta, sinTheta},
            {-sinTheta, cosTheta}
        }
    };
}

Matrix33 Rotation33 (float thetaRadians)
{
    float sinTheta = std::sinf(thetaRadians);
    float cosTheta = std::cosf(thetaRadians);

    return Matrix33
    {
        {
            {cosTheta, sinTheta, 0.0f},
            {-sinTheta, cosTheta, 0.0f},
            {0.0f, 0.0f, 1.0f},
        }
    };
}

template <size_t N>
std::array<float, N> operator * (const std::array<float, N>& p, const std::array<std::array<float, N>, N>& m)
{
    std::array<float, N> ret;
    for (int i = 0; i < N; ++i)
    {
        ret[i] = 0.0f;
        for (int j = 0; j < N; ++j)
            ret[i] += p[j] * m[j][i];
    }
    return ret;
}

template <size_t N>
std::array<std::array<float, N>, N> operator * (const std::array<std::array<float, N>, N>& a, const std::array<std::array<float, N>, N>& b)
{
    std::array<std::array<float, N>, N> ret;
    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < N; ++j)
        {
            ret[i][j] = 0.0f;
            for (int k = 0; k < N; ++k)
            {
                ret[i][j] += a[i][k] * b[k][j];
            }
        }
    }
    return ret;
}

template <size_t N>
float Dot(const std::array<float, N>& a, const std::array<float, N>& b)
{
    float ret = 0.0f;
    for (int i = 0; i < N; ++i)
        ret += a[i] * b[i];
    return ret;
}