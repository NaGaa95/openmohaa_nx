/*
===========================================================================
OpenMoHAA - Nintendo Switch (libnx) port

nx_heapguard.cpp

Guarded global operator new/delete (enabled with -DNX_HEAPGUARD). Each block
carries a small magic header; freed blocks are tagged and parked in a
quarantine ring instead of being returned to malloc immediately. This makes a
double free hit the preserved "freed" tag and get swallowed rather than crash
the allocator - which is what fixes the str double-free seen entering a
multiplayer game. Foreign/placement allocations (no magic) pass straight to
free(). Costs one header per allocation plus a deferred free; keep it enabled.

  Report() is a silenced hook left in place so heap-corruption logging can be
  switched back on for debugging without restructuring the allocator.
===========================================================================
*/

#ifdef NX_HEAPGUARD

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>

extern "C" void Com_Printf(const char *fmt, ...);

namespace {

constexpr uint64_t      kMagic    = 0xA110C8EDFEEDFACEull; // live block
constexpr uint64_t      kFreed    = 0xDEADBEEF0F0F0F0Full;  // quarantined block
constexpr unsigned char kTailByte = 0xBE;
constexpr size_t        kPrefix   = 32;   // multiple of 16 -> preserves alignment
constexpr size_t        kTail     = 64;   // absorbs/detects typical string overruns

struct Head {
    uint64_t magic;
    uint64_t size;
    void    *site;  // __builtin_return_address(0) of the allocating new
    uint64_t magic2;
};

// Lightweight spinlock - new/delete can run on worker threads and the quarantine
// ring is shared. No allocation here (would recurse).
volatile int g_lock = 0;
void Lock()   { while (__atomic_exchange_n(&g_lock, 1, __ATOMIC_ACQUIRE)) {} }
void Unlock() { __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE); }

// Silenced logging hook (see file header). Re-enable by printing what/h here.
void Report(const char *what, const Head *h, const char *extra)
{
    (void)what;
    (void)h;
    (void)extra;
}

// Quarantine ring: freed blocks are tagged and parked here, not returned to
// malloc immediately, so a double free still sees the original header and is
// swallowed. The oldest entry is really freed when the ring wraps (bounds leak).
constexpr size_t kQuar = 1u << 16; // 65536 deferred frees
void  *g_ring[kQuar];
size_t g_ringIdx = 0;

void *Galloc(size_t size, void *site)
{
    unsigned char *base = (unsigned char *)malloc(kPrefix + size + kTail);
    if (!base) {
        return nullptr;
    }
    Head *h   = (Head *)base;
    h->magic  = kMagic;
    h->size   = size;
    h->site   = site;
    h->magic2 = kMagic;
    memset(base + kPrefix + size, kTailByte, kTail);
    return base + kPrefix;
}

void Gfree(void *p)
{
    if (!p) {
        return;
    }
    unsigned char *base = (unsigned char *)p - kPrefix;
    Head          *h    = (Head *)base;

    // Already freed by us -> double free; swallow it to avoid the crash.
    if (h->magic == kFreed && h->magic2 == kFreed) {
        Report("DOUBLE FREE", h, "(freeing a block we already freed)");
        return;
    }

    // Not one of ours (aligned/placement/foreign), or the header was smashed.
    if (h->magic != kMagic || h->magic2 != kMagic) {
        free(p);
        return;
    }

    // Tail canary: detect a buffer overflow of this block.
    unsigned char *tail = base + kPrefix + h->size;
    for (size_t i = 0; i < kTail; i++) {
        if (tail[i] != kTailByte) {
            Report("buffer overflow", h, "(tail canary smashed)");
            break;
        }
    }

    h->magic  = kFreed;
    h->magic2 = kFreed;

    Lock();
    void *evict = g_ring[g_ringIdx];
    g_ring[g_ringIdx] = base;
    g_ringIdx = (g_ringIdx + 1) & (kQuar - 1);
    Unlock();

    if (evict) {
        free(evict); // a long-ago freed block; safe to really release now
    }
}

} // namespace

void *operator new(size_t s)
{
    void *p = Galloc(s, __builtin_return_address(0));
    if (!p) throw std::bad_alloc();
    return p;
}
void *operator new[](size_t s)
{
    void *p = Galloc(s, __builtin_return_address(0));
    if (!p) throw std::bad_alloc();
    return p;
}
void *operator new(size_t s, const std::nothrow_t &) noexcept   { return Galloc(s, __builtin_return_address(0)); }
void *operator new[](size_t s, const std::nothrow_t &) noexcept { return Galloc(s, __builtin_return_address(0)); }

void operator delete(void *p) noexcept                          { Gfree(p); }
void operator delete[](void *p) noexcept                        { Gfree(p); }
void operator delete(void *p, size_t) noexcept                  { Gfree(p); }
void operator delete[](void *p, size_t) noexcept                { Gfree(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept  { Gfree(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept{ Gfree(p); }

// Returns 1 iff p is a currently-live guarded allocation, 0 if null / already
// quarantined / not one of ours. Lets corepp refuse to `delete` an already-dead
// object before the virtual-destructor dispatch crashes (see usignal.cpp).
extern "C" int NX_HeapPtrLive(const void *p)
{
    if (!p) {
        return 0;
    }
    const Head *h = (const Head *)((const unsigned char *)p - kPrefix);
    return (h->magic == kMagic && h->magic2 == kMagic) ? 1 : 0;
}

#else // !NX_HEAPGUARD

// Guard compiled out: always "live" so callers delete normally.
extern "C" int NX_HeapPtrLive(const void *p)
{
    return p ? 1 : 0;
}

#endif // NX_HEAPGUARD
