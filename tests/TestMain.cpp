//  Transist4Test — DAW を開かずに挙動を確かめるオフラインドライバ
//
//   ./Transist4Test [出力フォルダ]
//     クロスオーバーの平坦さ・帯域分離・持続音での歪み・トランジェントの効き・CPU を測り、
//     Vorso のドラムループを処理した A/B ファイルと UI のスナップショットを出力フォルダに書く

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "LabTest.h"

namespace
{
    constexpr int block = 512;
    const char* loopDir = "/Volumes/midy9969nect/Samples/VORSO_COMPONENTS/VORSO_COMPONENTS_sounds/drums/drum_loops";
    const char* loops[] = {
        "VORSO_COMPONENTS_drum_loop_174_hard_dnb_drums.wav",
        "VORSO_COMPONENTS_drum_loop_172_tech_bounce.wav",
        "VORSO_COMPONENTS_drum_loop_124_slight_swing_big_snap.wav",
    };

    void setParam (Transist4AudioProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    std::unique_ptr<Transist4AudioProcessor> makeProc (double sr)
    {
        auto p = std::make_unique<Transist4AudioProcessor>();
        p->setPlayConfigDetails (2, 2, sr, block);
        p->prepareToPlay (sr, block);
        return p;
    }

    void setAllTransient (Transist4AudioProcessor& p, float v)
    {
        for (int b = 0; b < 4; ++b)
            setParam (p, "t" + juce::String (b), v);
    }

    juce::AudioBuffer<float> run (Transist4AudioProcessor& p, const juce::AudioBuffer<float>& input)
    {
        const int n = input.getNumSamples();
        juce::AudioBuffer<float> out (2, n);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (int start = 0; start < n; start += block)
        {
            const int len = juce::jmin (block, n - start);
            buf.setSize (2, len, false, false, true);
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, 0, input, juce::jmin (ch, input.getNumChannels() - 1), start, len);
            p.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, start, buf, ch, 0, len);
        }
        return out;
    }

    double db (double v) { return 20.0 * std::log10 (juce::jmax (v, 1.0e-12)); }

    double rms (const juce::AudioBuffer<float>& b, int start, int len)
    {
        double s = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < start + len; ++i)
                s += (double) b.getSample (ch, i) * b.getSample (ch, i);
        return std::sqrt (s / (len * b.getNumChannels()));
    }

    double peak (const juce::AudioBuffer<float>& b, int start, int len)
    {
        double m = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < start + len; ++i)
                m = juce::jmax (m, (double) std::abs (b.getSample (ch, i)));
        return m;
    }

    juce::AudioBuffer<float> sine (double sr, double f, double seconds, float amp)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / sr);
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        return b;
    }

    juce::AudioBuffer<float> noise (int n, float amp, int seed = 1)
    {
        juce::Random r (seed);
        juce::AudioBuffer<float> b (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, amp * (r.nextFloat() * 2.0f - 1.0f));
        return b;
    }

    // 1: 無処理 (transient 0, makeup 0) で 4 バンドの和がオールパスになっているか
    void testFlatness()
    {
        std::printf ("[1] crossover flatness (all bands neutral)\n");
        const double sr = 48000.0;
        const float freqSets[2][3] = { { 125, 450, 3500 }, { 60, 1000, 8000 } };
        const char* slopeNames[3] = { "24", "12", " 6" };

        for (auto& fs : freqSets)
            for (int slope = 0; slope < 3; ++slope)
            {
                auto p = makeProc (sr);
                setParam (*p, "xo1", fs[0]); setParam (*p, "xo2", fs[1]); setParam (*p, "xo3", fs[2]);
                setParam (*p, "slope", (float) slope);
                p->prepareToPlay (sr, block);   // スムージングを飛ばす

                constexpr int order = 16, N = 1 << order;
                juce::AudioBuffer<float> in (2, N);
                in.clear();
                in.setSample (0, 0, 1.0f);
                in.setSample (1, 0, 1.0f);
                auto out = run (*p, in);

                std::vector<float> fftData (2 * N, 0.0f);
                for (int i = 0; i < N; ++i) fftData[(size_t) i] = out.getSample (0, i);
                juce::dsp::FFT fft (order);
                fft.performFrequencyOnlyForwardTransform (fftData.data());

                double maxDev = 0.0;
                for (int k = 1; k < N / 2; ++k)
                {
                    const double f = k * sr / N;
                    if (f < 20.0 || f > 20000.0) continue;
                    maxDev = juce::jmax (maxDev, std::abs (db (fftData[(size_t) k])));
                }

                // delta モードの残り (= 処理後 - 処理前)。無処理なら 0 のはず
                auto p2 = makeProc (sr);
                setParam (*p2, "slope", (float) slope);
                setParam (*p2, "delta", 1.0f);
                p2->prepareToPlay (sr, block);
                auto nz = noise ((int) sr, 0.3f);
                auto d = run (*p2, nz);

                std::printf ("  xo %5.0f/%5.0f/%5.0f  slope %s : max |mag| dev %.4f dB, delta residual peak %.1f dBFS\n",
                             fs[0], fs[1], fs[2], slopeNames[slope], maxDev, db (peak (d, 0, d.getNumSamples())));
            }
    }

    // 2: 帯域分離。各バンドをソロにして、正弦波がどのバンドに入るか
    void testIsolation()
    {
        std::printf ("\n[2] band isolation, slope 24 (rows = sine freq, cols = soloed band low..high, dB)\n");
        const double sr = 48000.0;
        const double freqs[] = { 50, 250, 1500, 8000 };
        for (double f : freqs)
        {
            std::printf ("  %6.0f Hz :", f);
            auto in = sine (sr, f, 1.0, 0.25f);
            for (int b = 0; b < 4; ++b)
            {
                auto p = makeProc (sr);
                setParam (*p, "solo" + juce::String (b), 1.0f);
                p->prepareToPlay (sr, block);
                auto out = run (*p, in);
                const int n = in.getNumSamples();
                std::printf (" %7.1f", db (rms (out, n / 2, n / 2) / rms (in, n / 2, n / 2)));
            }
            std::printf ("\n");
        }
    }

    // 3: 持続音で余計な揺れ・歪みが出ないか (transient ±45)
    void testSteady()
    {
        std::printf ("\n[3] steady signals, all bands at ±45 (level change / residual vs clean allpassed signal)\n");
        const double sr = 48000.0;
        const double freqs[] = { 40, 100, 300, 1000, 5000, 0 };   // 0 = white noise
        for (int slope : { 0, 2 })
            for (float T : { 45.0f, -45.0f })
                for (double f : freqs)
                {
                    auto in = f > 0 ? sine (sr, f, 6.0, 0.25f) : noise ((int) (sr * 6.0), 0.25f, 7);
                    auto pRef = makeProc (sr);
                    setParam (*pRef, "slope", (float) slope);
                    pRef->prepareToPlay (sr, block);
                    auto ref = run (*pRef, in);
                    auto p = makeProc (sr);
                    setParam (*p, "slope", (float) slope);
                    setAllTransient (*p, T);
                    p->prepareToPlay (sr, block);
                    auto out = run (*p, in);

                    // 50 ms ごとに比 k を当てはめ、区間内の残差 (= 歪み・揺れ) と、区間どうしの k の差 (= ゆっくりした音量の動き) を分ける
                    const int seg = (int) (sr * 0.05), start = (int) (sr * 4.5), segs = 30;
                    double resSum = 0.0, sigSum = 0.0, kMin = 1.0e9, kMax = -1.0e9, kAvg = 0.0;
                    for (int sIdx = 0; sIdx < segs; ++sIdx)
                    {
                        const int s0 = start + sIdx * seg;
                        double dot = 0.0, rr = 0.0;
                        for (int i = s0; i < s0 + seg; ++i)
                        {
                            dot += (double) out.getSample (0, i) * ref.getSample (0, i);
                            rr  += (double) ref.getSample (0, i) * ref.getSample (0, i);
                        }
                        const double k = dot / rr;
                        for (int i = s0; i < s0 + seg; ++i)
                        {
                            const double e = out.getSample (0, i) - k * ref.getSample (0, i);
                            resSum += e * e;
                        }
                        sigSum += k * k * rr;
                        kMin = juce::jmin (kMin, k); kMax = juce::jmax (kMax, k); kAvg += k / segs;
                    }
                    std::printf ("  slope %s T %+3.0f  %6s : level %+6.2f dB (drift %.2f dB over 1.5 s), residual %6.1f dB\n",
                                 slope == 0 ? "24" : " 6", T,
                                 f > 0 ? (juce::String ((int) f) + "Hz").toRawUTF8() : "noise",
                                 db (kAvg), db (kMax) - db (kMin),
                                 10.0 * std::log10 (juce::jmax (resSum / sigSum, 1.0e-20)));
                }
    }

    // 4: 1 kHz のトーンバースト (立ち上がり即時、減衰 60 ms) で、頭と余韻がどう変わるか
    void testBurst()
    {
        std::printf ("\n[4] 1 kHz tone bursts (mid band): attack peak (first 3 ms) and tail RMS (50-150 ms) vs neutral\n");
        const double sr = 48000.0;
        const int period = (int) (sr * 0.3), n = period * 12;
        juce::AudioBuffer<float> in (2, n);
        for (int i = 0; i < n; ++i)
        {
            const int k = i % period;
            const float v = 0.5f * (float) (std::exp (-k / (0.06 * sr)) * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * k / sr));
            in.setSample (0, i, v);
            in.setSample (1, i, v);
        }
        auto pRef = makeProc (sr);
        auto ref = run (*pRef, in);
        const int last = n - period;
        const int a0 = last, aLen = (int) (0.003 * sr);
        const int t0 = last + (int) (0.05 * sr), tLen = (int) (0.1 * sr);

        for (float T : { 45.0f, 20.0f, 10.0f, -10.0f, -20.0f, -45.0f })
        {
            auto p = makeProc (sr);
            setParam (*p, "t2", T);
            p->prepareToPlay (sr, block);
            auto out = run (*p, in);
            std::printf ("  T %+3.0f : attack %+6.1f dB, tail %+6.1f dB, auto-gain b = %.3f\n", T,
                         db (peak (out, a0, aLen) / peak (ref, a0, aLen)),
                         db (rms (out, t0, tLen) / rms (ref, t0, tLen)), p->autoGainB (2));
        }
    }

    // 5: 1 サンプルだけのインパルス列 (-40 dBFS のノイズの上に)。インパルスの山と、ノイズの床がどう動くか
    void testImpulse()
    {
        std::printf ("\n[5] single-sample impulses every 100 ms over a -40 dBFS noise bed (all bands)\n");
        const double sr = 48000.0;
        const int n = (int) sr * 4, period = (int) (sr * 0.1);
        auto in = noise (n, 0.01f, 3);
        for (int i = 0; i < n; i += period)
        {
            in.setSample (0, i, in.getSample (0, i) + 0.5f);
            in.setSample (1, i, in.getSample (1, i) + 0.5f);
        }
        auto pRef = makeProc (sr);
        auto ref = run (*pRef, in);
        const int s = n - period;
        const int bedStart = s + (int) (0.03 * sr), bedLen = (int) (0.06 * sr);
        for (float T : { 20.0f, -20.0f })
        {
            auto p = makeProc (sr);
            setAllTransient (*p, T);
            p->prepareToPlay (sr, block);
            auto out = run (*p, in);
            std::printf ("  T %+3.0f : impulse peak %+6.1f dB, noise bed %+6.1f dB\n", T,
                         db (peak (out, s, 64) / peak (ref, s, 64)),
                         db (rms (out, bedStart, bedLen) / rms (ref, bedStart, bedLen)));
        }
    }

    void testCpu()
    {
        std::printf ("\n[6] CPU (10 s stereo noise @48k, all bands +20)\n");
        auto p = makeProc (48000.0);
        setAllTransient (*p, 20.0f);
        auto in = noise (480000, 0.25f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto out = run (*p, in);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("  %.0f ms for 10 s audio (%.2f%% of realtime)\n", ms, ms / 100.0);
    }

    juce::AudioBuffer<float> readWav (const juce::File& f, double& sr)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr) return {};
        sr = r->sampleRate;
        juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
        r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        return b;
    }

    void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
        if (w != nullptr)
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    struct Preset { const char* name; float t[4]; float mk[4]; };
    const Preset presets[] = {
        { "punch", { 18, 18, 18, 18 }, { 0, 0, 0, 0 } },
        { "soft",  { -18, -18, -18, -18 }, { 0, 0, 0, 0 } },
        // 効果がはっきり出る設定 (low, low-mid, mid, high)
        { "showcase", { -34.0f, 45.0f, 43.4f, -6.3f }, { 0, 0, 0, 2.4f } },
    };

    void applyPreset (Transist4AudioProcessor& p, const Preset& pr)
    {
        for (int b = 0; b < 4; ++b)
        {
            setParam (p, "t" + juce::String (b), pr.t[b]);
            setParam (p, "mk" + juce::String (b), pr.mk[b]);
        }
    }

    // A/B ファイル: 原音 2 周 → 0.4 秒の無音 → 処理後 2 周 (処理は 1 周目を捨てて、オートゲインが落ち着いてから)
    void renderLoops (const juce::File& outDir)
    {
        std::printf ("\n[7] Vorso drum loops -> %s\n", outDir.getFullPathName().toRawUTF8());
        for (auto* name : loops)
        {
            double sr = 44100.0;
            auto src = readWav (juce::File (loopDir).getChildFile (name), sr);
            if (src.getNumSamples() == 0) { std::printf ("  cannot read %s\n", name); continue; }
            const int L = src.getNumSamples();

            juce::AudioBuffer<float> in3 (2, L * 3);
            for (int k = 0; k < 3; ++k)
                for (int ch = 0; ch < 2; ++ch)
                    in3.copyFrom (ch, k * L, src, ch, 0, L);

            juce::String shortName = juce::String (name).fromFirstOccurrenceOf ("drum_loop_", false, false).upToLastOccurrenceOf (".wav", false, false);

            for (auto& pr : presets)
            {
                auto p = makeProc (sr);
                applyPreset (*p, pr);
                p->prepareToPlay (sr, block);
                auto out = run (*p, in3);

                const int gap = (int) (0.4 * sr);
                juce::AudioBuffer<float> ab (2, L * 4 + gap);
                ab.clear();
                for (int ch = 0; ch < 2; ++ch)
                {
                    ab.copyFrom (ch, 0, in3, ch, 0, L * 2);
                    ab.copyFrom (ch, L * 2 + gap, out, ch, L, L * 2);
                }

                const double inPk = peak (in3, L, L * 2), outPk = peak (out, L, L * 2);
                const double inR = rms (in3, L, L * 2), outR = rms (out, L, L * 2);

                // 試聴用: 全体のピークが -1 dBFS を超えるときだけ、A/B 両方を同じだけ下げる
                const double abPk = peak (ab, 0, ab.getNumSamples());
                const double trim = abPk > 0.891 ? 0.891 / abPk : 1.0;
                ab.applyGain ((float) trim);

                auto f = outDir.getChildFile (shortName + "__" + pr.name + ".wav");
                writeWav (f, ab, sr);
                std::printf ("  %-32s %-9s peak %+6.1f -> %+6.1f dBFS, rms %+6.1f -> %+6.1f dBFS, trim %.1f dB, b = %.2f/%.2f/%.2f/%.2f\n",
                             shortName.toRawUTF8(), pr.name, db (inPk), db (outPk), db (inR), db (outR), db (trim),
                             p->autoGainB (0), p->autoGainB (1), p->autoGainB (2), p->autoGainB (3));
            }
        }
    }

    // 任意の設定で A/B ファイルを書く:  Transist4Test render 入力.wav 出力.wav id=値 ...
    int renderCustom (int argc, char* argv[])
    {
        double sr = 44100.0;
        auto src = readWav (juce::File (argv[2]), sr);
        if (src.getNumSamples() == 0) { std::printf ("cannot read %s\n", argv[2]); return 1; }
        const int L = src.getNumSamples();
        juce::AudioBuffer<float> in3 (2, L * 3);
        for (int k = 0; k < 3; ++k)
            for (int ch = 0; ch < 2; ++ch)
                in3.copyFrom (ch, k * L, src, ch, 0, L);

        auto p = makeProc (sr);
        for (int i = 4; i < argc; ++i)
        {
            const juce::String kv (argv[i]);
            const auto id = kv.upToFirstOccurrenceOf ("=", false, false);
            if (p->apvts.getParameter (id) == nullptr) { std::printf ("unknown parameter %s\n", id.toRawUTF8()); return 1; }
            setParam (*p, id, kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
        }
        p->prepareToPlay (sr, block);
        auto out = run (*p, in3);

        const int gap = (int) (0.4 * sr);
        juce::AudioBuffer<float> ab (2, L * 4 + gap);
        ab.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            ab.copyFrom (ch, 0, in3, ch, 0, L * 2);
            ab.copyFrom (ch, L * 2 + gap, out, ch, L, L * 2);
        }
        const double abPk = peak (ab, 0, ab.getNumSamples());
        const double trim = abPk > 0.891 ? 0.891 / abPk : 1.0;
        ab.applyGain ((float) trim);
        writeWav (juce::File (argv[3]), ab, sr);
        std::printf ("peak %+6.1f -> %+6.1f dBFS, rms %+6.1f -> %+6.1f dBFS, trim %.1f dB\n",
                     db (peak (in3, L, L * 2)), db (peak (out, L, L * 2)),
                     db (rms (in3, L, L * 2)), db (rms (out, L, L * 2)), db (trim));
        return 0;
    }

    void snapshotUi (const juce::File& outDir)
    {
        double sr = 44100.0;
        auto src = readWav (juce::File (loopDir).getChildFile (loops[0]), sr);
        auto p = makeProc (sr);
        applyPreset (*p, presets[2]);
        p->prepareToPlay (sr, block);
        if (src.getNumSamples() > 0)
        {
            juce::AudioBuffer<float> in (2, src.getNumSamples() * 2);
            for (int k = 0; k < 2; ++k)
                for (int ch = 0; ch < 2; ++ch)
                    in.copyFrom (ch, k * src.getNumSamples(), src, ch, 0, src.getNumSamples());
            run (*p, in);
        }

        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setVisible (true);
        if (auto* e = dynamic_cast<Transist4AudioProcessorEditor*> (ed.get()))
            e->refreshNow();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);

        // クロスオーバーの数字を上下ドラッグで動かせるか (擬似マウスイベント)
        {
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            int k = 0;
            for (auto* c : ed->getChildren())
                if (auto* sl = dynamic_cast<juce::Slider*> (c))
                    if (sl->getProperties()["kind"].toString() == "number")
                    {
                        const auto id = "xo" + juce::String (++k);
                        auto* param = p->apvts.getParameter (id);
                        const float before = param->convertFrom0to1 (param->getValue());
                        const auto centre = sl->getLocalBounds().getCentre().toFloat();
                        auto ev = [&] (juce::Point<float> pos, bool dragged)
                        {
                            return juce::MouseEvent (src, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, sl, sl, juce::Time::getCurrentTime(),
                                                     centre, juce::Time::getCurrentTime(), 1, dragged);
                        };
                        sl->mouseDown (ev (centre, false));
                        sl->mouseDrag (ev (centre.translated (0.0f, -30.0f), true));
                        sl->mouseUp (ev (centre.translated (0.0f, -30.0f), true));
                        const float up = param->convertFrom0to1 (param->getValue());
                        sl->mouseDown (ev (centre, false));
                        sl->mouseDrag (ev (centre.translated (0.0f, 60.0f), true));
                        sl->mouseUp (ev (centre.translated (0.0f, 60.0f), true));
                        const float down = param->convertFrom0to1 (param->getValue());
                        std::printf ("    drag %s: %.0f Hz -> up 30px %.0f Hz -> down 60px %.0f Hz\n", id.toRawUTF8(), before, up, down);
                    }
        }
        auto f = outDir.getChildFile ("transist4_ui.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("\n[8] UI snapshot -> %s\n", f.getFullPathName().toRawUTF8());
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 4 && juce::String (argv[1]) == "render")
        return renderCustom (argc, argv);
    if (argc >= 2 && juce::String (argv[1]) == "robust")
    {
        // 耐久テスト (PluginLab/juce/common/LabRobust.h)。効果がはっきり出る設定で
        lab::RobustConfig cfg;
        cfg.setup = [] (juce::AudioProcessor& p)
        {
            lab::setParam (p, "t0", -34.0f); lab::setParam (p, "t1", 45.0f);
            lab::setParam (p, "t2", 43.4f);  lab::setParam (p, "t3", -6.3f);
            lab::setParam (p, "mk3", 2.4f);
        };
        return lab::robustMain<Transist4AudioProcessor> (cfg);
    }

    juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("renders");
    outDir.createDirectory();

    testFlatness();
    testIsolation();
    testSteady();
    testBurst();
    testImpulse();
    testCpu();
    renderLoops (outDir);
    snapshotUi (outDir);
    return 0;
}
