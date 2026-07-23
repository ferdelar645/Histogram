#include "PluginProcessor.h"

//==============================================================================
HistogramProcessor::HistogramProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    resetCurve (activeBins);
}

juce::AudioProcessorValueTreeState::ParameterLayout
HistogramProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Amount",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));

    // How long the plugin remembers what the signal looked like. Short = the
    // curve chases every transient; long = a slow, settled global shape.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "memory", 1 }, "Memory",
        NormalisableRange<float> (5.0f, 10000.0f, 1.0f, 0.3f), 400.0f, "ms"));

    // Histogram resolution. Low values quantise the transfer curve into visible
    // steps -- a bitcrush whose step positions follow the music.
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "grain", 1 }, "Grain", 4, 512, 256));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, "dB"));

    return layout;
}

//==============================================================================
void HistogramProcessor::resetCurve (int bins)
{
    histogram.fill (0.0f);
    for (int i = 0; i <= bins; ++i)
    {
        const float v = -1.0f + 2.0f * (float) i / (float) bins;   // identity
        curve[(size_t) i]  = v;
        target[(size_t) i] = v;
    }
    dcX.fill (0.0f);
    dcY.fill (0.0f);
}

void HistogramProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    resetCurve (activeBins);
}

bool HistogramProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void HistogramProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples == 0 || numCh == 0)
        return;

    const float mix     = apvts.getRawParameterValue ("mix")->load() * 0.01f;
    const float memMs   = apvts.getRawParameterValue ("memory")->load();
    const int   bins    = juce::jlimit (4, kMaxBins,
                              (int) apvts.getRawParameterValue ("grain")->load());
    const float outGain = juce::Decibels::decibelsToGain (
                              apvts.getRawParameterValue ("output")->load());

    if (bins != activeBins)
    {
        activeBins = bins;
        resetCurve (bins);
    }

    const double blockSeconds = (double) numSamples / sr;
    const float  binScale     = 0.5f * (float) bins;

    // ---- 1. forget a little, then count this block's sample values -----------
    const float decay = (float) std::exp (-blockSeconds
                            / juce::jmax (0.001, (double) memMs * 0.001));

    for (int i = 0; i < bins; ++i)
        histogram[(size_t) i] *= decay;

    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto* src = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            const int b = juce::jlimit (0, bins - 1,
                              (int) ((src[i] + 1.0f) * binScale));
            histogram[(size_t) b] += 1.0f;
        }
    }

    // ---- 2. integrate the histogram into a CDF: that IS the transfer curve ---
    double total = 0.0;
    for (int i = 0; i < bins; ++i)
        total += (double) histogram[(size_t) i];

    if (total > 1.0e-9)
    {
        double running = 0.0;
        target[0] = -1.0f;

        for (int i = 0; i < bins; ++i)
        {
            running += (double) histogram[(size_t) i];
            target[(size_t) i + 1] = (float) (2.0 * (running / total) - 1.0);
        }
    }

    // ---- 3. glide the live curve toward the new one (~15 ms) ----------------
    const float smooth = 1.0f - std::exp (-(float) blockSeconds / 0.015f);
    for (int i = 0; i <= bins; ++i)
        curve[(size_t) i] += smooth * (target[(size_t) i] - curve[(size_t) i]);

    // ---- 4. waveshape through it (shared curve keeps stereo image intact) ----
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* dst = buffer.getWritePointer (ch);
        float& px = dcX[(size_t) juce::jmin (ch, 7)];
        float& py = dcY[(size_t) juce::jmin (ch, 7)];

        for (int i = 0; i < numSamples; ++i)
        {
            const float x   = dst[i];
            const float pos = juce::jlimit (0.0f, (float) bins - 1.0e-4f,
                                            (x + 1.0f) * binScale);
            const int   idx = (int) pos;
            const float fr  = pos - (float) idx;

            const float shaped = curve[(size_t) idx]
                               + fr * (curve[(size_t) idx + 1] - curve[(size_t) idx]);

            const float wet = x + mix * (shaped - x);

            // Equalisation can shift the mean; block DC on the way out.
            const float y = wet - px + 0.995f * py;
            px = wet;
            py = y;

            dst[i] = y * outGain;
        }
    }
}

//==============================================================================
void HistogramProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HistogramProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HistogramProcessor();
}
