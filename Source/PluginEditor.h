#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>
#include "PluginProcessor.h"

// ChordRes と同じ「Ableton 純正寄り」の見た目
class Transist4LookAndFeel  : public juce::LookAndFeel_V4
{
public:
    Transist4LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool hover, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool hover, bool down) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

class Transist4AudioProcessorEditor  : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit Transist4AudioProcessorEditor (Transist4AudioProcessor&);
    ~Transist4AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // テスト用: タイマーを待たずに FIFO とメーターを読む
    void refreshNow() { timerCallback(); }

private:
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    static constexpr int numBands = Transist4AudioProcessor::numBands;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAttach> attach;
    };

    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<ButtonAttach> attach;
    };

    struct Switch
    {
        std::vector<std::unique_ptr<juce::TextButton>> buttons;
        std::vector<int> values;
        std::unique_ptr<juce::ParameterAttachment> attach;
        void setBounds (juce::Rectangle<int>);
    };

    struct Band
    {
        Knob transient, makeup;
        Toggle mute, solo, bypass;
        juce::Rectangle<int> strip, lcd;
        std::array<Transist4AudioProcessor::Col, Transist4AudioProcessor::displayCols> cols {};
        bool dimmed = false;
    };

    void timerCallback() override;
    void setUpKnob (Knob&, const juce::String& paramId, const juce::String& title, bool bipolar);
    void setUpToggle (Toggle&, const juce::String& paramId, const juce::String& text);
    void setUpSwitch (Switch&, const juce::String& paramId, std::initializer_list<std::pair<const char*, int>> items);
    void paintBand (juce::Graphics&, int band);
    void paintMeters (juce::Graphics&);

    Transist4AudioProcessor& proc;
    Transist4LookAndFeel lnf;

    std::array<Band, numBands> bands;
    int writePos = 0;

    // クロスオーバーは帯の境目に置く数字。上下ドラッグで動かす
    struct Xo
    {
        juce::Slider slider;
        std::unique_ptr<SliderAttach> attach;
        int row = 0;   // 何本目の帯の下か (0 = HIGH の下)
    };

    std::array<Xo, 3> xos;   // index 0 = Low (125 Hz) … 2 = High (3.5 kHz)
    Knob mix, output;
    Toggle delta, bypass;
    Switch slopeSwitch;

    juce::Rectangle<int> masterStrip, slopeArea, meterArea;
    float meterIn = -100.0f, meterOut = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Transist4AudioProcessorEditor)
};
