#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

/** Lock-free ring buffer that carries interleaved float audio pushed from the
    browser's message thread (via the WebBrowserComponent native-event
    callback) into the real-time audio thread's processBlock().
*/
class WebAudioFifo
{
public:
    void reset (int numChannelsIn, int capacityPerChannel)
    {
        channels = juce::jmax (1, numChannelsIn);
        fifo.setTotalSize (juce::jmax (1024, capacityPerChannel));
        buffer.setSize (channels, fifo.getTotalSize());
        buffer.clear();
    }

    /** Called from the message thread when a chunk of audio arrives from the page. */
    void pushInterleaved (const float* interleaved, int numFrames, int numChannelsIn)
    {
        if (numFrames <= 0 || numChannelsIn <= 0)
            return;

        int start1, size1, start2, size2;
        fifo.prepareToWrite (numFrames, start1, size1, start2, size2);

        auto writeChunk = [&] (int destStart, int n, int srcFrameOffset)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                auto srcCh = juce::jmin (ch, numChannelsIn - 1);
                auto* dest = buffer.getWritePointer (ch) + destStart;

                for (int i = 0; i < n; ++i)
                    dest[i] = interleaved[(size_t) (srcFrameOffset + i) * (size_t) numChannelsIn + (size_t) srcCh];
            }
        };

        writeChunk (start1, size1, 0);

        if (size2 > 0)
            writeChunk (start2, size2, size1);

        fifo.finishedWrite (size1 + size2);
    }

    /** Called from the audio thread. Fills as much of `dest` as is available and
        returns the number of frames actually written (dest is not cleared first).
    */
    int popInto (juce::AudioBuffer<float>& dest, int numFrames)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (numFrames, start1, size1, start2, size2);

        int written = 0;

        auto readChunk = [&] (int srcStart, int n)
        {
            for (int ch = 0; ch < juce::jmin (channels, dest.getNumChannels()); ++ch)
                dest.copyFrom (ch, written, buffer, ch, srcStart, n);

            written += n;
        };

        if (size1 > 0) readChunk (start1, size1);
        if (size2 > 0) readChunk (start2, size2);

        fifo.finishedRead (size1 + size2);
        return written;
    }

    int getNumReady() const { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo { 1 << 16 };
    juce::AudioBuffer<float> buffer;
    int channels = 2;
};
