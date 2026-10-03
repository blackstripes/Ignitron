#pragma once

enum class SparkSubmission { Sent, Busy, Failed };

// A busy command was not sent. Keep the caller's queued intent and pending UI;
// only an attempted, unsuccessful BLE write follows the failure path.
inline bool keepQueuedSparkIntent(SparkSubmission result) {
    return result == SparkSubmission::Busy;
}

inline bool refreshLooperAfterStop(bool firstStopSent) {
    return firstStopSent;
}

// Legacy button handlers have no pending action state. Never imply a busy
// one-shot was sent; the user must press again (no unsafe composite replay).
inline const char *legacyLooperButtonFailure(SparkSubmission result) {
    return result == SparkSubmission::Busy ?
        "Spark looper busy; button action not sent. Press again." :
        "Spark looper button action not sent; press again after recovery.";
}
