#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "Crossover.h"
#include "Detector.h"

class Transist4AudioProcessor  : public juce::AudioProcessor
{
public:
    static constexpr int numBands = tq::numBands;   // 0 = low, 1 = low-mid, 2 = mid, 3 = high
    static constexpr int displayCols = 500;          // 波形表示の横幅 (列数)
    static constexpr double displaySeconds = 2.0;    // 波形表示が映す時間

    struct Col { float preMin, preMax, postMin, postMax; };
    struct ColSet { std::array<Col, numBands> band; };

    Transist4AudioProcessor();
    ~Transist4AudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // 波形表示用 FIFO (オーディオスレッドが書き、UI が読む。満杯なら捨てる)
    juce::AbstractFifo displayFifo { 1024 };
    std::vector<ColSet> displayData = std::vector<ColSet> (1024);

    // メーター: 前回 UI が読んでからの最大ピーク。UI は exchange(0) で読む
    std::atomic<float> meterIn { 0.0f }, meterOut { 0.0f };

    // 実効クロスオーバー周波数 (順序の補正後。UI 表示用)
    std::array<std::atomic<float>, 3> effectiveXo;

    // テスト用
    float autoGainB (int band) const { return detectors[(size_t) band].autoGainB(); }

private:
    void updateCoefs (int stepSamples);
    void pushDisplay (const std::array<float, numBands>& pre, const std::array<float, numBands>& post);

    double sr = 48000.0;
    tq::Crossover4 xover;
    std::array<tq::CutCoefs, 3> coefs;
    std::array<tq::Detector, numBands> detectors;

    int curSlope = 0;
    float slopeFade = 1.0f, slopeFadeStep = 0.01f;

    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, 3> xoFreq;
    std::array<juce::SmoothedValue<float>, numBands> transientDb, makeupGain, gate, bypassBand;
    juce::SmoothedValue<float> mix, outGain, bypassMix, deltaMix;
    // 係数とオートゲインの更新間隔。カウンターはブロックをまたいで続ける (ブロックサイズで音が変わらないように)
    static constexpr int autoGainStep = 64;
    int coefCounter = 0, gainCounter = 0;

    // 波形表示の集計
    int colSamples = 192, colCount = 0;
    ColSet colAcc {};

    std::atomic<float>* pTransient[numBands] {};
    std::atomic<float>* pMakeup[numBands] {};
    std::atomic<float>* pMute[numBands] {};
    std::atomic<float>* pSolo[numBands] {};
    std::atomic<float>* pBandBypass[numBands] {};
    std::atomic<float>* pXo[3] {};
    std::atomic<float>* pSlope = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOut = nullptr;
    std::atomic<float>* pDelta = nullptr;
    std::atomic<float>* pBypass = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Transist4AudioProcessor)
};
