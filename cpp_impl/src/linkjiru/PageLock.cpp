#include "PageLock.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

PageLock::PageLock(PageLock&& other) noexcept : address(other.address), size(other.size), locked(other.locked)
{
    other.address = nullptr;
    other.size    = 0;
    other.locked  = false;
}

PageLock& PageLock::operator=(PageLock&& other) noexcept
{
    if (this != &other)
    {
        unlock();
        address       = other.address;
        size          = other.size;
        locked        = other.locked;
        other.address = nullptr;
        other.size    = 0;
        other.locked  = false;
    }
    return *this;
}

void PageLock::lock(void* addr, const std::size_t bytes)
{
    unlock();
    address = addr;
    size    = bytes;
#ifdef _WIN32
    if (address != nullptr && size > 0)
    {
        locked = (VirtualLock(address, size) != 0);
    }
#endif
}

void PageLock::unlock() noexcept
{
#ifdef _WIN32
    if (locked && address != nullptr)
    {
        VirtualUnlock(address, size);
    }
#endif
    locked = false;
}
