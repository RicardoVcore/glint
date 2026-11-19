#pragma once

#include <cstddef>

namespace glint
{

/** Number of factory presets. */
constexpr int numPresets = 10;

/**
    One factory preset.

    The knob values are NOT copied from any hardware unit: each preset is
    defined by a target gain reduction (targetGrDb) plus the time constant,
    link mode, sparkle and mix settings.  The input/threshold/output values
    below were MEASURED on the reference signal (see tests/PresetCalibrate.cpp)
    so that the engine's mean gain reduction over the measurement window hits
    the target within +/- 0.25 dB, and the processed RMS is loudness-matched
    to the input RMS.

    timeMode is the detector time-constant position, 1..6.
    midSide: false = stereo linked, true = mid/side.
*/
struct Preset
{
    const char* name;
    int timeMode;          // 1..6
    bool midSide;          // false = stereo linked, true = mid/side
    float sparklePct;      // 0..100
    float mixPct;          // 0..100
    float targetGrDb;      // design target for the measured gain reduction
    float inputDb;         // calibrated input (dB)
    float thresholdKnob;   // calibrated threshold knob (0..10)
    float outputDb;        // calibrated output (dB, loudness match)
};

    // Factory preset calibration — measured 2026-11-19.
    // Reference signal: stereo Voss-McCartney pink noise (12 rows, fixed
    // seeds), -18 dBFS RMS per channel, L/R correlation 0.7, 48 kHz, 20 s;
    // mean gain reduction and RMS measured over the last 10 s.
    inline constexpr Preset factoryPresets[numPresets]
    {
    { "Init",                 2,  false, 30.0f, 100.0f, 0.0f,  0.0f,  3.00f,   0.00f },
    { "Vocal Velvet",         3,  false, 35.0f, 100.0f, 4.0f,  0.0f,  2.88f,   4.19f },
    { "Vocal Upfront",        2,  false, 45.0f, 100.0f, 8.0f,  0.0f,  3.52f,   8.15f },
    { "Sixties Lead",         2,  false, 20.0f, 100.0f, 6.0f,  0.0f,  2.70f,   6.17f },
    { "Drum Smash (parallel)", 1,  false, 50.0f, 35.0f, 12.0f,  0.0f,  5.38f,   2.70f },
    { "Drum Bus Glue",        2,  false, 25.0f, 100.0f, 3.0f,  0.0f,  1.45f,   3.18f },
    { "Bass Hold",            4,  false, 10.0f, 100.0f, 7.0f,  0.0f,  4.19f,   7.26f },
    { "Acoustic Sheen",       3,  false, 40.0f, 100.0f, 3.0f,  0.0f,  2.43f,   3.20f },
    { "Mix Bus Glue",         5,  false, 20.0f, 100.0f, 2.0f,  0.0f,  1.95f,   2.25f },
    { "Master M/S",           6,  true, 15.0f, 100.0f, 1.5f,  0.0f,  1.49f,   1.44f },
    };

} // namespace glint
