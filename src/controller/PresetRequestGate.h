#pragma once

// A refresh is still part of a preset selection's synchronization window.
// Only a fresh, idle controller requires Ready; while a full query is pending,
// a newer selection can supersede it even though the snapshot is Syncing.
inline bool presetSelectionMayProceedDuringSync(bool commandSent, bool targetQueued,
                                                 bool actionFullQueryIssued, bool startupFullQueryIssued,
                                                 bool fullPresetObservedForLink) {
    return commandSent || targetQueued || actionFullQueryIssued ||
           (startupFullQueryIssued && !fullPresetObservedForLink);
}
