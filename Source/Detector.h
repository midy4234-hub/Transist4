#pragma once

// 1バンドぶんのトランジェント検出とゲイン計算。
//
// fast: |x| の区間最大。立ち上がりは即座に追う (1サンプルだけのインパルスにもその場で反応する)
//       区間の幅 W は、バンドに実際に入っている音の半周期より長くなるよう自動で決める。
//       短すぎると持続音の周期ごとのうねりをトランジェントと誤認し、音をゲインで揺らしてしまう
//       (隣のバンドから漏れてきた低い音で特に起きる)
//       半周期はゼロ交差の間隔から推定する (最大値を即座に取り、100 ms で緩やかに下げる)
// slow: fast に時定数 tauS で遅れて追いつく。fast が下がるときは即座に合わせる
// t = (fast - slow) / fast ∈ [0, 1] … 鳴り始めで 1、持続・減衰中は 0
//
// ゲイン[dB] = T · (t - b)
//   T: transient つまみ (-45..+45)。アタックと余韻の差が T dB になる「傾き」
//   b: オートゲイン。約 1.5 秒の区間で、処理前後のエネルギーが等しくなる値
//      E[e · 10^(T(t-b)/10)] = E[e]  →  b = (10/T) · log10( E[e·10^(T t/10)] / E[e] )
//      t の分布 (エネルギー重み付きヒストグラム) を持っておけば、T を回した瞬間から正しい b が出る

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace tq
{
    class Detector
    {
    public:
        static constexpr int cap  = 16384;   // 区間最大の最大幅 (192k で 85 ms まで)
        static constexpr int mask = cap - 1;
        static constexpr int histBins = 32;

        void prepare (double sampleRate, double tauSeconds)
        {
            sr = sampleRate;
            alphaS = (float) (1.0 - std::exp (-1.0 / (tauSeconds * sampleRate)));
            estDecay = (float) std::exp (-1.0 / (0.1 * sampleRate));
            maxW = std::min (cap - 2, (int) (0.05 * sampleRate));
            reset();
        }

        void reset() noexcept
        {
            head = tail = 0;
            n = 0;
            slow = 0.0f;
            sign = 1;
            sinceCross = 0;
            halfPeriod = (float) minW;
            hist.fill (0.0);
            blockHist.fill (0.0);
            bAuto = 1.0f;   // T の符号が分かる最初の endBlock までは、持ち上げない側 (T > 0 なら b = 1)
        }

        // バンドの下端から決まる最小の幅
        void setMinWindow (int samples) noexcept { minW = std::clamp (samples, 2, maxW); }

        // トランジェント量 t を返す。a = |x| (ステレオはリンク済み)、e = エネルギー、x = 符号付き (ゼロ交差用)
        inline float process (float a, float e, float x) noexcept
        {
            trackHalfPeriod (x);
            const int W = std::clamp ((int) (1.5f * halfPeriod) + 2, minW, maxW);

            while (tail != head && vals[(size_t) ((tail - 1) & mask)] <= a)
                tail = (tail - 1) & mask;
            vals[(size_t) tail] = a;
            idx[(size_t) tail]  = n;
            tail = (tail + 1) & mask;
            while (idx[(size_t) head] <= n - W)
                head = (head + 1) & mask;
            ++n;

            const float fast = vals[(size_t) head];
            lastFast = fast;
            if (fast > slow) slow += (fast - slow) * alphaS;
            else             slow = fast;

            const float t = (fast - slow) / (fast + 1.0e-7f);
            const int bin = std::min (histBins - 1, (int) (t * histBins));
            blockHist[(size_t) bin] += (double) e;
            return t;
        }

        // ブロックの終わりに呼ぶ。T が現在の transient 値
        void endBlock (int numSamples, float T) noexcept
        {
            const double decay = std::exp (-(double) numSamples / (1.5 * sr));
            double total = 0.0;
            for (int j = 0; j < histBins; ++j)
            {
                hist[(size_t) j] = hist[(size_t) j] * decay + blockHist[(size_t) j];
                blockHist[(size_t) j] = 0.0;
                total += hist[(size_t) j];
            }

            // 履歴が無いうち (起動直後・無音のあと) は、アタックを持ち上げない側から始める
            // (b = 0 のまま T = +45 だと最初のアタックが +45 dB 跳ねた。lab.py robust の極端な入力で発覚)
            if (total < 1.0e-12)
            {
                bAuto = T > 0.0f ? 1.0f : 0.0f;
                return;
            }
            if (std::abs (T) < 0.05f)
                return;

            double weighted = 0.0;
            for (int j = 0; j < histBins; ++j)
            {
                const double tc = (j + 0.5) / histBins;
                weighted += hist[(size_t) j] * std::pow (10.0, (double) T * tc / 10.0);
            }
            const double b = (10.0 / T) * std::log10 (weighted / total);
            bAuto = (float) std::clamp (b, 0.0, 1.0);
        }

        float autoGainB() const noexcept { return bAuto; }

    private:
        inline void trackHalfPeriod (float x) noexcept
        {
            // ヒステリシス付きのゼロ交差。閾値は現在の包絡の 10 %
            const float h = 0.1f * lastFast;
            bool crossed = false;
            if (sign > 0 && x < -h)      { sign = -1; crossed = true; }
            else if (sign < 0 && x > h)  { sign = 1;  crossed = true; }

            if (lastFast > 1.0e-6f)   // 無音のあいだは伸ばさない
                ++sinceCross;

            halfPeriod *= estDecay;
            halfPeriod = std::max (halfPeriod, (float) sinceCross);
            if (crossed)
                sinceCross = 0;
        }

        double sr = 48000.0;
        float alphaS = 0.01f, estDecay = 0.9998f;
        int minW = 64, maxW = 2400;

        std::array<float, cap> vals {};
        std::array<int64_t, cap> idx {};
        int head = 0, tail = 0;
        int64_t n = 0;
        float slow = 0.0f, lastFast = 0.0f;

        int sign = 1, sinceCross = 0;
        float halfPeriod = 64.0f;

        std::array<double, histBins> hist {}, blockHist {};
        float bAuto = 0.0f;
    };
}
