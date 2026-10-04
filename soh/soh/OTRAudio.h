#pragma once
#include <atomic>
#include <thread>
#include <condition_variable>

struct OTRAudioState {
    std::thread thread;
    std::condition_variable cv_to_thread;
    std::mutex mutex;
    std::atomic_bool running;
    std::atomic_bool processing;
};

inline OTRAudioState audio;
