#include "PluginEditor.h"
#include <cmath>

using namespace juce;

namespace
{
    constexpr float dbFloor = -96.f;

    String formatDb (float db) { return db < -100.f ? String ("-inf") : String (db, 1); }

    // Lógica común de "peak hold": mantiene, y tras el tiempo de hold cae a release dB/s.
    void updateHold (float current, float& holdVal, float& timer, int holdIdx, float rel, float dt)
    {
        if (holdIdx == 0) { holdVal = current; timer = 0.f; return; }
        if (current >= holdVal) { holdVal = current; timer = 0.f; return; }
        timer += dt;
        const float holdSeconds = holdIdx == 1 ? 2.f : holdIdx == 2 ? 5.f : 1.0e9f;
        if (timer > holdSeconds) holdVal = jmax (current, holdVal - rel * dt);
    }
}

//==============================================================================
// ANALYZER
//==============================================================================
AnalyzerView::AnalyzerView (TMMeteringProcessor& p)
    : proc (p),
      fft (TMMeteringProcessor::fftOrder),
      window ((size_t) TMMeteringProcessor::fftSize, dsp::WindowingFunction<float>::hann, false),
      fftData ((size_t) TMMeteringProcessor::fftSize * 2, 0.f),
      samples ((size_t) TMMeteringProcessor::fftSize, 0.f)
{
    modeP    = proc.apvts.getRawParameterValue ("mode");
    bandsP   = proc.apvts.getRawParameterValue ("bands");
    holdP    = proc.apvts.getRawParameterValue ("hold");
    releaseP = proc.apvts.getRawParameterValue ("release");
}

Rectangle<float> AnalyzerView::getPlot() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (34).withTrimmedBottom (18)
                                     .withTrimmedTop (8).withTrimmedRight (10);
}

void AnalyzerView::update (float dt)
{
    constexpr int N = TMMeteringProcessor::fftSize;
    const int bandsIdx = (int) bandsP->load();
    const int bpo = bandsIdx == 0 ? 3 : bandsIdx == 1 ? 6 : 12;
    const int nb  = 10 * bpo;                                // 20 Hz .. 20.48 kHz (10 octavas)

    if (bpo != bandsPerOct || (int) bandDb.size() != nb)
    {
        bandsPerOct = bpo;
        bandDb.assign ((size_t) nb, -120.f);
        holdDb.assign ((size_t) nb, -120.f);
        holdTime.assign ((size_t) nb, 0.f);
    }

    proc.readAnalyzer (samples.data(), N);
    std::copy (samples.begin(), samples.end(), fftData.begin());
    std::fill (fftData.begin() + N, fftData.end(), 0.f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) N);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const double sr   = proc.sampleRateHz.load();
    const double bw   = sr / N;
    const int    half = N / 2;
    const float  scale = 4.0f / (float) N;                   // Hann: 0 dBFS para un seno a fondo de escala
    const float  rel  = releaseP->load();
    const int    mode = (int) modeP->load();                 // 0 RMS Slow, 1 RMS Fast, 2 Peak
    const int    holdIdx = (int) holdP->load();

    for (int b = 0; b < nb; ++b)
    {
        const double lo = 20.0 * std::pow (2.0, (double) b / bpo);
        const double hi = 20.0 * std::pow (2.0, (b + 1.0) / bpo);
        const double fc = std::sqrt (lo * hi);

        float newDb = -120.f;
        if (fc < sr * 0.5)
        {
            const int i0 = (int) std::ceil (lo / bw);
            const int i1 = jmin ((int) std::floor (hi / bw), half - 1);
            float mag = 0.f;

            if (i1 >= i0)
            {
                double s = 0.0;
                for (int i = i0; i <= i1; ++i) s += (double) fftData[(size_t) i] * fftData[(size_t) i];
                mag = (float) std::sqrt (s / (i1 - i0 + 1));
            }
            else
            {
                const double pos = fc / bw;
                const int i = jlimit (0, half - 2, (int) pos);
                const float fr = (float) (pos - i);
                mag = fftData[(size_t) i] * (1.f - fr) + fftData[(size_t) i + 1] * fr;
            }
            newDb = Decibels::gainToDecibels (mag * scale, -120.f);
        }

        float& cur = bandDb[(size_t) b];
        if (mode == 2)       cur = newDb > cur ? newDb : jmax (newDb, cur - rel * dt);
        else if (mode == 1)  cur += (1.f - std::exp (-dt / 0.12f)) * (newDb - cur);
        else                 cur += (1.f - std::exp (-dt / 0.60f)) * (newDb - cur);

        updateHold (cur, holdDb[(size_t) b], holdTime[(size_t) b], holdIdx, rel, dt);
    }
    repaint();
}

void AnalyzerView::paint (Graphics& g)
{
    g.setColour (TM::display);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.f);

    const auto plot = getPlot();
    auto fx = [&] (double f) { return plot.getX() + plot.getWidth() * (float) (std::log2 (f / 20.0) / 10.0); };
    auto fy = [&] (float db) { return plot.getY() + plot.getHeight() * jlimit (0.f, 1.f, -db / -dbFloor); };

    g.setFont (10.f);

    // Rejilla de frecuencia
    const double fr[]    = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    const char*  frTxt[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    for (int i = 0; i < 10; ++i)
    {
        const float x = fx (fr[i]);
        g.setColour (TM::grid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (TM::text.withAlpha (0.7f));
        g.drawText (frTxt[i], Rectangle<float> (x - 18, plot.getBottom() + 2, 36, 14), Justification::centred);
    }
    // Rejilla de nivel
    for (int db = 0; db >= -96; db -= 12)
    {
        const float y = fy ((float) db);
        g.setColour (TM::grid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (TM::text.withAlpha (0.7f));
        g.drawText (String (db), Rectangle<float> (0, y - 7, 30, 14), Justification::centredRight);
    }

    // Barras
    const int nb = (int) bandDb.size();
    if (nb == 0) return;

    g.setGradientFill (ColourGradient::vertical (TM::accent.brighter (0.2f), plot.getY(),
                                                 TM::blue.darker (0.3f), plot.getBottom()));
    for (int b = 0; b < nb; ++b)
    {
        const double lo = 20.0 * std::pow (2.0, (double) b / bandsPerOct);
        const double hi = 20.0 * std::pow (2.0, (b + 1.0) / bandsPerOct);
        const float x0 = fx (lo) + 0.5f, x1 = jmin (fx (hi) - 0.5f, plot.getRight());
        const float y = fy (bandDb[(size_t) b]);
        if (y < plot.getBottom() - 0.5f && x1 > x0)
            g.fillRect (x0, y, x1 - x0, plot.getBottom() - y);
    }

    if ((int) holdP->load() != 0)
    {
        g.setColour (Colours::white.withAlpha (0.85f));
        for (int b = 0; b < nb; ++b)
        {
            if (holdDb[(size_t) b] < -95.f) continue;
            const float x0 = fx (20.0 * std::pow (2.0, (double) b / bandsPerOct)) + 0.5f;
            const float x1 = jmin (fx (20.0 * std::pow (2.0, (b + 1.0) / bandsPerOct)) - 0.5f, plot.getRight());
            g.fillRect (x0, fy (holdDb[(size_t) b]) - 1.f, jmax (1.f, x1 - x0), 2.f);
        }
    }

    // Lectura bajo el ratón
    if (hovering && plot.contains (hover))
    {
        const double f = 20.0 * std::pow (2.0, 10.0 * (hover.x - plot.getX()) / plot.getWidth());
        const int b = jlimit (0, nb - 1, (int) std::floor (10.0 * bandsPerOct * (hover.x - plot.getX()) / plot.getWidth()));
        g.setColour (Colours::white.withAlpha (0.5f));
        g.drawVerticalLine ((int) hover.x, plot.getY(), plot.getBottom());
        g.setColour (Colours::white);
        g.setFont (Font (FontOptions (11.f, Font::bold)));
        const String fs = f >= 1000 ? String (f / 1000.0, 2) + " kHz" : String ((int) f) + " Hz";
        g.drawText (fs + "   " + formatDb (bandDb[(size_t) b]) + " dB",
                    Rectangle<float> (plot.getX() + 6, plot.getY() + 4, 200, 14), Justification::left);
    }
}

//==============================================================================
// GONIOMETER
//==============================================================================
GonioView::GonioView (TMMeteringProcessor& p) : proc (p) {}

void GonioView::resized()
{
    if (getWidth() > 0 && getHeight() > 0)
    {
        trail = Image (Image::ARGB, getWidth(), getHeight(), true);
        Graphics tg (trail);
        tg.fillAll (TM::display);
    }
}

Rectangle<float> GonioView::getScope() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (36);
}

void GonioView::update()
{
    if (! trail.isValid()) return;

    constexpr int N = 1024;
    float l[N], r[N];
    proc.readGonio (l, r, N);

    const auto area = getScope();
    const float cx = area.getCentreX(), cy = area.getCentreY();
    const float radius = jmin (area.getWidth(), area.getHeight()) * 0.5f - 16.f;

    Graphics tg (trail);
    tg.setColour (TM::display.withAlpha (0.30f));   // persistencia tipo fósforo
    tg.fillAll();

    Path path;
    bool first = true;
    for (int i = 0; i < N; ++i)
    {
        float x = (r[i] - l[i]) * 0.5f;             // side
        float y = (l[i] + r[i]) * 0.5f;             // mid
        const float m = std::hypot (x, y);
        if (m > 1.0e-6f) { const float s = std::pow (jmin (m, 1.f), 0.65f) / m; x *= s; y *= s; }
        const float px = cx + x * radius, py = cy - y * radius;
        if (first) { path.startNewSubPath (px, py); first = false; }
        else path.lineTo (px, py);
    }
    tg.setColour (TM::accent.withAlpha (0.9f));
    tg.strokePath (path, PathStrokeType (1.2f));

    corrSmooth += 0.25f * (proc.correlation.load() - corrSmooth);
    repaint();
}

void GonioView::paint (Graphics& g)
{
    g.setColour (TM::display);
    g.fillRect (getLocalBounds());
    if (trail.isValid()) g.drawImageAt (trail, 0, 0);

    const auto area = getScope();
    const float cx = area.getCentreX(), cy = area.getCentreY();
    const float rad = jmin (area.getWidth(), area.getHeight()) * 0.5f - 16.f;

    g.setColour (TM::grid.brighter (0.3f).withAlpha (0.8f));
    g.drawEllipse (cx - rad, cy - rad, rad * 2, rad * 2, 1.f);
    g.drawEllipse (cx - rad * 0.5f, cy - rad * 0.5f, rad, rad, 0.5f);
    g.drawLine (cx - rad, cy, cx + rad, cy, 0.5f);
    g.drawLine (cx, cy - rad, cx, cy + rad, 0.5f);
    const float d = rad * 0.7071f;
    g.drawLine (cx - d, cy - d, cx + d, cy + d, 0.5f);
    g.drawLine (cx - d, cy + d, cx + d, cy - d, 0.5f);

    g.setColour (TM::text);
    g.setFont (Font (FontOptions (11.f, Font::bold)));
    g.drawText ("M",  Rectangle<float> (cx - 10, cy - rad - 15, 20, 12), Justification::centred);
    g.drawText ("L",  Rectangle<float> (cx - d - 18, cy - d - 14, 20, 12), Justification::centred);
    g.drawText ("R",  Rectangle<float> (cx + d - 2,  cy - d - 14, 20, 12), Justification::centred);
    g.drawText ("S",  Rectangle<float> (cx + rad + 2, cy - 6, 14, 12), Justification::centred);

    // Medidor de correlación de fase
    const float tx0 = 40.f, tx1 = (float) getWidth() - 40.f, ty = (float) getHeight() - 22.f;
    Rectangle<float> track (tx0, ty, tx1 - tx0, 8.f);
    ColourGradient cg (Colour (0xffd9534f), tx0, ty, Colour (0xff4cc38a), tx1, ty, false);
    cg.addColour (0.5, Colour (0xffe6c84a));
    g.setGradientFill (cg);
    g.setOpacity (0.45f);
    g.fillRoundedRectangle (track, 3.f);

    const float mx = track.getCentreX() + corrSmooth * track.getWidth() * 0.5f;
    g.setColour (Colours::white);
    g.fillRect (mx - 1.5f, ty - 3.f, 3.f, 14.f);

    g.setColour (TM::text.withAlpha (0.8f));
    g.setFont (10.f);
    g.drawText ("-1", Rectangle<float> (tx0 - 30, ty - 3, 26, 14), Justification::centredRight);
    g.drawText ("+1", Rectangle<float> (tx1 + 4,  ty - 3, 26, 14), Justification::centredLeft);
    g.drawText ("0",  Rectangle<float> (track.getCentreX() - 10, ty + 9, 20, 12), Justification::centred);
    g.drawText ("CORRELATION " + String (corrSmooth, 2),
                Rectangle<float> (track.getCentreX() - 70, ty - 16, 140, 12), Justification::centred);
}

//==============================================================================
// LEVEL METER
//==============================================================================
LevelMeterView::LevelMeterView (TMMeteringProcessor& p) : proc (p)
{
    holdP    = proc.apvts.getRawParameterValue ("hold");
    releaseP = proc.apvts.getRawParameterValue ("release");
    levelP   = proc.apvts.getRawParameterValue ("level");
}

void LevelMeterView::update (float dt)
{
    const float rel = releaseP->load();
    const int holdIdx = (int) holdP->load();

    for (int c = 0; c < 2; ++c)
    {
        const float pk = (c == 0 ? proc.peakL : proc.peakR).exchange (0.f, std::memory_order_relaxed);
        if (pk >= 0.9999f) clip[c] = true;

        const float db = Decibels::gainToDecibels (pk, -120.f);
        peakDb[c] = db > peakDb[c] ? db : jmax (db, peakDb[c] - rel * dt);
        maxDb[c]  = jmax (maxDb[c], db);
        updateHold (peakDb[c], holdDb[c], holdTime[c], holdIdx, rel, dt);

        const float ms = (c == 0 ? proc.msL : proc.msR).load (std::memory_order_relaxed);
        rmsDb[c] = 10.f * std::log10 (ms + 1.0e-12f);
    }
    repaint();
}

void LevelMeterView::mouseDown (const MouseEvent&)
{
    for (int c = 0; c < 2; ++c) { clip[c] = false; maxDb[c] = -120.f; holdDb[c] = -120.f; }
    repaint();
}

void LevelMeterView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (TM::display);
    g.fillRoundedRectangle (b, 4.f);

    g.setColour (TM::text);
    g.setFont (Font (FontOptions (10.f, Font::bold)));
    g.drawText ("CLIPS", b.withHeight (16).translated (0, 4), Justification::centred);

    const float barW = 18.f, gap = 10.f;
    const float scaleW = 26.f;
    const float groupW = barW * 2 + gap;
    const float x0 = b.getX() + scaleW + (b.getWidth() - scaleW - groupW) * 0.5f;
    const float top = 66.f, bottom = b.getBottom() - 10.f, h = bottom - top;
    auto fy = [&] (float db) { return top + h * jlimit (0.f, 1.f, -db / 60.f); };

    // Indicadores de clip + lecturas numéricas
    for (int c = 0; c < 2; ++c)
    {
        const float bx = x0 + c * (barW + gap);
        g.setColour (clip[c] ? Colour (0xffe8453c) : TM::ctrl);
        g.fillRoundedRectangle (bx, 24.f, barW, 8.f, 2.f);
        g.setColour (TM::text);
        g.setFont (Font (FontOptions (10.f, Font::bold)));
        g.drawText (formatDb (maxDb[c]), Rectangle<float> (bx - 8, 38.f, barW + 16, 14.f), Justification::centred);
        g.drawText (c == 0 ? "L" : "R", Rectangle<float> (bx, 51.f, barW, 12.f), Justification::centred);
    }

    // Escala
    g.setFont (9.f);
    for (int db : { 0, -6, -12, -18, -24, -36, -48, -60 })
    {
        const float y = fy ((float) db);
        g.setColour (TM::grid);
        g.drawHorizontalLine ((int) y, b.getX() + scaleW - 2, x0 + groupW + 4);
        g.setColour (TM::text.withAlpha (0.75f));
        g.drawText (String (db), Rectangle<float> (b.getX(), y - 6, scaleW - 4, 12), Justification::centredRight);
    }

    const int lv = (int) levelP->load();   // 0 Peak, 1 RMS, 2 Peak & RMS
    for (int c = 0; c < 2; ++c)
    {
        const float bx = x0 + c * (barW + gap);
        g.setColour (TM::ctrl);
        g.fillRect (bx, top, barW, h);

        ColourGradient grad (Colour (0xffe8453c), bx, fy (0.f), TM::accent, bx, fy (-24.f), false);
        grad.addColour (fy (-6.f) > fy (0.f) ? (double) (fy (-6.f) - fy (0.f)) / (fy (-24.f) - fy (0.f)) : 0.2,
                        Colour (0xffe6c84a));

        auto drawBar = [&] (float db, float inset, float alpha)
        {
            const float y = fy (db);
            g.setGradientFill (grad);
            g.setOpacity (alpha);
            g.fillRect (bx + inset, y, barW - 2 * inset, bottom - y);
        };

        if (lv == 0)      drawBar (peakDb[c], 0.f, 1.f);
        else if (lv == 1) drawBar (rmsDb[c],  0.f, 1.f);
        else              { drawBar (peakDb[c], 0.f, 0.4f); drawBar (rmsDb[c], 4.f, 1.f); }

        if ((int) holdP->load() != 0 && holdDb[c] > -100.f)
        {
            g.setColour (Colours::white);
            g.fillRect (bx, fy (holdDb[c]) - 1.f, barW, 2.f);
        }
    }
}

//==============================================================================
// EDITOR
//==============================================================================
TMMeteringEditor::TMMeteringEditor (TMMeteringProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      analyzer (p), gonio (p), meter (p),
      channelSeg (*p.apvts.getParameter ("channel"), { "Left", "Right", "Mono" }),
      modeSeg    (*p.apvts.getParameter ("mode"),    { "RMS Slow", "RMS Fast", "Peak" }),
      bandsSeg   (*p.apvts.getParameter ("bands"),   { "1/3", "1/6", "1/12" }),
      holdSeg    (*p.apvts.getParameter ("hold"),    { "Off", "2 s", "5 s", "Inf" }),
      levelSeg   (*p.apvts.getParameter ("level"),   { "Peak", "RMS", "Peak & RMS" }),
      releaseAttach (p.apvts, "release", releaseSlider)
{
    setLookAndFeel (&laf);

    addAndMakeVisible (analyzer);
    addChildComponent (gonio);
    addAndMakeVisible (meter);

    for (auto* t : { &tabAnalyzer, &tabGonio })
    {
        addAndMakeVisible (t);
        t->setRadioGroupId (1);
        t->setClickingTogglesState (true);
    }
    tabAnalyzer.setConnectedEdges (Button::ConnectedOnRight);
    tabGonio.setConnectedEdges (Button::ConnectedOnLeft);
    tabAnalyzer.setToggleState (true, dontSendNotification);
    tabAnalyzer.onClick = [this] { showView (true); };
    tabGonio.onClick    = [this] { showView (false); };

    for (auto* c : { (Component*) &channelSeg, (Component*) &modeSeg, (Component*) &bandsSeg,
                     (Component*) &holdSeg, (Component*) &levelSeg, (Component*) &releaseSlider })
        addAndMakeVisible (c);

    releaseSlider.setSliderStyle (Slider::LinearBar);
    releaseSlider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    releaseSlider.setTextValueSuffix (" dB/s");
    releaseSlider.setNumDecimalPlacesToDisplay (1);

    auto initLabel = [this] (Label& l, const String& t)
    {
        l.setText (t, dontSendNotification);
        l.setFont (Font (FontOptions (11.f, Font::bold)));
        l.setColour (Label::textColourId, TM::text);
        l.setJustificationType (Justification::centredLeft);
        l.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
    };
    initLabel (lblChannel, "Channel");
    initLabel (lblMode,    "Mode");
    initLabel (lblBands,   "Analyzer Bands");
    initLabel (lblHold,    "Peak Hold");
    initLabel (lblRelease, "Release Rate");
    initLabel (lblLevel,   "Level");

    setResizeLimits (600, 420, 1600, 1000);
    setResizable (true, true);
    setSize (720, 480);
    startTimerHz (30);
}

TMMeteringEditor::~TMMeteringEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TMMeteringEditor::showView (bool analyzerVisible)
{
    analyzer.setVisible (analyzerVisible);
    gonio.setVisible (! analyzerVisible);
}

void TMMeteringEditor::timerCallback()
{
    const float dt = 1.0f / 30.0f;
    meter.update (dt);
    if (analyzer.isVisible()) analyzer.update (dt);
    if (gonio.isVisible())    gonio.update();
}

void TMMeteringEditor::paint (Graphics& g)
{
    g.fillAll (TM::bg);

    // Cabecera
    auto head = getLocalBounds().removeFromTop (34);
    g.setGradientFill (ColourGradient::vertical (Colour (0xff343944), 0.f, TM::bg, 34.f));
    g.fillRect (head);
    g.setColour (TM::accent);
    g.fillEllipse (12.f, 8.f, 18.f, 18.f);
    g.setColour (TM::bg);
    g.setFont (Font (FontOptions (10.f, Font::bold)));
    g.drawText ("TM", Rectangle<float> (12.f, 8.f, 18.f, 18.f), Justification::centred);
    g.setColour (Colours::white.withAlpha (0.9f));
    g.setFont (Font (FontOptions (15.f, Font::bold)));
    g.drawText ("TM Metering", head.withTrimmedLeft (40), Justification::centredLeft);

    // Panel inferior
    g.setGradientFill (ColourGradient::vertical (TM::panel.brighter (0.1f), (float) panelBounds.getY(),
                                                 TM::panel.darker (0.3f), (float) panelBounds.getBottom()));
    g.fillRect (panelBounds);

    // Pie
    g.setColour (TM::text.withAlpha (0.6f));
    g.setFont (Font (FontOptions (11.f, Font::bold)));
    g.drawText ("TM Metering", getLocalBounds().removeFromBottom (20), Justification::centred);
}

void TMMeteringEditor::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (34);
    auto tabs = r.removeFromTop (26);
    tabAnalyzer.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2));
    tabGonio.setBounds (tabs);

    r.removeFromBottom (20);
    panelBounds = r.removeFromBottom (126);

    auto displayArea = r.reduced (8, 6);
    meter.setBounds (displayArea.removeFromRight (96));
    displayArea.removeFromRight (6);
    analyzer.setBounds (displayArea);
    gonio.setBounds (displayArea);

    auto grid = panelBounds.reduced (14, 8);
    const int colW = grid.getWidth() / 3, rowH = grid.getHeight() / 2;
    auto place = [&] (int row, int col, Label& lab, Component& c)
    {
        Rectangle<int> cell (grid.getX() + col * colW, grid.getY() + row * rowH, colW - 12, rowH);
        lab.setBounds (cell.removeFromTop (16));
        c.setBounds (cell.removeFromTop (28));
    };
    place (0, 0, lblChannel, channelSeg);
    place (0, 1, lblMode,    modeSeg);
    place (0, 2, lblBands,   bandsSeg);
    place (1, 0, lblHold,    holdSeg);
    place (1, 1, lblRelease, releaseSlider);
    place (1, 2, lblLevel,   levelSeg);
}
