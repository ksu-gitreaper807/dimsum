// SPDX-License-Identifier: MIT
#include "adaptivebrightness.h"
#include <cstdlib>
#include <iostream>
#include <limits>

static void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using DimSum::AdaptiveBrightness;
    check(std::abs(DimSum::steppedThreshold(0.12, -1) - 0.11) < 1e-12, "Comma lowers threshold by 0.01");
    check(std::abs(DimSum::steppedThreshold(0.12, 1) - 0.13) < 1e-12, "Period raises threshold by 0.01");
    check(DimSum::steppedThreshold(0.01, -1) == 0.01, "Threshold lower bound holds");
    check(DimSum::steppedThreshold(1, 1) == 1, "Threshold upper bound holds");
    AdaptiveBrightness thresholdControl;
    thresholdControl.observe(1, 0.12, 0.8);
    const double initialTarget = thresholdControl.target();
    thresholdControl.observe(1, DimSum::steppedThreshold(0.12, 1), 0.8, true);
    check(thresholdControl.target() > initialTarget, "Period immediately retargets toward less dimming, despite deadband");
    check(thresholdControl.gain() == 1, "Threshold key preserves smooth transitions");
    thresholdControl.observe(1, DimSum::steppedThreshold(0.12, -1), 0.8, true);
    check(thresholdControl.target() < initialTarget, "Comma retargets toward more dimming");
    AdaptiveBrightness a;
    check(a.gain() == 1, "Starts at manual ceiling");
    a.observe(0.02, 0.12, 0.8);
    check(a.target() == 1, "Dark page never gets boosted");
    a.observe(1, 0.12, 0.8);
    check(a.target() < 0.5 && a.target() >= 0.25, "White page receives additional dimming");
    double previous = 1;
    for (int i = 0; i < 200; ++i) {
        const double gain = a.advance(0.016);
        check(gain <= previous && gain >= a.target(), "Dimming converges without overshoot");
        check(previous - gain < 0.05, "No abrupt step at normal frame rate");
        check(0.6 * gain <= 0.6, "Manual brightness is always the ceiling");
        previous = gain;
    }
    check(!a.settling(), "White page settles");
    const double whiteGain = a.gain();
    const double whiteMean = std::pow(whiteGain, 2.2);
    check(whiteMean < 0.3, "Reduces the light/dark page brightness gap");
    // Measure only the original scene, not the dimmed result; a constant
    // white page must not oscillate back towards manual brightness.
    a.observe(1, 0.12, 0.8);
    check(a.advance(0.016) == whiteGain, "Stable white content does not pump");
    a.observe(1, 0.2, 0.8);
    check(a.gain() == whiteGain && a.target() > whiteGain, "Settings retarget without flashing back to manual brightness");
    a.observe(1, 0.12, 0.8);
    a.observe(0, 0.12, 0.8);
    for (int i = 0; i < 500; ++i) {
        const double gain = a.advance(0.016);
        check(gain >= previous && gain <= 1, "Dark page recovery is monotonic and bounded");
        previous = gain;
    }
    check(a.gain() == 1, "Dark page recovers to manual, never beyond");
    a.observe(1, 0.12, 0);
    check(a.target() == 1, "Zero strength disables adaptation");
    a.observe(1, 0.12, 1);
    const double target = a.target();
    a.observe(0.99, 0.12, 1);
    check(a.target() == target, "Hysteresis suppresses small measurement noise");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (double invalid : { nan, -1.0, 2.0, std::numeric_limits<double>::infinity() }) {
        a.observe(invalid, 0.12, 1);
        check(a.target() == target, "Invalid samples are ignored");
    }
    a.observe(1, 0, 1);
    a.observe(1, nan, 1);
    a.observe(1, 0.12, nan);
    check(a.target() == target, "Invalid settings are ignored");
    check(a.advance(nan) == a.gain(), "Invalid frame timing is ignored");
    a.reset();
    check(a.gain() == 1 && !a.settling(), "Reset clears history");
    AdaptiveBrightness other;
    a.observe(1, 0.12, 0.8);
    a.advance(0.1);
    check(other.gain() == 1, "Displays have independent histories");
    std::cout << "Adaptive controller checks passed\n";
}
