#pragma once
#include <math.h>
#include <xhl/debug.h>
#include <xhl/maths.h>

static float sinarctan(float x) { return x / sqrtf(x * x + 1); }
static float sinarctan2(float x) { return xm_clampf(x, -1, 1) / sqrtf(x * x + 1); }
static float softsine(float x) { return x / (fabsf(x) + 1); }
static float softsine2(float x) { return xm_clampf(x, -1, 1) / (fabsf(x) + 1); }

// v - value between 0-1
// a - skew amount between 0-1 where lower values push x closer to 0 and vice versa
static inline float skewf(float v, float a)
{
    // va/(1-a-v+2va)
    if (a <= 0)
        return 0;
    else if (a >= 1.0f)
        return 1.0f;

    float va = v * a;
    return va / (1.0f - a - v + 2 * va);
}

static float interp_points(float norm_pos, float skew_amt, float point1_y, float point2_y)
{
    if (point1_y == point2_y)
        return point2_y;

    if (point1_y > point2_y)
        norm_pos = (1.0f - norm_pos);
    float skewed_pos = skewf(norm_pos, skew_amt);
    if (point1_y > point2_y)
        skewed_pos = (1.0f - skewed_pos);

    return xm_lerpf(skewed_pos, point1_y, point2_y);
}

// SvfLinearTrapOptimised2
// https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
typedef union Coeffs
{
    struct
    {
        float a1, a2, a3, m0, m1, m2;
    };
    void* _align;
} Coeffs;

static inline float calcG(float fc, float fs_inv /*sampleRateInv*/)
{
    return xm_fasttan_normalised(XM_HALF_PIf * 1.27f * fc * fs_inv);
}

static Coeffs filter_LP(float fc, float Q, float fs_inv)
{
    float g = calcG(fc, fs_inv);
    float k = 1.0f / Q;
    // float k = XM_SQRT2f; // Butterworth

    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;

    float m0 = 0.0f;
    float m1 = 0.0f;
    float m2 = 1.0f;
    return (Coeffs){a1, a2, a3, m0, m1, m2};
}

static Coeffs filter_HP(float fc, float Q, float fs_inv)
{
    float g = calcG(fc, fs_inv);
    float k = 1.0f / Q;
    // float k  = XM_SQRT2f; // Butterworth
    float a1 = 1 / (1 + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;
    float m0 = 1;
    float m1 = -k;
    float m2 = -1;
    return (Coeffs){a1, a2, a3, m0, m1, m2};
}

static inline float filter_process(float v0 /*xn*/, Coeffs* c, float* s)
{
    float v3 = v0 - s[1];
    float v1 = c->a1 * s[0] + c->a2 * v3;
    float v2 = s[1] + c->a2 * s[0] + c->a3 * v3;
    s[0]     = 2 * v1 - s[0];
    s[1]     = 2 * v2 - s[1];
    return c->m0 * v0 + c->m1 * v1 + c->m2 * v2;
}

typedef union BiquadCoeffs
{
    struct
    {
        float b0, b1, b2, a1, a2;
    };
    void* _align;
} BiquadCoeffs;

static inline BiquadCoeffs biquad_make_identity(void)
{
    return (BiquadCoeffs){1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
}

static inline float biquad_sanitise_freq(float hz, float sample_rate)
{
    const float nyquist = xm_maxf(20.0f, sample_rate * 0.5f);
    return xm_clampf(hz, 10.0f, nyquist * 0.94f);
}

static inline BiquadCoeffs biquad_normalise(float b0, float b1, float b2, float a0, float a1, float a2)
{
    if (fabsf(a0) < 1.0e-8f || a0 != a0)
        return biquad_make_identity();

    const float inv_a0 = 1.0f / a0;
    BiquadCoeffs c = {b0 * inv_a0, b1 * inv_a0, b2 * inv_a0, a1 * inv_a0, a2 * inv_a0};
    if (c.b0 != c.b0 || c.b1 != c.b1 || c.b2 != c.b2 || c.a1 != c.a1 || c.a2 != c.a2)
        return biquad_make_identity();
    return c;
}

static inline BiquadCoeffs biquad_make_low_shelf(float hz, float gain_dB, float sample_rate)
{
    if (fabsf(gain_dB) < 1.0e-4f)
        return biquad_make_identity();

    const float sr     = sample_rate > 0.0f ? sample_rate : 44100.0f;
    const float fc     = biquad_sanitise_freq(hz, sr);
    const float gain   = xm_clampf(gain_dB, -30.0f, 30.0f);
    const float A      = powf(10.0f, gain / 40.0f);
    const float w0     = XM_TAUf * fc / sr;
    const float cs     = cosf(w0);
    const float sn     = sinf(w0);
    const float alpha  = sn * XM_SQRT1_2f;
    const float sqrt_A = sqrtf(A);
    const float Ap1    = A + 1.0f;
    const float Am1    = A - 1.0f;
    const float two    = 2.0f * sqrt_A * alpha;

    const float b0 = A * (Ap1 - Am1 * cs + two);
    const float b1 = 2.0f * A * (Am1 - Ap1 * cs);
    const float b2 = A * (Ap1 - Am1 * cs - two);
    const float a0 = Ap1 + Am1 * cs + two;
    const float a1 = -2.0f * (Am1 + Ap1 * cs);
    const float a2 = Ap1 + Am1 * cs - two;
    return biquad_normalise(b0, b1, b2, a0, a1, a2);
}

static inline BiquadCoeffs biquad_make_peak(float hz, float q, float gain_dB, float sample_rate)
{
    if (fabsf(gain_dB) < 1.0e-4f)
        return biquad_make_identity();

    const float sr    = sample_rate > 0.0f ? sample_rate : 44100.0f;
    const float fc    = biquad_sanitise_freq(hz, sr);
    const float safe_q = xm_maxf(q, 0.05f);
    const float gain  = xm_clampf(gain_dB, -30.0f, 30.0f);
    const float A     = powf(10.0f, gain / 40.0f);
    const float w0    = XM_TAUf * fc / sr;
    const float cs    = cosf(w0);
    const float sn    = sinf(w0);
    const float alpha = sn / (2.0f * safe_q);

    const float b0 = 1.0f + alpha * A;
    const float b1 = -2.0f * cs;
    const float b2 = 1.0f - alpha * A;
    const float a0 = 1.0f + alpha / A;
    const float a1 = -2.0f * cs;
    const float a2 = 1.0f - alpha / A;
    return biquad_normalise(b0, b1, b2, a0, a1, a2);
}

static inline BiquadCoeffs biquad_make_high_shelf(float hz, float gain_dB, float sample_rate)
{
    if (fabsf(gain_dB) < 1.0e-4f)
        return biquad_make_identity();

    const float sr     = sample_rate > 0.0f ? sample_rate : 44100.0f;
    const float fc     = biquad_sanitise_freq(hz, sr);
    const float gain   = xm_clampf(gain_dB, -30.0f, 30.0f);
    const float A      = powf(10.0f, gain / 40.0f);
    const float w0     = XM_TAUf * fc / sr;
    const float cs     = cosf(w0);
    const float sn     = sinf(w0);
    const float alpha  = sn * XM_SQRT1_2f;
    const float sqrt_A = sqrtf(A);
    const float Ap1    = A + 1.0f;
    const float Am1    = A - 1.0f;
    const float two    = 2.0f * sqrt_A * alpha;

    const float b0 = A * (Ap1 + Am1 * cs + two);
    const float b1 = -2.0f * A * (Am1 + Ap1 * cs);
    const float b2 = A * (Ap1 + Am1 * cs - two);
    const float a0 = Ap1 - Am1 * cs + two;
    const float a1 = 2.0f * (Am1 - Ap1 * cs);
    const float a2 = Ap1 - Am1 * cs - two;
    return biquad_normalise(b0, b1, b2, a0, a1, a2);
}

static inline float biquad_process(float x, const BiquadCoeffs* c, float* z)
{
    const float y = c->b0 * x + z[0];
    z[0]          = c->b1 * x - c->a1 * y + z[1];
    z[1]          = c->b2 * x - c->a2 * y;

    if (y != y || z[0] != z[0] || z[1] != z[1])
    {
        z[0] = 0.0f;
        z[1] = 0.0f;
        return 0.0f;
    }

    return xm_clampf(y, -32.0f, 32.0f);
}

// Source: Will Pirkle - Designing Audio Effect Plugins in C++, somewhere in fxobjects.cpp
static double convert_compressor_time(double num_samples)
{
    static const double TLD_AUDIO_ENVELOPE_ANALOG_TC = -0.99967234081320612357829304641019;
    return exp(TLD_AUDIO_ENVELOPE_ANALOG_TC / num_samples);
}

// Giannoulis, Massberg, Reiss compressor
// http://eecs.qmul.ac.uk/~josh/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf
static inline float hard_knee_expander(float x_dB, float threshold_dB, float ratio)
{
    float y_dB;
    if (x_dB >= threshold_dB)
        y_dB = x_dB;
    else
        y_dB = threshold_dB + ratio * (x_dB - threshold_dB);
    return y_dB;
}

static inline float hard_knee_compress(float x_dB, float threshold_dB, float ratio_inv)
{
    float y_dB, hard_knee;
    hard_knee = threshold_dB + (x_dB - threshold_dB) * ratio_inv;

    y_dB = x_dB > threshold_dB ? hard_knee : x_dB;
    // protect against inf, which may occur in the division above if 'knee_dB' is 0
    xassert(y_dB == y_dB);

    return y_dB;
}

static inline float soft_knee_compress(float x_dB, float threshold_dB, float ratio_inv, float knee_dB)
{
    float y_dB, hard_knee, soft_knee, v, cond;
    v         = x_dB - threshold_dB + knee_dB / 2.0f;
    hard_knee = threshold_dB + (x_dB - threshold_dB) * ratio_inv;
    soft_knee = x_dB + ((ratio_inv - 1.0f) * v * v) / (2 * knee_dB);

    cond = 2 * (x_dB - threshold_dB);
    y_dB = cond <= -knee_dB ? x_dB : cond > knee_dB ? hard_knee : soft_knee;
    // protect against inf, which may occur in the division above if 'knee_dB' is 0
    xassert(y_dB == y_dB);

    return y_dB;
}

static inline float detect_peak(float x, float yn_1, float attack, float release)
{
    float y = 0.0f;

    if (x > yn_1)
        y = attack * yn_1 + (1 - attack) * x;
    else
        y = release * yn_1;

    if (y < 1.0e-6f)
        y = 0;

    return y;
}

static inline float detect_rms(float x, float z1, float attack, float release)
{
    float y = 0.0f;

    if (x > z1)
        y = sqrtf(attack * z1 * z1 + (1 - attack) * x * x);
    else
        y = release * z1;

    if (y < 1.0e-8f)
        y = 0;

    return y;
}

static inline float hard_knee_upwards_compress(float x_dB, float threshold_dB, float ratio_inv)
{
    float y_dB, hard_knee, soft_knee, v, cond;
    hard_knee = threshold_dB + (x_dB - threshold_dB) * ratio_inv;

    y_dB = x_dB > threshold_dB ? x_dB : hard_knee;
    xassert(y_dB == y_dB);

    return y_dB;
}

static inline float soft_knee_upwards_compress(float x_dB, float threshold_dB, float ratio_inv, float knee_dB)
{
    float y_dB, hard_knee, soft_knee, v, cond;
    v         = x_dB - threshold_dB - knee_dB / 2.0f;
    soft_knee = x_dB + (1.0f - ratio_inv) * v * v / (2 * knee_dB);
    hard_knee = threshold_dB + (x_dB - threshold_dB) * ratio_inv;

    cond = 2 * (x_dB - threshold_dB);
    y_dB = cond <= -knee_dB ? hard_knee : cond > knee_dB ? x_dB : soft_knee;
    xassert(y_dB == y_dB);

    return y_dB;
}

static inline float distort_upwards_compress(float input, float* xn_1, float ratio_inv, float atk, float rel)
{
    // up comp waveshaping
    float in_dB     = xm_fast_gain_to_dB(fabsf(input));
    float boost_dB  = hard_knee_upwards_compress(in_dB, 0, ratio_inv);
    boost_dB       -= in_dB;
    float boost_G   = xm_fast_dB_to_gain(boost_dB);

    // down comp
    // *xn_1          = detect_peak(fabsf(input), *xn_1, atk, rel);
    *xn_1          = detect_peak(fabsf(input * boost_G), *xn_1, atk, rel);
    float peak_dB  = xm_fast_gain_to_dB(*xn_1);
    float cut_dB   = soft_knee_compress(peak_dB, -6, ratio_inv, 6);
    cut_dB        -= peak_dB;
    float cut_G    = xm_fast_dB_to_gain(cut_dB);

    float output = input * boost_G * cut_G;
    // float output = input * cut_G;
    return output;
}
