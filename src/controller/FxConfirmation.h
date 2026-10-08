#pragma once

// Keep all FX confirmation mutations ahead of serial output. The caller must
// capture any fields cleared by clearRequest before invoking this helper.
// The logger is deliberately last: even if it stalls or throws, the request
// has already been resolved and cannot be confirmed twice.
template <typename ConfirmState, typename RecordTelemetry, typename RecordPersistent,
          typename ClearRequest, typename Log>
void confirmFxRequest(ConfirmState confirmState, RecordTelemetry recordTelemetry,
                      RecordPersistent recordPersistent, ClearRequest clearRequest, Log log) {
    confirmState();
    recordTelemetry();
    recordPersistent();
    clearRequest();
    log();
}
