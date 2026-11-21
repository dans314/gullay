// Gullay: the Tape memory mode.
//
// The magnetic hysteresis model here is adapted from CHOW Tape Model by Jatin Chowdhury
// (https://github.com/jatinchowdhury18/AnalogTapeModel, GPL v3): HysteresisOps.h and
// HysteresisProcessing.{h,cpp}, kept unchanged in libs/chowtape with the project's LICENSE.
// The model is described in J. Chowdhury, "Real-time Physical Modelling for Analog Tape Machines",
// DAFx 2019.
//
// What changed for Gullay: the scalar (non-SIMD) path only, the RK2 solver only, no JUCE; the
// parameter cooking, input clip and output make-up follow HysteresisProcessor.cpp; Gullay adds an
// input gain tied to Drive (taken back off after). Gullay runs it
// at twice the sample rate (linear in, averaged out) and adds a one-pole DC blocker after it.
// Real-time safe: no allocation, no locks.
#pragma once
#include <algorithm>
#include <cmath>

namespace gytape
{
struct HysteresisState
{
    double M_s = 1.0;
    double a = M_s / 4.0;
    static constexpr double alpha = 1.6e-3;
    double k = 0.47875;
    double c = 1.7e-1;
    double nc = 1 - c;
    double M_s_oa = M_s / a;
    double M_s_oa_talpha = alpha * M_s / a;
    double M_s_oa_tc = c * M_s / a;
    double M_s_oa_tc_talpha = alpha * c * M_s / a;
    double M_s_oaSq_tc_talpha = alpha * c * M_s / (a * a);
    double M_s_oaSq_tc_talphaSq = alpha * alpha * c * M_s / (a * a);
    double Q = 0, M_diff = 0, L_prime = 0, kap1 = 0, f1Denom = 0, f1 = 0, f2 = 0, f3 = 0;
    double coth = 0.0;
    bool nearZero = false;
    double oneOverQ = 0, oneOverQSq = 0, oneOverQCubed = 0, cothSq = 0, oneOverF3 = 0, oneOverF1Denom = 0;
};

constexpr double ONE_THIRD = 1.0 / 3.0;
constexpr inline int sign(double x) { return int(x > 0.0) - int(x < 0.0); }

// Langevin function and its derivative
inline double langevin(const HysteresisState &hp) noexcept { return !hp.nearZero ? hp.coth - hp.oneOverQ : hp.Q * ONE_THIRD; }
inline double langevinD(const HysteresisState &hp) noexcept { return !hp.nearZero ? hp.oneOverQSq - hp.cothSq + 1.0 : ONE_THIRD; }

// derivative by alpha transform
inline double deriv(double x_n, double x_n1, double x_d_n1, double T) noexcept
{
    const double dAlpha = 0.75;
    return ((1.0 + dAlpha) / T) * (x_n - x_n1) - dAlpha * x_d_n1;
}

// hysteresis function dM/dt
inline double hysteresisFunc(double M, double H, double H_d, HysteresisState &hp) noexcept
{
    hp.Q = (H + M * HysteresisState::alpha) * (1.0 / hp.a);
    hp.oneOverQ = 1.0 / hp.Q;
    hp.oneOverQSq = hp.oneOverQ * hp.oneOverQ;
    hp.oneOverQCubed = hp.oneOverQ * hp.oneOverQSq;
    hp.coth = 1.0 / std::tanh(hp.Q);
    hp.nearZero = hp.Q < 0.001 && hp.Q > -0.001;
    hp.cothSq = hp.coth * hp.coth;
    hp.M_diff = langevin(hp) * hp.M_s - M;
    const double delta = (double)((H_d >= 0.0) - (H_d < 0.0));
    const double delta_M = (double)(sign(delta) == sign(hp.M_diff));
    hp.kap1 = hp.nc * delta_M;
    hp.L_prime = langevinD(hp);
    hp.f1Denom = (hp.nc * delta) * hp.k - HysteresisState::alpha * hp.M_diff;
    hp.oneOverF1Denom = 1.0 / hp.f1Denom;
    hp.f1 = hp.kap1 * hp.M_diff / hp.f1Denom;
    hp.f2 = hp.L_prime * hp.M_s_oa_tc;
    hp.f3 = 1.0 - (hp.L_prime * hp.M_s_oa_tc_talpha);
    hp.oneOverF3 = 1.0 / hp.f3;
    return H_d * (hp.f1 + hp.f2) * hp.oneOverF3;
}

// one channel of HysteresisProcessing, RK2 solver
struct Hysteresis
{
    double fs = 96000.0, T = 1.0 / 96000.0, upperLim = 20.0;
    double M_n1 = 0.0, H_n1 = 0.0, H_d_n1 = 0.0;
    HysteresisState hp;

    void reset()
    {
        M_n1 = H_n1 = H_d_n1 = 0.0;
        hp.coth = 0.0, hp.nearZero = false;
    }
    void setSampleRate(double sr) { fs = sr, T = 1.0 / fs; }
    void cook(double drive, double width, double sat)
    {
        hp.M_s = 0.5 + 1.5 * (1.0 - sat);
        hp.a = hp.M_s / (0.01 + 6.0 * drive);
        hp.c = std::sqrt(1.0f - width) - 0.01;
        hp.k = 0.47875;
        upperLim = 20.0;
        hp.nc = 1.0 - hp.c;
        hp.M_s_oa = hp.M_s / hp.a;
        hp.M_s_oa_talpha = HysteresisState::alpha * hp.M_s_oa;
        hp.M_s_oa_tc = hp.c * hp.M_s_oa;
        hp.M_s_oa_tc_talpha = HysteresisState::alpha * hp.M_s_oa_tc;
        hp.M_s_oaSq_tc_talpha = hp.M_s_oa_tc_talpha / hp.a;
        hp.M_s_oaSq_tc_talphaSq = HysteresisState::alpha * hp.M_s_oaSq_tc_talpha;
    }
    inline double process(double H) noexcept
    {
        double H_d = deriv(H, H_n1, H_d_n1, T);
        const double k1 = hysteresisFunc(M_n1, H_n1, H_d_n1, hp) * T;
        const double k2 = hysteresisFunc(M_n1 + k1 * 0.5, (H + H_n1) * 0.5, (H_d + H_d_n1) * 0.5, hp) * T;
        double M = M_n1 + k2;
        if (std::isnan(M) || M > upperLim) // instability: start again from rest
            M = 0.0, H_d = 0.0;
        M_n1 = M, H_n1 = H, H_d_n1 = H_d;
        return M;
    }
};
} // namespace gytape

// Gullay's stereo tape: Drive, Saturation and Bias (CHOW's "width" is 1 - bias), at 2x the rate.
// Gullay keeps it level-neutral for quiet signals (a gain table measured once in prepare, off the audio
// thread's hot path), so Tape adds colour and compression rather than changing how long echoes last.
struct GyTape
{
    gytape::Hysteresis h[2];
    double prev[2]{}, dcX[2]{}, dcY[2]{};
    float makeup{1.f}, dcA{0.999f}, preGain{1.f};
    float curDrive{-1}, curSat{-1}, curBias{-1};
    static constexpr int nT = 9;
    float gainTab[nT][nT]{}; // quiet-signal gain at Drive 1, by Saturation and Bias
    bool tabled{false};

    static float biasOf(float knob) { return 0.15f + 0.85f * std::clamp(knob, 0.f, 1.f); } // below ~15 % the model goes nearly silent
    void prepare(double sr)
    {
        for (auto &x : h)
            x.setSampleRate(sr * 2.0), x.reset();
        dcA = (float)std::exp(-2.0 * 3.14159265358979 * 12.0 / sr);
        if (!tabled)
        {
            // measure: a quiet 300 Hz sine through the bare model at Drive 1, for each Saturation and Bias
            for (int i = 0; i < nT; ++i)
                for (int j = 0; j < nT; ++j)
                {
                    gytape::Hysteresis m;
                    m.setSampleRate(96000.0);
                    m.cook(1.0, 1.0 - biasOf((float)j / (nT - 1)), (double)i / (nT - 1));
                    double s1 = 0, s2 = 0;
                    for (int n = 0; n < 1600; ++n)
                    {
                        double x = 0.02 * std::sin(2 * 3.14159265358979 * 300.0 * n / 96000.0), y = m.process(x);
                        if (n >= 800)
                            s1 += y * y, s2 += x * x;
                    }
                    gainTab[i][j] = (float)std::max(1e-3, std::sqrt(s1 / std::max(1e-12, s2)));
                }
            tabled = true;
        }
        reset();
        curDrive = -1;
    }
    void reset()
    {
        for (auto &x : h)
            x.reset();
        prev[0] = prev[1] = dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0;
    }
    void set(float drive, float sat, float bias)
    {
        drive = std::clamp(drive, 0.f, 1.f), sat = std::clamp(sat, 0.f, 1.f), bias = std::clamp(bias, 0.f, 1.f);
        if (drive == curDrive && sat == curSat && bias == curBias)
            return;
        curDrive = drive, curSat = sat, curBias = bias;
        const double width = 1.0 - biasOf(bias); // as HysteresisProcessor: setWidth (1 - widthParam)
        const double d = 0.05 + 0.95 * drive;     // the model's drive (never quite 0, where it passes nothing)
        for (auto &x : h)
            x.cook(d, width, sat);
        // the gain at this Saturation and Bias, from the table; the model's gain grows with its drive
        float fi = sat * (nT - 1), fj = bias * (nT - 1);
        int i = std::min((int)fi, nT - 2), j = std::min((int)fj, nT - 2);
        float u = fi - i, v = fj - j;
        float g = (1 - u) * (1 - v) * gainTab[i][j] + u * (1 - v) * gainTab[i + 1][j] + (1 - u) * v * gainTab[i][j + 1] + u * v * gainTab[i + 1][j + 1];
        // drive the tape harder with Drive (v0.8: up to +18 dB in, was +12), and take the quiet-signal gain back to 1
        preGain = std::exp2(3.f * drive);
        makeup = 1.f / std::max(1e-3f, g * (float)d * preGain);
    }
    inline void tick(float &l, float &r) noexcept
    {
        float *io[2] = {&l, &r};
        for (int c = 0; c < 2; ++c)
        {
            double x = std::clamp((double)*io[c] * preGain, -8.0, 8.0); // RK2's clip level
            double y = 0.5 * (h[c].process(0.5 * (prev[c] + x)) + h[c].process(x));
            prev[c] = x;
            y *= makeup;
            double o = y - dcX[c] + dcA * dcY[c]; // DC blocker
            dcX[c] = y, dcY[c] = o;
            *io[c] = (float)o;
        }
    }
};
