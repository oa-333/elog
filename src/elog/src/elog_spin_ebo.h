#ifndef __ELOG_SPIN_EBO_H__
#define __ELOG_SPIN_EBO_H__

#include <cstdint>
#include <thread>

#include "elog_def.h"

namespace elog {

// spin and exponential back-off for busy-wait loops
#define EBO_INIT_SPIN_COUNT 256
#define EBO_SPIN_FACTOR 2
#define EBO_MAX_SPIN_COUNT 16384
#define EBO_INIT_SLEEP_MICROS 1
#define EBO_SLEEP_FACTOR 2
#define EBO_MAX_SLEEP_MICROS 1024

class ELogSpinEbo {
public:
    ELogSpinEbo(uint64_t initSpinCount = EBO_INIT_SPIN_COUNT, uint64_t spinFactor = EBO_SPIN_FACTOR,
                uint64_t maxSpinCount = EBO_MAX_SPIN_COUNT,
                uint64_t initSleepMicros = EBO_INIT_SLEEP_MICROS,
                uint64_t sleepFactor = EBO_SLEEP_FACTOR,
                uint64_t maxSleepMicros = EBO_MAX_SLEEP_MICROS)
        : m_initSpinCount(initSpinCount),
          m_spinFactor(spinFactor),
          m_maxSpinCount(maxSpinCount),
          m_initSleepMicros(initSleepMicros),
          m_sleepFactor(sleepFactor),
          m_maxSleepMicros(maxSleepMicros),
          m_spinCount(0),
          m_backOffSleepMicros(initSleepMicros) {}
    ~ELogSpinEbo() {}

    inline void reset() {
        m_spinCount = m_initSpinCount;
        m_backOffSleepMicros = m_initSleepMicros;
    }

    inline void spinOrBackoff() {
        if (m_spinCount < m_maxSpinCount) {
            spin();
        } else {
            backoff();
        }
    }

private:
    uint64_t m_initSpinCount;
    uint64_t m_spinFactor;
    uint64_t m_maxSpinCount;
    uint64_t m_initSleepMicros;
    uint64_t m_sleepFactor;
    uint64_t m_maxSleepMicros;

    uint64_t m_spinCount;
    uint64_t m_backOffSleepMicros;

    inline void spin() {
        for (uint64_t i = 0; i < m_spinCount; ++i) {
            CPU_RELAX;
        }
        if (m_spinCount < m_maxSpinCount) {
            m_spinCount *= m_spinFactor;
        }
    }

    inline void backoff() {
        std::this_thread::sleep_for(std::chrono::microseconds(m_backOffSleepMicros));
        if (m_backOffSleepMicros < m_maxSleepMicros) {
            m_backOffSleepMicros *= m_sleepFactor;
        }
    }
};

}  // namespace elog

#endif  // __ELOG_SPIN_EBO_H__