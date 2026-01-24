#ifndef TIMER_H
#define TIMER_H

#include <cstdint>

namespace server {

/**
 * @brief Timer using timerfd
 */
class Timer {
public:
    Timer();
    ~Timer();

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    /**
     * @brief Create and start periodic timer
     * @param intervalMs interval in milliseconds
     * @return true on success
     */
    bool Start(uint32_t intervalMs);

    /**
     * @brief Stop timer
     */
    void Stop();

    /**
     * @brief Get timer file descriptor
     */
    int32_t GetFd() const { return timerFd_; }

    /**
     * @brief Read timer expiration count
     * @return number of expirations since last read
     */
    uint64_t Read();

private:
    int32_t timerFd_;
};

}  // namespace server

#endif  // TIMER_H
