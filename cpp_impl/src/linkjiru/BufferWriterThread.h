#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <algorithm>

class BufferWriterThread final : public juce::Thread
{
public:
    static constexpr int windowSize = 8192; // <- arbitrary size for now, can be tuned later

    BufferWriterThread(juce::AbstractFifo &fifoRef,
                       std::array<float, 131072> &fifoBufferRef)
        : Thread("LinkjiruWriter"),
          fifo(fifoRef),
          fifoBuffer(fifoBufferRef)
    {
        slidingWindow.fill(0.0f);
    }

    ~BufferWriterThread() override
    {
        stopThread(2000);
    }

    void run() override
    {
        auto dir = juce::File::getSpecialLocation(
                       juce::File::userApplicationDataDirectory)
                       .getChildFile("Linkjiru");
        dir.createDirectory();

        auto outputFile = dir.getChildFile("buffer.raw");

        while (!threadShouldExit())
        {
            const int ready = fifo.getNumReady();

            if (ready > 0)
            {
                const auto scope = fifo.read(ready);

                appendToWindow(fifoBuffer.data() + scope.startIndex1, scope.blockSize1);
                appendToWindow(fifoBuffer.data() + scope.startIndex2, scope.blockSize2);

                outputFile.replaceWithData(slidingWindow.data(),
                                           sizeof(float) * static_cast<size_t>(windowSize));
            }

            sleep(10); // ~100Hz poll rate
        }
    }

private:
    juce::AbstractFifo &fifo;
    // Must be large enough so that we can hold multiple objects in queue
    std::array<float, 131072> &fifoBuffer;
    std::array<float, windowSize> slidingWindow{};
    int writePos = 0;

    void appendToWindow(const float *data, int numSamples)
    {
        if (numSamples <= 0)
            return;

        if (numSamples >= windowSize)
        {
            // More samples than the window — just take the last windowSize samples
            std::copy_n(data + numSamples - windowSize, windowSize, slidingWindow.begin());
            writePos = 0;
            return;
        }

        // Shift existing samples left to make room
        const int remaining = windowSize - numSamples;
        std::copy(slidingWindow.begin() + numSamples,
                  slidingWindow.end(),
                  slidingWindow.begin());

        // Write new samples at the end
        std::copy_n(data, numSamples, slidingWindow.begin() + remaining);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BufferWriterThread)
};
