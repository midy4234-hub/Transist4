#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // バンドごとの slow 追従の時定数 (= トランジェントとみなす長さの目安)
    constexpr double bandTau[Transist4AudioProcessor::numBands] = { 0.025, 0.015, 0.008, 0.004 };
    constexpr double lowBandFloorHz = 40.0;   // low バンドの下端とみなす周波数
    constexpr double minWindowPeriods = 1.5;  // 区間最大の最小幅 = 下端の周期の何倍か

    juce::String bandId (const char* prefix, int b) { return juce::String (prefix) + juce::String (b); }

    juce::String dbText (float v, int)   { return juce::String::formatted ("%+.1f dB", (double) v); }
    juce::String hzText (float v, int)
    {
        return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz"
                           : juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz";
    }
    juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }

    float dbToGain (float db) { return std::exp (db * 0.11512925f); }   // ln(10)/20
}

Transist4AudioProcessor::Transist4AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Transist4", createLayout())
{
    for (int b = 0; b < numBands; ++b)
    {
        pTransient[b]  = apvts.getRawParameterValue (bandId ("t", b));
        pMakeup[b]     = apvts.getRawParameterValue (bandId ("mk", b));
        pMute[b]       = apvts.getRawParameterValue (bandId ("mute", b));
        pSolo[b]       = apvts.getRawParameterValue (bandId ("solo", b));
        pBandBypass[b] = apvts.getRawParameterValue (bandId ("byp", b));
    }
    for (int i = 0; i < 3; ++i)
    {
        pXo[i] = apvts.getRawParameterValue (bandId ("xo", i + 1));
        effectiveXo[(size_t) i].store (pXo[i]->load());
    }
    pSlope  = apvts.getRawParameterValue ("slope");
    pMix    = apvts.getRawParameterValue ("mix");
    pOut    = apvts.getRawParameterValue ("out");
    pDelta  = apvts.getRawParameterValue ("delta");
    pBypass = apvts.getRawParameterValue ("bypass");
}

juce::AudioProcessorValueTreeState::ParameterLayout Transist4AudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const char* names[numBands] = { "Low", "Low-Mid", "Mid", "High" };

    for (int b = 0; b < numBands; ++b)
    {
        const String n (names[b]);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bandId ("t", b), 1 }, n + " Transient",
                                                           NormalisableRange<float> (-45.0f, 45.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bandId ("mk", b), 1 }, n + " Makeup",
                                                           NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bandId ("mute", b), 1 }, n + " Mute", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bandId ("solo", b), 1 }, n + " Solo", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bandId ("byp", b), 1 }, n + " Bypass", false));
    }

    auto freqRange = [] (float lo, float hi)
    {
        NormalisableRange<float> r (lo, hi, 1.0f);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    };
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "xo1", 1 }, "Crossover Low",
                                                       freqRange (20.0f, 1000.0f), 125.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "xo2", 1 }, "Crossover Mid",
                                                       freqRange (100.0f, 8000.0f), 450.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "xo3", 1 }, "Crossover High",
                                                       freqRange (500.0f, 18000.0f), 3500.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "slope", 1 }, "Slope",
                                                        StringArray { "24 dB/oct", "12 dB/oct", "6 dB/oct" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Dry/Wet",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "delta", 1 }, "Delta", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

juce::AudioProcessorParameter* Transist4AudioProcessor::getBypassParameter() const
{
    return apvts.getParameter ("bypass");
}

bool Transist4AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void Transist4AudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    xover.reset();
    for (int b = 0; b < numBands; ++b)
    {
        detectors[(size_t) b].prepare (sr, bandTau[b]);
        transientDb[(size_t) b].reset (sr, 0.03);
        transientDb[(size_t) b].setCurrentAndTargetValue (pTransient[b]->load());
        makeupGain[(size_t) b].reset (sr, 0.03);
        makeupGain[(size_t) b].setCurrentAndTargetValue (dbToGain (pMakeup[b]->load()));
        gate[(size_t) b].reset (sr, 0.005);
        gate[(size_t) b].setCurrentAndTargetValue (1.0f);
        bypassBand[(size_t) b].reset (sr, 0.005);
        bypassBand[(size_t) b].setCurrentAndTargetValue (pBandBypass[b]->load() > 0.5f ? 1.0f : 0.0f);
    }
    for (int i = 0; i < 3; ++i)
    {
        xoFreq[(size_t) i].reset (sr, 0.05);
        xoFreq[(size_t) i].setCurrentAndTargetValue (pXo[i]->load());
    }
    mix.reset (sr, 0.03);
    mix.setCurrentAndTargetValue (pMix->load() * 0.01f);
    outGain.reset (sr, 0.03);
    outGain.setCurrentAndTargetValue (dbToGain (pOut->load()));
    bypassMix.reset (sr, 0.01);
    bypassMix.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 1.0f : 0.0f);

    curSlope = (int) pSlope->load();
    slopeFade = 1.0f;
    slopeFadeStep = (float) (1.0 / (0.005 * sr));

    deltaMix.reset (sr, 0.01);
    deltaMix.setCurrentAndTargetValue (pDelta->load() > 0.5f ? 1.0f : 0.0f);
    coefCounter = 0;
    gainCounter = 0;

    colSamples = juce::jmax (1, (int) std::round (sr * displaySeconds / displayCols));
    colCount = 0;
    colAcc = {};

    updateCoefs (0);
}

void Transist4AudioProcessor::updateCoefs (int stepSamples)
{
    float f[3];
    for (int i = 0; i < 3; ++i)
        f[i] = stepSamples > 0 ? xoFreq[(size_t) i].skip (stepSamples) : xoFreq[(size_t) i].getCurrentValue();

    // 順序を保つ: f1 < f2 < f3 (1.25 倍以上離す)
    const float nyq = (float) (sr * 0.45);
    f[0] = juce::jmin (f[0], nyq / 1.5625f);
    f[1] = juce::jlimit (f[0] * 1.25f, nyq / 1.25f, f[1]);
    f[2] = juce::jlimit (f[1] * 1.25f, nyq, f[2]);

    for (int i = 0; i < 3; ++i)
    {
        coefs[(size_t) i].set (f[i], sr);
        effectiveXo[(size_t) i].store (f[i]);
    }

    // 区間最大の最小幅は各バンドの下端から。実際の幅は検出器が音の周期に合わせて伸ばす
    detectors[0].setMinWindow ((int) std::ceil (minWindowPeriods * sr / lowBandFloorHz));
    for (int b = 1; b < numBands; ++b)
        detectors[(size_t) b].setMinWindow ((int) std::ceil (minWindowPeriods * sr / f[b - 1]));
}

void Transist4AudioProcessor::pushDisplay (const std::array<float, numBands>& pre, const std::array<float, numBands>& post)
{
    for (int b = 0; b < numBands; ++b)
    {
        auto& c = colAcc.band[(size_t) b];
        if (colCount == 0)
        {
            c.preMin = c.preMax = pre[(size_t) b];
            c.postMin = c.postMax = post[(size_t) b];
        }
        else
        {
            c.preMin  = juce::jmin (c.preMin,  pre[(size_t) b]);
            c.preMax  = juce::jmax (c.preMax,  pre[(size_t) b]);
            c.postMin = juce::jmin (c.postMin, post[(size_t) b]);
            c.postMax = juce::jmax (c.postMax, post[(size_t) b]);
        }
    }

    if (++colCount >= colSamples)
    {
        colCount = 0;
        int s1, n1, s2, n2;
        displayFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0)
        {
            displayData[(size_t) s1] = colAcc;
            displayFifo.finishedWrite (1);
        }
    }
}

void Transist4AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    for (int ch = numIn; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);
    const int numCh = juce::jmin (numIn, tq::maxChannels);
    if (numCh == 0 || n == 0)
        return;

    // パラメータ → 目標値
    bool anySolo = false;
    for (int b = 0; b < numBands; ++b)
        anySolo = anySolo || pSolo[b]->load() > 0.5f;

    for (int b = 0; b < numBands; ++b)
    {
        transientDb[(size_t) b].setTargetValue (pTransient[b]->load());
        makeupGain[(size_t) b].setTargetValue (dbToGain (pMakeup[b]->load()));
        const bool audible = pMute[b]->load() < 0.5f && (! anySolo || pSolo[b]->load() > 0.5f);
        gate[(size_t) b].setTargetValue (audible ? 1.0f : 0.0f);
        bypassBand[(size_t) b].setTargetValue (pBandBypass[b]->load() > 0.5f ? 1.0f : 0.0f);
    }
    for (int i = 0; i < 3; ++i)
        xoFreq[(size_t) i].setTargetValue (pXo[i]->load());
    mix.setTargetValue (pMix->load() * 0.01f);
    outGain.setTargetValue (dbToGain (pOut->load()));
    bypassMix.setTargetValue (pBypass->load() > 0.5f ? 1.0f : 0.0f);
    const int wantSlope = (int) pSlope->load();

    std::array<float, numBands> bAuto {};
    for (int b = 0; b < numBands; ++b)
        bAuto[(size_t) b] = detectors[(size_t) b].autoGainB();
    deltaMix.setTargetValue (pDelta->load() > 0.5f ? 1.0f : 0.0f);

    float* data[tq::maxChannels] = { buffer.getWritePointer (0), buffer.getWritePointer (numCh > 1 ? 1 : 0) };
    constexpr int coefStep = 16;
    float blockInPeak = 0.0f, blockOutPeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        // 係数は 16 サンプルごと、オートゲインは 64 サンプルごと。カウンターはブロックをまたいで続ける
        // (ブロックの頭で数え直すと、ブロックサイズで音が変わる。lab.py robust で発覚)
        if (++coefCounter >= coefStep)
        {
            coefCounter = 0;
            updateCoefs (coefStep);
        }
        if (++gainCounter >= autoGainStep)
        {
            gainCounter = 0;
            for (int b = 0; b < numBands; ++b)
            {
                detectors[(size_t) b].endBlock (autoGainStep, transientDb[(size_t) b].getCurrentValue());
                bAuto[(size_t) b] = detectors[(size_t) b].autoGainB();
            }
        }

        // スロープ切り替え: 5 ms で絞ってから状態を消して切り替え、また開く
        if (wantSlope != curSlope)
        {
            slopeFade -= slopeFadeStep;
            if (slopeFade <= 0.0f)
            {
                slopeFade = 0.0f;
                curSlope = wantSlope;
                xover.reset();
            }
        }
        else if (slopeFade < 1.0f)
        {
            slopeFade = juce::jmin (1.0f, slopeFade + slopeFadeStep);
        }

        float in[tq::maxChannels], band[tq::maxChannels][numBands];
        for (int ch = 0; ch < numCh; ++ch)
        {
            in[ch] = data[ch][i];
            xover.process (ch, in[ch], coefs, curSlope, band[ch]);
        }
        if (numCh == 1)
        {
            in[1] = in[0];
            for (int b = 0; b < numBands; ++b)
                band[1][b] = band[0][b];
        }

        float wet[2] = { 0.0f, 0.0f }, dry[2] = { 0.0f, 0.0f };
        std::array<float, numBands> pre {}, post {};

        for (int b = 0; b < numBands; ++b)
        {
            const float l = band[0][b], r = band[1][b];
            const float a = juce::jmax (std::abs (l), std::abs (r));
            const float e = 0.5f * (l * l + r * r);

            const float T  = transientDb[(size_t) b].getNextValue();
            const float mk = makeupGain[(size_t) b].getNextValue();
            const float gt = gate[(size_t) b].getNextValue();

            const float t = detectors[(size_t) b].process (a, e, 0.5f * (l + r));
            const float gDb = juce::jlimit (-60.0f, 60.0f, T * (t - bAuto[(size_t) b]));
            // バンドのバイパスも 5 ms でつなぐ (瞬時に切り替えるとクリック。lab.py robust で発覚)
            const float bb = bypassBand[(size_t) b].getNextValue();
            const float g = dbToGain (gDb) * mk * (1.0f - bb) + bb;

            dry[0] += l * gt;
            dry[1] += r * gt;
            wet[0] += l * g * gt;
            wet[1] += r * g * gt;

            pre[(size_t) b]  = 0.5f * (l + r);
            post[(size_t) b] = pre[(size_t) b] * g * gt;
        }

        const float m  = mix.getNextValue();
        const float og = outGain.getNextValue() * slopeFade;
        const float deltaNow = deltaMix.getNextValue();
        const float bp = bypassMix.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float diff = wet[ch] - dry[ch];
            const float dm = deltaNow;
            const float y = ((dry[ch] + m * diff) * (1.0f - dm) + m * diff * dm) * og;
            data[ch][i] = y + (in[ch] - y) * bp;
        }

        pushDisplay (pre, post);

        for (int ch = 0; ch < numCh; ++ch)
        {
            blockInPeak  = juce::jmax (blockInPeak,  std::abs (in[ch]));
            blockOutPeak = juce::jmax (blockOutPeak, std::abs (data[ch][i]));
        }
    }

    auto raise = [] (std::atomic<float>& m, float v)
    {
        float cur = m.load();
        while (v > cur && ! m.compare_exchange_weak (cur, v)) {}
    };
    raise (meterIn, blockInPeak);
    raise (meterOut, blockOutPeak);

}

juce::AudioProcessorEditor* Transist4AudioProcessor::createEditor()
{
    return new Transist4AudioProcessorEditor (*this);
}

void Transist4AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void Transist4AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Transist4AudioProcessor();
}
