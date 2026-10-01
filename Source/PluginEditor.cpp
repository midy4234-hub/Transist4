#include "PluginEditor.h"

namespace
{
    namespace col
    {
        const juce::Colour bg          { 0xff2e2e2e };
        const juce::Colour panel       { 0xff363636 };
        const juce::Colour panelStroke { 0xff404040 };
        const juce::Colour text        { 0xffd2d2d2 };
        const juce::Colour dim         { 0xff8c8c8c };
        const juce::Colour lcd         { 0xff1f1f1f };
        const juce::Colour lcdGrid     { 0xff333333 };
        const juce::Colour wave        { 0xff6ec8ff };
        const juce::Colour accent      { 0xffffa400 };
        const juce::Colour meter       { 0xff8ee06b };
        const juce::Colour over        { 0xffff5a5a };
        const juce::Colour track       { 0xff555555 };
        const juce::Colour switchOff   { 0xff2a2a2a };
        const juce::Colour onText      { 0xff1e1e1e };
    }

    constexpr float meterMinDb = -48.0f, meterMaxDb = 12.0f;

    // index = バンド番号 (0 = low … 3 = high)
    const char* bandTitle[Transist4AudioProcessor::numBands] = { "LOW", "LOW-MID", "MID", "HIGH" };
}

//==============================================================================
Transist4LookAndFeel::Transist4LookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, col::accent.withAlpha (0.4f));
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextEditor::backgroundColourId, col::lcd);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    setColour (juce::CaretComponent::caretColourId, col::accent);
}

void Transist4LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                             float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();

    // 数字だけのつまみ (クロスオーバー): 枠の中に値を書く。上下ドラッグで動く
    if (s.getProperties()["kind"].toString() == "number")
    {
        const bool active = s.isMouseOverOrDragging();
        g.setColour (active ? col::switchOff.brighter (0.15f) : col::switchOff);
        g.fillRoundedRectangle (bounds.reduced (0.5f), 2.0f);
        g.setColour (active ? col::accent : col::panelStroke.brighter (0.1f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 2.0f, 1.0f);
        g.setColour (col::text);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (s.getProperties()["display"].toString(), bounds, juce::Justification::centred);
        return;
    }

    const float r = juce::jmin (16.0f, juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f);
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    // 両側に振れるパラメータは 0 から弧を描く
    float origin = startAngle;
    if ((bool) s.getProperties()["bipolar"])
        origin = startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle);

    juce::Path trackArc;
    trackArc.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (col::track);
    g.strokePath (trackArc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    if (std::abs (angle - origin) > 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (origin, angle), juce::jmax (origin, angle), true);
        g.setColour (col::accent);
        g.strokePath (valueArc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    const juce::Point<float> tip (c.x + r * std::sin (angle), c.y - r * std::cos (angle));
    g.setColour (col::text);
    g.drawLine ({ c, tip }, 1.5f);
}

void Transist4LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                 bool hover, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();

    auto fill = on ? col::accent : col::switchOff;
    if (hover && ! on) fill = fill.brighter (0.15f);
    if (down) fill = fill.darker (0.1f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 2.0f);
    if (! on)
    {
        g.setColour (col::panelStroke.brighter (0.1f));
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
    }
}

void Transist4LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool hover, bool)
{
    const bool on = b.getToggleState();
    g.setColour (on ? col::onText : (hover ? col::text : col::dim));
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

juce::Font Transist4LookAndFeel::getLabelFont (juce::Label&)
{
    return juce::FontOptions (10.5f);
}

juce::Label* Transist4LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (10.5f));
    l->setColour (juce::Label::textColourId, col::text);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return l;
}

//==============================================================================
namespace
{
    // レイアウト (ウィンドウ 900 x 584)
    constexpr int winW = 900, winH = 584;
    constexpr int margin = 16, titleH = 34;
    constexpr int stripH = 88, xoRowH = 28;
    constexpr int nameColW = 74;                 // 帯の左端のバンド名の列
    constexpr int knobCellW = 80, msbW = 28;
    constexpr int controlsW = knobCellW * 2 + 12 + msbW + 10;
}

Transist4AudioProcessorEditor::Transist4AudioProcessorEditor (Transist4AudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    for (int b = 0; b < numBands; ++b)
    {
        auto& band = bands[(size_t) b];
        const auto id = juce::String (b);
        setUpKnob (band.transient, "t" + id, "Transient", true);
        setUpKnob (band.makeup, "mk" + id, "Makeup", true);
        setUpToggle (band.mute, "mute" + id, "M");
        setUpToggle (band.solo, "solo" + id, "S");
        setUpToggle (band.bypass, "byp" + id, "B");
    }

    for (int i = 0; i < 3; ++i)
    {
        auto& x = xos[(size_t) i];
        const auto id = "xo" + juce::String (i + 1);
        x.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        x.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        x.slider.setMouseDragSensitivity (480);
        x.slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        x.slider.getProperties().set ("kind", "number");
        addAndMakeVisible (x.slider);
        x.attach = std::make_unique<SliderAttach> (proc.apvts, id, x.slider);
        if (auto* param = proc.apvts.getParameter (id))
            x.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        x.row = 2 - i;   // High(3.5k) は HIGH の帯の下、Low(125) は LOW-MID の帯の下
    }

    setUpSwitch (slopeSwitch, "slope", { { "24", 0 }, { "12", 1 }, { "6", 2 } });
    setUpKnob (mix, "mix", "Dry/Wet", false);
    setUpKnob (output, "out", "Output", true);
    setUpToggle (delta, "delta", "Delta");
    setUpToggle (bypass, "bypass", "Bypass");

    setSize (winW, winH);
    startTimerHz (30);
}

Transist4AudioProcessorEditor::~Transist4AudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Transist4AudioProcessorEditor::setUpKnob (Knob& k, const juce::String& paramId, const juce::String& t, bool bipolar)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 14);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, col::text);
    k.slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (k.slider);

    k.label.setText (t, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, col::dim);
    addAndMakeVisible (k.label);

    k.attach = std::make_unique<SliderAttach> (proc.apvts, paramId, k.slider);

    // ダブルクリックで初期値に戻す
    if (auto* param = proc.apvts.getParameter (paramId))
        k.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void Transist4AudioProcessorEditor::setUpToggle (Toggle& t, const juce::String& paramId, const juce::String& text)
{
    t.button.setButtonText (text);
    t.button.setClickingTogglesState (true);
    addAndMakeVisible (t.button);
    t.attach = std::make_unique<ButtonAttach> (proc.apvts, paramId, t.button);
}

void Transist4AudioProcessorEditor::setUpSwitch (Switch& sw, const juce::String& paramId,
                                                 std::initializer_list<std::pair<const char*, int>> items)
{
    auto* swPtr = &sw;
    for (auto& [text, value] : items)
    {
        auto b = std::make_unique<juce::TextButton> (text);
        const int v = value;
        b->onClick = [swPtr, v] { swPtr->attach->setValueAsCompleteGesture ((float) v); };
        addAndMakeVisible (*b);
        sw.buttons.push_back (std::move (b));
        sw.values.push_back (value);
    }

    sw.attach = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (paramId),
        [swPtr] (float v)
        {
            for (size_t i = 0; i < swPtr->buttons.size(); ++i)
                swPtr->buttons[i]->setToggleState (juce::roundToInt (v) == swPtr->values[i], juce::dontSendNotification);
        });
    sw.attach->sendInitialUpdate();
}

void Transist4AudioProcessorEditor::Switch::setBounds (juce::Rectangle<int> r)
{
    const int n = (int) buttons.size();
    for (int i = 0; i < n; ++i)
    {
        const int x0 = r.getX() + r.getWidth() * i / n, x1 = r.getX() + r.getWidth() * (i + 1) / n;
        buttons[(size_t) i]->setBounds (x0, r.getY(), x1 - x0, r.getHeight());
    }
}

//==============================================================================
void Transist4AudioProcessorEditor::timerCallback()
{
    const int ready = proc.displayFifo.getNumReady();
    if (ready > 0)
    {
        int s1, n1, s2, n2;
        proc.displayFifo.prepareToRead (ready, s1, n1, s2, n2);
        auto take = [this] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                for (int b = 0; b < numBands; ++b)
                    bands[(size_t) b].cols[(size_t) writePos] = proc.displayData[(size_t) (start + i)].band[(size_t) b];
                writePos = (writePos + 1) % Transist4AudioProcessor::displayCols;
            }
        };
        take (s1, n1);
        take (s2, n2);
        proc.displayFifo.finishedRead (n1 + n2);
    }

    bool anySolo = false;
    for (auto& b : bands)
        anySolo = anySolo || b.solo.button.getToggleState();
    for (auto& b : bands)
        b.dimmed = b.mute.button.getToggleState() || (anySolo && ! b.solo.button.getToggleState());

    // クロスオーバーの表示は順序補正後の実効値
    for (int i = 0; i < 3; ++i)
    {
        const auto* param = proc.apvts.getParameter ("xo" + juce::String (i + 1));
        const float f = proc.effectiveXo[(size_t) i].load();
        auto& sl = xos[(size_t) i].slider;
        const auto text = param->getText (param->convertTo0to1 (f), 16);
        if (sl.getProperties()["display"].toString() != text)
        {
            sl.getProperties().set ("display", text);
            sl.repaint();
        }
    }

    // メーター: 上がるときは即座に、下がるときは 1 フレーム 1.5dB (= 45dB/s)
    auto fall = [] (float& shown, std::atomic<float>& src)
    {
        const float db = juce::Decibels::gainToDecibels (src.exchange (0.0f), -100.0f);
        shown = juce::jmax (db, shown - 1.5f);
    };
    fall (meterIn,  proc.meterIn);
    fall (meterOut, proc.meterOut);

    for (auto& b : bands)
        repaint (b.lcd);
    repaint (meterArea);
}

void Transist4AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    g.setColour (col::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Transist4", margin, 6, 200, 24, juce::Justification::centredLeft);

    auto drawStrip = [&g] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (r.toFloat(), 2.0f);
        g.setColour (col::panelStroke);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 2.0f, 1.0f);
        g.setColour (col::dim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (title, r.getX() + 10, r.getY(), nameColW - 10, r.getHeight(), juce::Justification::centredLeft);
    };

    for (int b = 0; b < numBands; ++b)
    {
        drawStrip (bands[(size_t) b].strip, bandTitle[b]);
        paintBand (g, b);
    }
    drawStrip (masterStrip, "MASTER");

    // 帯の境目の線 (クロスオーバーのつまみの左右)
    g.setColour (col::panelStroke.brighter (0.15f));
    for (auto& x : xos)
    {
        const int y = x.slider.getBounds().getCentreY();
        g.drawHorizontalLine (y, (float) margin + 10.0f, (float) x.slider.getX() - 8.0f);
        g.drawHorizontalLine (y, (float) x.slider.getRight() + 8.0f, (float) (winW - margin - 10));
    }

    // MASTER の小見出し
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("Slope", slopeArea.withHeight (14), juce::Justification::centred);

    paintMeters (g);
}

void Transist4AudioProcessorEditor::paintBand (juce::Graphics& g, int b)
{
    const auto& band = bands[(size_t) b];
    const auto area = band.lcd.toFloat();
    g.setColour (col::lcd);
    g.fillRoundedRectangle (area, 2.0f);

    const auto lane = area.reduced (6.0f, 4.0f);
    const float mid = lane.getCentreY();
    const int n = Transist4AudioProcessor::displayCols;

    // 見えている範囲の最大で縦の拡大率を決める (処理前・処理後の大きい方)
    float peak = 1.0e-4f;
    for (auto& c : band.cols)
        peak = juce::jmax (peak, juce::jmax (std::abs (c.preMin), std::abs (c.preMax), std::abs (c.postMin), std::abs (c.postMax)));
    const float scale = (lane.getHeight() * 0.5f - 1.0f) / peak;

    auto layer = [&] (bool post, juce::Colour colour)
    {
        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const auto& c = band.cols[(size_t) ((writePos + i) % n)];
            const float x = lane.getX() + lane.getWidth() * (float) i / (float) (n - 1);
            const float y = mid - (post ? c.postMax : c.preMax) * scale;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        for (int i = n - 1; i >= 0; --i)
        {
            const auto& c = band.cols[(size_t) ((writePos + i) % n)];
            const float x = lane.getX() + lane.getWidth() * (float) i / (float) (n - 1);
            p.lineTo (x, mid - (post ? c.postMin : c.preMin) * scale);
        }
        p.closeSubPath();
        g.setColour (colour);
        g.fillPath (p);
    };
    layer (false, col::text.withAlpha (0.14f));
    layer (true, col::wave.withAlpha (band.dimmed ? 0.25f : 0.9f));
}

void Transist4AudioProcessorEditor::paintMeters (juce::Graphics& g)
{
    const auto area = meterArea.toFloat();
    g.setColour (col::lcd);
    g.fillRoundedRectangle (area, 2.0f);

    const float left = area.getX() + 34.0f, right = area.getRight() - 10.0f;
    const float barH = 8.0f;

    auto dbToX = [&] (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - meterMinDb) / (meterMaxDb - meterMinDb));
        return left + t * (right - left);
    };
    const float zeroX = dbToX (0.0f);

    const std::array<std::pair<const char*, float>, 2> meters { { { "IN", meterIn }, { "OUT", meterOut } } };
    const float rowGap = area.getHeight() / 3.0f;
    for (size_t i = 0; i < meters.size(); ++i)
    {
        const float yc = area.getY() + rowGap * (float) (i + 1);
        const auto bar = juce::Rectangle<float> (left, yc - barH * 0.5f, right - left, barH);
        g.setColour (col::lcdGrid);
        g.fillRect (bar);

        const float x = dbToX (meters[i].second);
        if (x > left)
        {
            g.setColour (col::meter);
            g.fillRect (bar.withRight (juce::jmin (x, zeroX)));
            if (x > zeroX)
            {
                g.setColour (col::over);
                g.fillRect (bar.withLeft (zeroX).withRight (x));
            }
        }

        g.setColour (col::dim);
        g.setFont (juce::FontOptions (9.5f));
        g.drawText (meters[i].first, juce::Rectangle<float> (area.getX() + 6.0f, yc - 6.0f, 26.0f, 12.0f),
                    juce::Justification::centredLeft);
    }

    // 0dBFS の目盛り
    g.setColour (col::dim.withAlpha (0.6f));
    g.drawVerticalLine ((int) zeroX, area.getY() + 6.0f, area.getBottom() - 6.0f);
}

void Transist4AudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (margin, 0);
    area.removeFromTop (titleH);

    // 帯の中: [名前] [波形] [Transient] [Makeup] [M S B]
    auto layoutStripControls = [] (juce::Rectangle<int> strip, Knob& k1, Knob& k2)
    {
        auto r = strip.reduced (0, 6);
        r.removeFromRight (10);
        auto msb = r.removeFromRight (msbW);
        r.removeFromRight (12);
        auto c2 = r.removeFromRight (knobCellW);
        auto c1 = r.removeFromRight (knobCellW);
        for (auto [k, cell] : { std::pair<Knob*, juce::Rectangle<int>> { &k1, c1 }, { &k2, c2 } })
        {
            k->label.setBounds (cell.removeFromTop (14));
            k->slider.setBounds (cell.reduced (2, 0));
        }
        return msb;
    };

    for (int row = 0; row < numBands; ++row)
    {
        const int b = numBands - 1 - row;   // 上が HIGH
        auto& band = bands[(size_t) b];
        band.strip = area.removeFromTop (stripH);

        auto msb = layoutStripControls (band.strip, band.transient, band.makeup);
        const int bh = 18, gap = (msb.getHeight() - 3 * bh) / 2;
        band.mute.button.setBounds   (msb.getX(), msb.getY(), msbW, bh);
        band.solo.button.setBounds   (msb.getX(), msb.getY() + bh + gap, msbW, bh);
        band.bypass.button.setBounds (msb.getX(), msb.getY() + 2 * (bh + gap), msbW, bh);

        band.lcd = band.strip.withTrimmedLeft (nameColW)
                             .withTrimmedRight (controlsW + 10)
                             .reduced (0, 8);

        if (row < numBands - 1)
        {
            auto xoRow = area.removeFromTop (xoRowH);
            for (auto& x : xos)
                if (x.row == row)
                {
                    // つまみは Transient の列の中央に合わせる
                    const int cx = band.transient.slider.getBounds().getCentreX();
                    x.slider.setBounds (cx - 40, xoRow.getCentreY() - 10, 80, 20);
                }
        }
    }

    area.removeFromTop (12);
    masterStrip = area.removeFromTop (stripH);

    // MASTER: [名前] [Slope] [Dry/Wet] [Output] [Delta/Bypass] [IN/OUT]
    auto r = masterStrip.withTrimmedLeft (nameColW).reduced (0, 6);
    slopeArea = r.removeFromLeft (96);
    slopeSwitch.setBounds (slopeArea.withTrimmedTop (22).withHeight (20));
    r.removeFromLeft (16);
    for (auto* k : { &mix, &output })
    {
        auto cell = r.removeFromLeft (knobCellW);
        k->label.setBounds (cell.removeFromTop (14));
        k->slider.setBounds (cell.reduced (2, 0));
    }
    r.removeFromLeft (16);
    auto btns = r.removeFromLeft (76);
    delta.button.setBounds (btns.getX(), btns.getCentreY() - 22, 76, 20);
    bypass.button.setBounds (btns.getX(), btns.getCentreY() + 2, 76, 20);
    r.removeFromLeft (16);
    meterArea = r.withTrimmedRight (10).reduced (0, 4);
}
