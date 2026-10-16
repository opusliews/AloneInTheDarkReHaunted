// TatouScenarioStrict event trail: see eventTrail.h.
#ifdef AITD_HEAP_STRICT
#include "eventTrail.h"

#include <Windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace
{
const int TRAIL_SIZE = 2048;
const int TRAIL_DEDUP = 32;     // an event equal to one of the last 32 only bumps its count

struct TrailEntry
{
    long long frame, lastFrame;
    double time;
    unsigned long thread;
    int count;
    char text[108];
};
}

// Not static: easy to find in a dump.
TrailEntry g_eventTrail[TRAIL_SIZE];
long long g_eventTrailCount = 0;
static long long s_frame = 0;
static SRWLOCK s_lock = SRWLOCK_INIT;
static unsigned long s_gameThread = 0;

static double trailNow()
{
    static LARGE_INTEGER freq = {}, start = {};
    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start); }
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart - start.QuadPart) / double(freq.QuadPart);
}

void eventTrailNextFrame()
{
    s_gameThread = GetCurrentThreadId();
    s_frame++;
}

long long eventTrailFrame() { return s_frame; }

void eventTrail(const char* fmt, ...)
{
    char text[sizeof(TrailEntry::text)];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(text, sizeof(text), _TRUNCATE, fmt, args);
    va_end(args);

    const unsigned long thread = GetCurrentThreadId();
    AcquireSRWLockExclusive(&s_lock);
    for (int i = 1; i <= TRAIL_DEDUP && i <= g_eventTrailCount; i++)
    {
        TrailEntry& e = g_eventTrail[(g_eventTrailCount - i) % TRAIL_SIZE];
        if (e.thread == thread && strcmp(e.text, text) == 0)
        {
            e.count++;
            e.lastFrame = s_frame;
            ReleaseSRWLockExclusive(&s_lock);
            return;
        }
    }
    TrailEntry& e = g_eventTrail[g_eventTrailCount % TRAIL_SIZE];
    e.frame = e.lastFrame = s_frame;
    e.time = trailNow();
    e.thread = thread;
    e.count = 1;
    memcpy(e.text, text, sizeof(text));
    g_eventTrailCount++;
    ReleaseSRWLockExclusive(&s_lock);
}

// Called by the strict stop (exceptionHandler.h). Does not take the lock: the stopping
// thread may have been interrupted while holding it.
void eventTrailWrite(FILE* f)
{
    long long n = g_eventTrailCount < TRAIL_SIZE ? g_eventTrailCount : TRAIL_SIZE;
    fprintf(f, "Event trail: last %lld of %lld events (frame, seconds, thread: game = g, other = id):\n",
        n, g_eventTrailCount);
    for (long long i = g_eventTrailCount - n; i < g_eventTrailCount; i++)
    {
        const TrailEntry& e = g_eventTrail[i % TRAIL_SIZE];
        char thread[16];
        if (e.thread == s_gameThread) strcpy_s(thread, "g");
        else sprintf_s(thread, "%lu", e.thread);
        if (e.count > 1)
            fprintf(f, "  %7lld %9.3f %6s  %s  (x%d, until frame %lld)\n", e.frame, e.time, thread, e.text, e.count, e.lastFrame);
        else
            fprintf(f, "  %7lld %9.3f %6s  %s\n", e.frame, e.time, thread, e.text);
    }
}
#endif
