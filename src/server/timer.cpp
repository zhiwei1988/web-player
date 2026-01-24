#include "timer.h"

#include <sys/timerfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace server {

Timer::Timer() : timerFd_(-1) {
}

Timer::~Timer() {
    Stop();
}

bool Timer::Start(uint32_t intervalMs) {
    timerFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timerFd_ < 0) {
        std::fprintf(stderr, "Failed to create timerfd: %s\n", std::strerror(errno));
        return false;
    }

    struct itimerspec ts;
    ts.it_value.tv_sec = intervalMs / 1000;
    ts.it_value.tv_nsec = (intervalMs % 1000) * 1000000;
    ts.it_interval.tv_sec = intervalMs / 1000;
    ts.it_interval.tv_nsec = (intervalMs % 1000) * 1000000;

    if (timerfd_settime(timerFd_, 0, &ts, nullptr) < 0) {
        std::fprintf(stderr, "Failed to set timer: %s\n", std::strerror(errno));
        close(timerFd_);
        timerFd_ = -1;
        return false;
    }

    return true;
}

void Timer::Stop() {
    if (timerFd_ >= 0) {
        close(timerFd_);
        timerFd_ = -1;
    }
}

uint64_t Timer::Read() {
    uint64_t expirations = 0;
    ssize_t s = read(timerFd_, &expirations, sizeof(expirations));
    if (s != sizeof(expirations)) {
        return 0;
    }
    return expirations;
}

}  // namespace server
