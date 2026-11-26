#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace TM
{
    inline const juce::Colour bg     { 0xff1b1f27 };
    inline const juce::Colour display{ 0xff09152a };
    inline const juce::Colour panel  { 0xff173257 };
    inline const juce::Colour ctrl   { 0xff0f2140 };
    inline const juce::Colour accent { 0xff38d0d8 };
    inline const juce::Colour blue   { 0xff2a74c4 };
    inline const juce::Colour text   { 0xffb9c8de };
    inline const juce::Colour grid   { 0xff21395e };
}

//==============================================================================
class TMLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TMLookAndFeel()
    {
        setColour (juce::TextButton::textColourOffId, TM::text);
        setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override
    {
        return juce::Font (juce::FontOptions (12.0f, juce::Font::bold));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const int f = b.getConnectedEdgeFlags();
        juce::Path p;
        p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 4.f, 4.f,
                               (f & juce::Button::ConnectedOnLeft)  == 0, (f & juce::Button::ConnectedOnRight) == 0,
                               (f & juce::Button::ConnectedOnLeft)  == 0, (f & juce::Button::ConnectedOnRight) == 0);

        auto c = b.getToggleState() ? TM::blue : TM::ctrl;
        if (over) c = c.brighter (0.12f);
        if (down) c = c.brighter (0.2f);
        g.setColour (c);
        g.fillPath (p);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (p, juce::PathStrokeType (1.f));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (0.5f);
        g.setColour (TM::ctrl);
        g.fillRoundedRectangle (r, 4.f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (juce::Rectangle<int> (x, y, juce::roundToInt (pos) - x, h));
            g.setColour (TM::blue);
            g.fillRoundedRectangle (r, 4.f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 4.f, 1.f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (s.getTextFromValue (s.getValue()), r, juce::Justification::centred);
    }
};

//==============================================================================
/** Botones segmentados enlazados a un parámetro de tipo choice. */
class Segmented : public juce::Component
{
public:
    Segmented (juce::RangedAudioParameter& p, const juce::StringArray& labels)
        : attachment (p, [this] (float v) { setIndex ((int) std::lround (v)); })
    {
        for (int i = 0; i < labels.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (labels[i]));
            addAndMakeVisible (b);
            b->setClickingTogglesState (false);
            int flags = 0;
            if (i > 0)                 flags |= juce::Button::ConnectedOnLeft;
            if (i < labels.size() - 1) flags |= juce::Button::ConnectedOnRight;
            b->setConnectedEdges (flags);
            b->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };
        }
        attachment.sendInitialUpdate();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int n = buttons.size();
        for (int i = 0; i < n; ++i)
            buttons[i]->setBounds (r.removeFromLeft (r.getWidth() / (n - i)));
    }

private:
    void setIndex (int idx)
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
    }

    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;
};

//==============================================================================
class AnalyzerView : public juce::Component
{
public:
    explicit AnalyzerView (TMMeteringProcessor&);
    void update (float dt);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = e.position; hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { hovering = false; repaint(); }

private:
    juce::Rectangle<float> getPlot() const;

    TMMeteringProcessor& proc;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> fftData, samples;
    std::vector<float> bandDb, holdDb, holdTime;
    int bandsPerOct = 12;
    juce::Point<float> hover;
    bool hovering = false;

    std::atomic<float> *modeP, *bandsP, *holdP, *releaseP;
};

//==============================================================================
class GonioView : public juce::Component
{
public:
    explicit GonioView (TMMeteringProcessor&);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> getScope() const;

    TMMeteringProcessor& proc;
    juce::Image trail;
    float corrSmooth = 0.f;
};

//==============================================================================
class LevelMeterView : public juce::Component
{
public:
    explicit LevelMeterView (TMMeteringProcessor&);
    void update (float dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    TMMeteringProcessor& proc;
    float peakDb[2] { -120.f, -120.f }, rmsDb[2] { -120.f, -120.f };
    float holdDb[2] { -120.f, -120.f }, holdTime[2] { 0.f, 0.f }, maxDb[2] { -120.f, -120.f };
    bool clip[2] { false, false };
    std::atomic<float> *holdP, *releaseP, *levelP;
};

//==============================================================================
class TMMeteringEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit TMMeteringEditor (TMMeteringProcessor&);
    ~TMMeteringEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showView (bool analyzerVisible);

    TMMeteringProcessor& proc;
    TMLookAndFeel laf;

    AnalyzerView analyzer;
    GonioView gonio;
    LevelMeterView meter;

    juce::TextButton tabAnalyzer { "ANALYZER" }, tabGonio { "GONIOMETER" };

    Segmented channelSeg, modeSeg, bandsSeg, holdSeg, levelSeg;
    juce::Slider releaseSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment releaseAttach;
    juce::Label lblChannel, lblMode, lblBands, lblHold, lblRelease, lblLevel;
    juce::Rectangle<int> panelBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TMMeteringEditor)
};
