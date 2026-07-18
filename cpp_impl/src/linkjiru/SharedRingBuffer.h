#pragma once

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <vector>

template <int Capacity> class SharedRingBuffer
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    // write() must never block. Require a lock-free counter so an unusual target
    // fails to compile rather than putting a mutex on the audio thread.
    static_assert(std::atomic<uint64_t>::is_always_lock_free,
                  "writeCount must be lock-free: a lock-based atomic would make write() block");

    static constexpr uint64_t Mask = static_cast<uint64_t>(Capacity) - 1;

public:
    // Write samples. Wait-free; single-writer (the audio thread). numSamples may
    // exceed Capacity: indices wrap, only the last Capacity samples are kept,
    // and writeCount advances by the full numSamples.
    void write(const float* data, const int numSamples)
    {
        const auto pos = writeCount.load(std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            buffer[static_cast<std::size_t>((pos + i) & Mask)] = data[i];
        }
        writeCount.store(pos + numSamples, std::memory_order_release);
    }

    // Snapshot the most recent n samples into dest, right-aligned and zero-padded
    // at the front if fewer than n exist. n is clamped to Capacity.
    //
    // Coherent under concurrent writes: after copying we re-read the counter and
    // confirm the writer hasn't reached start + Capacity (overwriting the oldest
    // slot we read). If it has, retry up to maxRetries. Returns true on a coherent
    // snapshot; false if every attempt was lapped, leaving the last (torn) attempt
    // in dest for the caller to discard. Reader-side only, any non-audio thread.
    bool readLastN(float* dest, int n, const int maxRetries = 4) const
    {
        if (n > Capacity)
        {
            n = Capacity;
        }

        for (int attempt = 0; attempt < maxRetries; ++attempt)
        {
            const auto before = writeCount.load(std::memory_order_acquire);

            if (before == 0)
            {
                std::fill_n(dest, n, 0.0f);
                return true;
            }

            uint64_t start;
            int toRead;

            if (before >= static_cast<uint64_t>(n))
            {
                start  = before - n;
                toRead = n;
            }
            else
            {
                start  = 0;
                toRead = static_cast<int>(before);
                std::fill_n(dest, n - toRead, 0.0f);
            }

            copyOut(dest + (n - toRead), start, toRead);

            // Order the copy before the verifying re-read, then confirm the writer
            // hasn't overwritten the oldest slot we just read.
            std::atomic_thread_fence(std::memory_order_acquire);
            if (writeCount.load(std::memory_order_acquire) - start <= static_cast<uint64_t>(Capacity))
            {
                return true;
            }
        }

        return false;
    }

    struct ReadResult
    {
        int samplesRead = 0;
        bool overrun    = false;
    };

    // Sequential read with a caller-managed cursor (each reader owns its own).
    // Sets overrun when the writer has lapped the cursor (more than Capacity
    // behind), jumping it forward to the oldest still-live sample.
    ReadResult read(float* dest, const int maxSamples, uint64_t& cursor) const
    {
        const auto currentWrite = writeCount.load(std::memory_order_acquire);

        bool overrun = false;
        if (currentWrite > cursor + static_cast<uint64_t>(Capacity))
        {
            cursor  = currentWrite - static_cast<uint64_t>(Capacity);
            overrun = true;
        }

        const auto available = currentWrite - cursor;
        const int toRead     = static_cast<int>(std::min(available, static_cast<uint64_t>(maxSamples)));

        copyOut(dest, cursor, toRead);

        cursor += toRead;
        return {toRead, overrun};
    }

    uint64_t getWriteCount() const { return writeCount.load(std::memory_order_acquire); }

    // Raw storage + byte size, exposed only so the processor can page-lock the
    // buffer (VirtualLock). Not a sample accessor; use readLastN()/read().
    void* storage() noexcept { return buffer.data(); }
    static constexpr std::size_t byteSize() noexcept { return sizeof(float) * static_cast<std::size_t>(Capacity); }

private:
    // Copy count samples from absolute index start into dest, wrapping through
    // the ring. Shared by readLastN() and read().
    void copyOut(float* dest, const uint64_t start, const int count) const
    {
        for (int i = 0; i < count; ++i)
        {
            dest[i] = buffer[static_cast<std::size_t>((start + i) & Mask)];
        }
    }

    // Heap-allocated (not an inline std::array): at 512 KB per ring, an inline
    // member would blow a 1 MB stack when the processor is stack-allocated (tests,
    // headless hosts). The data() pointer is stable; we never resize.
    std::vector<float> buffer = std::vector<float>(static_cast<std::size_t>(Capacity), 0.0f);

    // Total samples ever written, monotonic; wraps only after ~3 trillion years
    // at 192 kHz.
    std::atomic<uint64_t> writeCount{0};
};
