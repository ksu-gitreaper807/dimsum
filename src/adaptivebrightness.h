// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>

namespace DimSum
{
inline double steppedThreshold(double current, int steps)
{
    if (!std::isfinite(current)) {
        current = 0.12;
    }
    return std::clamp(current + steps * 0.01, 0.01, 1.0);
}

// Pure controller math: no screen capture, GL, Qt, or persisted transient state.
class AdaptiveBrightness
{
public:
    void reset() { m_gain = m_target = 1.0; }

    void observe(double mean, double threshold, double strength, bool forceRetarget = false)
    {
        if (!std::isfinite(mean) || mean < 0 || mean > 1 || !std::isfinite(threshold) || threshold <= 0 || threshold > 1
            || !std::isfinite(strength) || strength < 0 || strength > 1) {
            return;
        }
        // Manual dimming uses encoded RGB, so convert the linear-light ratio
        // into an approximate gamma-2.2 gain. Never brighten past manual gain.
        const double ratio = mean > threshold ? threshold / mean : 1.0;
        const double target = std::clamp(std::pow(ratio, strength / 2.2), 0.25, 1.0);
        // Hysteresis stops tiny sampling/scrolling variations from pumping.
        if (forceRetarget || std::abs(target - m_target) >= 0.015 || target == 1.0) {
            m_target = target;
        }
    }

    double advance(double seconds)
    {
        if (!std::isfinite(seconds) || seconds <= 0) {
            return m_gain;
        }
        // Reduce light faster than restoring it, but never step abruptly.
        const double tau = m_target < m_gain ? 0.25 : 0.8;
        m_gain += (m_target - m_gain) * -std::expm1(-std::min(seconds, 0.1) / tau);
        if (!settling()) {
            m_gain = m_target;
        }
        return m_gain;
    }

    bool settling() const { return std::abs(m_target - m_gain) > 0.001; }
    double gain() const { return m_gain; }
    double target() const { return m_target; }

private:
    double m_gain = 1.0;
    double m_target = 1.0;
};
}
