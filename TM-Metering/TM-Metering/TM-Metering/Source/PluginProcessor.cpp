#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    void atomicMax (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout TMMeteringProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto choice = [] (const char* id, const char* name, StringArray items, int def)
    {
        return std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def);
    };

    l.add (choice ("channel", "Channel",        { "Left", "Right", "Mono" },            2));
    l.add (choice ("mode",    "Mode",           { "RMS Slow", "RMS Fast", "Peak" },     2));
    l.add (choice ("bands",   "Analyzer Bands", { "1/3", "1/6", "1/12" },               2));
    l.add (choice ("hold",    "Peak Hold",      { "Off", "2 s", "5 s", "Inf" },         1));
    l.add (choice ("level",   "Level",          { "Peak", "RMS", "Peak & RMS" },        2));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release Rate",
                                                  NormalisableRange<float> (1.f, 60.f, 0.1f), 12.f,
                                                  AudioParameterFloatAttributes().withLabel ("dB/s")));
    return l;
}

TMMeteringProcessor::TMMeteringProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& a : analyzerRing) a.store (0.f, std::memory_order_relaxed);
    for (auto& a : gonioL)       a.store (0.f, std::memory_order_relaxed);
    for (auto& a : gonioR)       a.store (0.f, std::memory_order_relaxed);
    channelParam = apvts.getRawParameterValue ("channel");
}

bool TMMeteringProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void TMMeteringProcessor::prepareToPlay (double sr, int)
{
    sampleRateHz.store (sr);
    rmsCoeff = 1.0 - std::exp (-1.0 / (0.3 * sr));
    msL_ = msR_ = lr_ = 0.0;
}

void TMMeteringProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    if (numIn == 0 || n == 0) return;

    const float* l = buffer.getReadPointer (0);
    const float* r = numIn > 1 ? buffer.getReadPointer (1) : l;
    const int sel = (int) channelParam->load();

    int aPos = analyzerPos.load (std::memory_order_relaxed);
    int gPos = gonioPos.load (std::memory_order_relaxed);
    float pkL = 0.f, pkR = 0.f;

    for (int i = 0; i < n; ++i)
    {
        const float a = l[i], b = r[i];

        pkL = std::max (pkL, std::abs (a));
        pkR = std::max (pkR, std::abs (b));

        msL_ += rmsCoeff * ((double) a * a - msL_);
        msR_ += rmsCoeff * ((double) b * b - msR_);
        lr_  += rmsCoeff * ((double) a * b - lr_);

        const float s = sel == 0 ? a : sel == 1 ? b : 0.5f * (a + b);
        analyzerRing[(size_t) aPos].store (s, std::memory_order_relaxed);
        aPos = (aPos + 1) & (ringSize - 1);

        gonioL[(size_t) gPos].store (a, std::memory_order_relaxed);
        gonioR[(size_t) gPos].store (b, std::memory_order_relaxed);
        gPos = (gPos + 1) & (gonioSize - 1);
    }

    analyzerPos.store (aPos, std::memory_order_release);
    gonioPos.store (gPos, std::memory_order_release);

    atomicMax (peakL, pkL);
    atomicMax (peakR, pkR);
    msL.store ((float) msL_, std::memory_order_relaxed);
    msR.store ((float) msR_, std::memory_order_relaxed);

    const double denom = std::sqrt (msL_ * msR_);
    correlation.store (denom > 1.0e-10 ? (float) juce::jlimit (-1.0, 1.0, lr_ / denom) : 0.f,
                       std::memory_order_relaxed);
    // El audio no se modifica: pasa tal cual.
}

void TMMeteringProcessor::readAnalyzer (float* dest, int n) const
{
    const int p = analyzerPos.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        dest[i] = analyzerRing[(size_t) ((p - n + i + ringSize) & (ringSize - 1))].load (std::memory_order_relaxed);
}

void TMMeteringProcessor::readGonio (float* l, float* r, int n) const
{
    const int p = gonioPos.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
    {
        const auto idx = (size_t) ((p - n + i + gonioSize) & (gonioSize - 1));
        l[i] = gonioL[idx].load (std::memory_order_relaxed);
        r[i] = gonioR[idx].load (std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* TMMeteringProcessor::createEditor() { return new TMMeteringEditor (*this); }

void TMMeteringProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void TMMeteringProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TMMeteringProcessor(); }
