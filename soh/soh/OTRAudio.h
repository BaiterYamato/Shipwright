#pragma once
#include <thread>
#include <condition_variable>

// SOH [Link-Span] One instance for the whole program (inline): with `static` every .cpp had its own copy, and the
// savestate code locked a mutex the audio thread never saw (RFC 0027, patch 12). Upstream 9.3.0 still uses static.
struct OTRAudioState {
    std::thread thread;
    std::condition_variable cv_to_thread, cv_from_thread;
    std::mutex mutex;
    bool running;
    bool processing;
};

inline OTRAudioState audio;
