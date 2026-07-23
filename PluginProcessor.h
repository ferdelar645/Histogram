#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

//==============================================================================
//  HISTOGRAM — adaptive amplitude-histogram equalisation.
//
//  Borrowed wholesale from image processing: build a running histogram of the
//  incoming sample values, integrate it into a CDF, and use that CDF as the
//  waveshaping transfer curve. The result is a distortion that has no fixed
//  shape at all -- it is derived, continuously, from the statistics of whatever
//  you feed it, and it drives the output toward a uniform amplitude
//  distribution (maximum amplitude entropy).
//
//  Side effect worth knowing: a sine wave has an arcsine amplitude
//  distribution, so equalising it yields (2/pi)*asin(x) -- a perfect triangle
//  wave. Feed it noise and almost nothing happens, because noise is already
//  near-uniform.
//==============================================================================
class HistogramProcessor : public juce::AudioProcessor
{
public:
    HistogramProcessor();
    ~HistogramProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override
    {
        return new juce::GenericAudioProcessorEditor (*this);
    }
    bool hasEditor() const override                            { return true; }

    const juce::String getName() const override                { return "Histogram"; }
    bool acceptsMidi() const override                          { return false; }
    bool producesMidi() const override                         { return false; }
    bool isMidiEffect() const override                         { return false; }
    double getTailLengthSeconds() const override               { return 0.0; }

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void resetCurve (int bins);

    static constexpr int kMaxBins = 512;

    std::array<float, kMaxBins>     histogram {};   // running sample-value counts
    std::array<float, kMaxBins + 1> curve     {};   // smoothed transfer curve
    std::array<float, kMaxBins + 1> target    {};   // freshly computed CDF

    int    activeBins = 256;
    double sr         = 44100.0;

    std::array<float, 8> dcX {};                    // DC blocker state
    std::array<float, 8> dcY {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HistogramProcessor)
};
