#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

/**
    TM Metering - analizador de espectro + goniómetro + medidor de nivel/correlación.
    El audio pasa sin ningún tipo de procesado (bypass transparente).
    El hilo de audio solo escribe en buffers lock-free; la UI los lee a 30 Hz.
*/
class TMMeteringProcessor : public juce::AudioProcessor
{
public:
    static constexpr int fftOrder  = 13;
    static constexpr int fftSize   = 1 << fftOrder;   // 8192
    static constexpr int ringSize  = 1 << 14;         // 16384
    static constexpr int gonioSize = 1 << 11;         // 2048

    TMMeteringProcessor();
    ~TMMeteringProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TM Metering"; }
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

    // --- Acceso desde la UI ---
    void readAnalyzer (float* dest, int numSamples) const;
    void readGonio (float* l, float* r, int numSamples) const;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>  peakL { 0.f }, peakR { 0.f };   // máximo desde la última lectura
    std::atomic<float>  msL { 0.f }, msR { 0.f };       // potencia media (RMS^2), ventana ~300 ms
    std::atomic<float>  correlation { 0.f };
    std::atomic<double> sampleRateHz { 44100.0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::array<std::atomic<float>, ringSize>  analyzerRing;
    std::array<std::atomic<float>, gonioSize> gonioL, gonioR;
    std::atomic<int> analyzerPos { 0 }, gonioPos { 0 };

    std::atomic<float>* channelParam = nullptr;

    double msL_ = 0.0, msR_ = 0.0, lr_ = 0.0, rmsCoeff = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TMMeteringProcessor)
};
