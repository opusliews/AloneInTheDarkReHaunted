#pragma once
// TatouScenarioStrict event trail (Research/Crash_2026-10-16_createAITD1Mask.md): the last
// ~2000 gameplay events (inputs, LIFE actions, animations, hits, items, rooms, sounds, saves,
// HD loads, background jobs) in a fixed in-memory ring, written into crash_log.txt when the
// strict build stops. No heap use, so it still works on a corrupted heap; the ring is also in
// the dump (g_eventTrail). Other builds compile the TRAIL() calls out.

#ifdef AITD_HEAP_STRICT
void eventTrail(const char* fmt, ...);
void eventTrailNextFrame();
long long eventTrailFrame();
#define TRAIL(...) eventTrail(__VA_ARGS__)
#else
#define TRAIL(...) ((void)0)
#endif
