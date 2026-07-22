#pragma once

#include <cstddef>

// RAII page-lock: pins a memory range into the process working set (VirtualLock)
// so the audio thread never page-faults touching it.
//
// Best-effort: if the OS refuses (e.g. working-set quota), we run unlocked;
// residency is an optimisation, not correctness. We don't enlarge the working set
// to force it, since this runs as a guest in the host (DAW) process. Non-copyable,
// movable; unlocks on destruction.
//
// Implementation is in PageLock.cpp to keep <windows.h> out of every TU that
// includes the processor.
class PageLock
{
public:
    PageLock() = default;
    PageLock(void* addr, const std::size_t bytes) { lock(addr, bytes); }
    ~PageLock() { unlock(); }

    PageLock(const PageLock&)            = delete;
    PageLock& operator=(const PageLock&) = delete;

    PageLock(PageLock&& other) noexcept;
    PageLock& operator=(PageLock&& other) noexcept;

    // (Re)lock a range, unlocking any currently held first.
    void lock(void* addr, std::size_t bytes);

    // Unlock the current range if locked. Safe to call repeatedly.
    void unlock() noexcept;

    [[nodiscard]] bool isLocked() const noexcept { return locked; }

private:
    void* address    = nullptr;
    std::size_t size = 0;
    bool locked      = false;
};
