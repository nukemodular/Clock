# Workflow Templates

This folder contains stripped reusable templates extracted from the current project structure.

Goal:
- keep only the essential pattern-storage and recall flow
- keep host-sync and shuffle timing independent from the TR-808 specifics
- make it easy to copy the logic into future JUCE plugin projects

Files:
- `PatternStoreTemplate.h` / `PatternStoreTemplate.cpp`: single-source-of-truth pattern storage using `juce::ValueTree`
- `HostSyncShuffleTemplate.h`: minimal host PPQ to step-boundary enumerator with offbeat shuffle offsets
- `JucePlayheadHostSyncTemplate.h`: JUCE playhead adapter that builds host blocks from `AudioPlayHead`
- `RuntimeSequencerTemplate.h`: compact runtime cache that turns stored patterns into scheduled trigger callbacks
- `SlotControllerTemplate.h`: reusable slot-selection, store, clear, and occupancy helper
- `EditorIntegrationTemplate.md`: minimal glue for autosave-on-edit, slot recall, and plugin state persistence
- `LicenseManagerTemplate.h` / `LicenseManagerTemplate.cpp`: reusable local license-file storage plus machine fingerprint validation
- `LicenseDialogTemplate.h` / `LicenseDialogTemplate.cpp`: full-screen activation overlay with async validator hook
- `LicensingIntegrationTemplate.md`: processor/editor/CMake wiring, overlay flow, Gumroad example, and asset checklist
- `GumroadLicenseValidatorTemplate.h` / `GumroadLicenseValidatorTemplate.cpp`: provider-specific async validator factory for Gumroad
- `assets/LicenseCardScaffold.svg`: neutral vector card scaffold for registration overlays
- `MinimumLicensingTemplate.md`: smallest useful licensing setup with minimal processor/editor wiring

Core rules worth keeping in future projects:
- Treat the pattern store as the authoritative editable state.
- Never read `juce::ValueTree` directly from the audio thread.
- Copy the selected pattern into a plain runtime cache before sequencing.
- Slot occupancy should be based on note content only, not metadata-only edits.
- Host sync should convert PPQ to discrete step boundaries first, then apply shuffle as a sample offset on offbeats.

Suggested import order:
1. Add the pattern store.
2. Add a runtime sequencer state that mirrors one selected pattern.
3. Wire editor callbacks so every edit writes immediately into the store.
4. Wire slot selection so it swaps the current pattern and reapplies runtime state immediately.
5. Add host-sync step enumeration and shuffle offset logic.

Project source references used to derive these templates:
- `Source/Utils/PatternManager.h`
- `Source/Utils/PatternManager.cpp`
- `Source/PluginEditor_construct.cpp`
- `Source/PluginEditor_pages.cpp`
- `Source/PluginProcessor.cpp`
- `Source/Utils/ShuffleTiming.h`
- `Source/Utils/LicenseManager.h`
- `Source/Utils/LicenseManager.cpp`
- `Source/Components/LicenseDialog.h`
- `Source/Components/LicenseDialog.cpp`
- `Source/PluginEditor.cpp`
- `LICENSE_SYSTEM_SETUP.md`