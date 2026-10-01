#pragma once

// 4バンドのクロスオーバー。
//
// どのスロープでも「4バンドを足すとオールパスになる」ように組む。
// 各分割点では LP を作り、HP 側は「そのスロープの足し戻しと同じオールパス − LP」で作る。
//   24 dB/oct: LR4。LP = BW2 LP の2段、足し戻し = 2次オールパス (Q = 1/√2)
//   12 dB/oct: LR2。LP = 1次 LP の2段、足し戻し = 1次オールパス (HP 側は逆相になる)
//    6 dB/oct: 1次。LP + HP = 1 なので補償不要
// 双一次変換は有理式の恒等式を保つので、デジタルでも足し戻しは厳密にオールパスになる。
//
// 木構造: f2 で Low/High に分け、Low 側に f3 の、High 側に f1 のオールパスを掛けて位相を揃えてから
// Low を f1、High を f3 で分ける。全体 = AP(f1)·AP(f2)·AP(f3)。レイテンシは 0。

#include <array>
#include <cmath>

namespace tq
{
    constexpr int maxChannels = 2;
    constexpr int numBands = 4;
    constexpr double pi = 3.14159265358979323846;

    enum Slope { slope24 = 0, slope12 = 1, slope6 = 2 };

    struct CutCoefs
    {
        // 1次 TPT: G = g / (1 + g)
        float G = 0.0f;
        // SVF (Simper): k = 1/Q
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.41421356f;

        void set (double freq, double sampleRate)
        {
            const double g = std::tan (pi * freq / sampleRate);
            G = (float) (g / (1.0 + g));
            const double kk = std::sqrt (2.0);
            const double A1 = 1.0 / (1.0 + g * (g + kk));
            a1 = (float) A1;
            a2 = (float) (g * A1);
            a3 = (float) (g * g * A1);
            k  = (float) kk;
        }
    };

    struct OnePole
    {
        float s = 0.0f;
        void reset() noexcept { s = 0.0f; }
        inline float lp (float x, float G) noexcept
        {
            const float v = (x - s) * G;
            const float y = v + s;
            s = y + v;
            return y;
        }
    };

    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        void reset() noexcept { ic1 = ic2 = 0.0f; }
        // lp と bp を返す
        inline void tick (float x, const CutCoefs& c, float& lp, float& bp) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = c.a1 * ic1 + c.a2 * v3;
            const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2;
            bp = v1;
        }
    };

    // 1つの分割点
    struct Split
    {
        Svf lpA, lpB, ap;
        OnePole p1, p2, pap;

        void reset() noexcept
        {
            lpA.reset(); lpB.reset(); ap.reset();
            p1.reset(); p2.reset(); pap.reset();
        }

        inline void process (float x, const CutCoefs& c, int slope, float& lo, float& hi) noexcept
        {
            if (slope == slope24)
            {
                float l1, b1, l2, b2, la, ba;
                lpA.tick (x, c, l1, b1);
                lpB.tick (l1, c, l2, b2);
                ap.tick (x, c, la, ba);
                const float all = x - 2.0f * c.k * ba;
                lo = l2;
                hi = all - l2;
            }
            else if (slope == slope12)
            {
                const float l = p2.lp (p1.lp (x, c.G), c.G);
                const float all = 2.0f * pap.lp (x, c.G) - x;
                lo = l;
                hi = all - l;
            }
            else
            {
                const float l = p1.lp (x, c.G);
                lo = l;
                hi = x - l;
            }
        }

        // このスロープでの足し戻しと同じオールパスだけを掛ける (位相補償用)
        inline float allpass (float x, const CutCoefs& c, int slope) noexcept
        {
            if (slope == slope24)
            {
                float la, ba;
                ap.tick (x, c, la, ba);
                return x - 2.0f * c.k * ba;
            }
            if (slope == slope12)
                return 2.0f * pap.lp (x, c.G) - x;
            return x;
        }
    };

    struct Crossover4
    {
        std::array<Split, maxChannels> sA, sB, sC, compLow, compHigh;

        void reset() noexcept
        {
            for (int ch = 0; ch < maxChannels; ++ch)
            {
                sA[ch].reset(); sB[ch].reset(); sC[ch].reset();
                compLow[ch].reset(); compHigh[ch].reset();
            }
        }

        // c[0..2] = f1 < f2 < f3。band[0] = low … band[3] = high
        inline void process (int ch, float x, const std::array<CutCoefs, 3>& c, int slope, float* band) noexcept
        {
            float L, H;
            sA[ch].process (x, c[1], slope, L, H);
            L = compLow[ch].allpass (L, c[2], slope);
            H = compHigh[ch].allpass (H, c[0], slope);
            sB[ch].process (L, c[0], slope, band[0], band[1]);
            sC[ch].process (H, c[2], slope, band[2], band[3]);
        }
    };
}
