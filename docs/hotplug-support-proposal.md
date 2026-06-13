# Hotplug Disk Wiping Proposal

## Summary

Add an optional Linux hotplug mode that watches for newly inserted block devices and applies the configured wipe method only to devices that appear after the mode is armed.

The goal is to support workflows where disks are inserted one at a time into a wipe station, and nwipe should automatically pick up each new disk without rescanning the whole system or requiring manual selection.

This proposal keeps the existing startup enumeration and wipe engine intact as much as possible. The main change is to introduce a device event monitor, a dynamic device registry, and a job admission path that can turn newly discovered devices into wipe contexts at runtime.

## Implementation Status

This document started as the design proposal, and the first pass of the work now exists in the tree. The checklist below reflects the current state of implementation.

### Done

- [x] Add an opt-in `--hotplug` CLI flag.
- [x] Make hotplug imply `--autonuke`, `--nogui`, and `--nowait`.
- [x] Ignore disks that were already present when hotplug mode started.
- [x] Add a runtime hotplug entry point in the main startup path.
- [x] Add a dedicated hotplug source file and wire it into the build.
- [x] Reuse the existing device validation and wipe pipeline where possible.
- [x] Reuse the existing logging path so hotplug activity lands in the normal log file / log stream.
- [x] Add a pending state so newly detected disks must survive one more poll before admission.
- [x] Add unit tests for hotplug basename, partition detection, and promotion policy helpers.
- [x] Document the mode in the help text, man page, README, and proposal.
- [x] Verify the tree builds and `make check` passes in Docker.

### Still to do

- [ ] Replace the polling-based watcher with a `libudev` monitor on Linux.
- [ ] Add a more stable device identity layer for hotplug bookkeeping.
- [ ] Split out reusable device-validation helpers so hotplug does not depend on `check_device()` as a monolith.
- [ ] Add queueing / back-pressure controls for multiple newly inserted disks.
- [ ] Define and implement device removal semantics during pending and active wipes.
- [ ] Decide whether hotplug stays CLI-only or also updates the GUI live device list.
- [ ] Add targeted tests for duplicate filtering, baseline filtering, and device rejection paths.
- [ ] Add integration tests that exercise device insertion and removal with loop / dm-backed block devices.

## Problem Statement

Today nwipe performs a one-time device scan during startup, then builds a fixed list of devices and starts wiping the selected set. That model works for manual batch wipes, but it does not support a workflow where:

1. nwipe starts first,
2. the wipe method is already configured,
3. disks are inserted later,
4. only those newly inserted disks should be wiped automatically.

The current design has several constraints that block this use case:

- Device discovery is static and synchronous.
- The selected wipe list is fixed before the wipe threads start.
- The GUI and non-GUI flows assume the device count does not change.
- Device identity is mostly derived from paths like `/dev/sdX`, which are not stable enough for hotplug bookkeeping.

## Goals

- Allow nwipe to run in a “watch for new disks” mode.
- Automatically wipe only disks inserted after arming the hotplug mode.
- Preserve the existing method, PRNG, logging, and safety checks.
- Reuse existing device validation logic as much as possible.
- Keep hotplug optional and off by default.
- Support both CLI and GUI modes, with CLI-first implementation acceptable.

## Non-Goals

- Replacing the current startup scan path.
- Supporting non-Linux hotplug APIs.
- Automatically wiping every disk currently connected at program start.
- Changing wipe algorithms or I/O semantics.
- Solving all GUI redesign issues in the first patch.

## Proposed User Experience

### CLI mode

Example flow:

```bash
nwipe --hotplug --method=prng --prng=chacha20
```

Behavior:

- nwipe starts and snapshots currently present block devices.
- It enters a watch loop.
- A disk inserted after startup is detected.
- The device is validated against current safety rules.
- If accepted, it is wiped automatically using the configured method.
- Disks present before arming are ignored unless an explicit “include baseline” mode is added later.

Optional future extensions:

- `--hotplug-once` to exit after the first inserted disk is wiped.
- `--hotplug-serial` to only accept devices matching a serial or path pattern.
- `--hotplug-queue=N` to cap the number of pending hotplug jobs.

### GUI mode

Two reasonable options:

1. Minimal first pass:
   - hotplug mode is CLI-only.
   - GUI remains unchanged.
2. Full integration:
   - add a live “devices discovered” view.
   - newly inserted devices appear in the selection/status list.
   - auto-selected devices start wiping without manual selection.

The recommended path is to ship CLI hotplug first, then add GUI integration once the core event and registry logic is stable.

## Design Overview

The current code path should be split into three responsibilities:

1. Baseline discovery
   - snapshot the devices visible when nwipe starts hotplug mode.
   - store stable identifiers for later comparison.

2. Event monitoring
   - listen for block device add/remove events.
   - resolve events to stable device identities.
   - filter out baseline devices.

3. Admission and wiping
   - validate the candidate against current policy.
   - create a new `nwipe_context_t`.
   - enqueue or launch the wipe thread.

## Recommended Linux Implementation

Use `libudev` on Linux for the eventual production implementation. The first implementation can use a polling loop over the existing block-device inventory to keep the dependency surface unchanged.

Why `libudev`:

- It is the standard userspace interface for device add/remove events.
- It maps naturally to block subsystem events.
- It can resolve symlinks and sysfs metadata.
- It is more direct than polling `/proc/partitions`.

Why not polling as the final design:

- Polling `/proc/partitions` is simple but brittle.
- It produces latency and unnecessary rescans.
- It can miss transient state or create duplicate admissions if not carefully de-duplicated.

Why polling is acceptable for the first patch:

- It avoids adding a new dependency before the feature is proven.
- It reuses the current libparted discovery path.
- It is enough to validate the admission, logging, and lifecycle model.

## Core Data Model Changes

Introduce a runtime hotplug state structure, separate from the current startup arrays.

Suggested fields:

- `enabled`
- `baseline_snapshot`
- `seen_devices`
- `pending_jobs`
- `active_jobs`
- `stop_requested`
- `monitor_thread`

Suggested device identity keys, in order of preference:

1. `dev_t` major/minor number
2. sysfs device path
3. stable `/dev/disk/by-id/*` path if available
4. serial number plus model as fallback

The important point is that `/dev/sdX` must not be the primary key, because it can be reused after unplug/replug.

## Admission Rules

When a new block device appears, it should be accepted only if all of the following are true:

- It was not present in the startup baseline.
- It resolves to a whole block device, not a partition, unless partitions are explicitly supported later.
- It is not excluded by `--exclude`.
- It is not rejected by `--nousb`.
- It is not currently mounted or otherwise in use unless `--force` is set.
- It can be opened and validated by the existing device probing logic.
- It does not duplicate an already active or previously wiped device identity.

The existing checks in `check_device()` should be reused where practical, but some of them will need to be split so they can be called on-demand from the hotplug path without depending on the startup array layout.

## Proposed Refactor

### 1. Extract device validation into reusable helpers

Today `check_device()` does discovery, filtering, metadata collection, and context allocation in one path.

Refactor it into smaller units:

- `nwipe_device_is_excluded()`
- `nwipe_device_is_usb()`
- `nwipe_device_is_supported_block_device()`
- `nwipe_device_build_context()`
- `nwipe_device_validate_for_wipe()`

This will let the hotplug monitor reuse the same policy without duplicating logic.

### 2. Add a hotplug monitor thread

Add a dedicated thread that:

- initializes a `udev_monitor` for `block` events,
- polls for `add` and `remove`,
- translates each event into a device candidate,
- applies baseline/duplicate filtering,
- forwards accepted devices to a dispatcher.

### 3. Add a dispatcher / queue

The dispatcher can be lightweight:

- one mutex-protected queue of accepted contexts,
- main thread or worker thread drains the queue,
- a new wipe thread is created per accepted device.

This keeps admission separate from wiping and avoids blocking the monitor on slow device validation.

### 4. Make the device list dynamic

The current `c1` / `c2` fixed arrays are built once from the initial enumeration. Hotplug mode needs a dynamic list, so one of these approaches is needed:

1. Keep the static arrays for startup devices and add a separate hotplug job path.
2. Replace the fixed arrays with a dynamic registry shared by GUI, monitor, and wipe threads.

For a first implementation, option 1 is lower risk:

- keep existing startup flow untouched,
- add a separate hotplug job list only when `--hotplug` is enabled,
- do not require GUI selection support immediately.

### 5. Add a new runtime mode flag

Add a new option in `nwipe_options_t`, for example:

- `hotplug`
- `hotplug_ignore_existing`
- `hotplug_queue_only`

The exact naming can be finalized later, but the mode must be explicit and opt-in.

## Suggested Phased Implementation

### Phase 1: CLI hotplug, no GUI integration

Scope:

- add `--hotplug`
- detect baseline devices at startup
- watch for new block devices
- wipe accepted devices automatically
- reuse existing logging and safety checks

Deliverables:

- library and build changes for `libudev`
- hotplug monitor thread
- hotplug registry
- admission queue
- docs and manpage updates
- basic tests for duplicate filtering and device filtering logic

### Phase 2: Improve lifecycle handling

Scope:

- support device removal during pending and active jobs
- add explicit status events for “new device detected”, “accepted”, “rejected”, “removed”
- define shutdown behavior clearly

Potential additions:

- stop accepting new disks after a threshold
- wait for current wipe to finish or abort immediately on shutdown
- configurable queue size

### Phase 3: GUI integration

Scope:

- live device list updates in curses UI
- visual distinction between baseline and newly inserted devices
- hotplug status indicator
- optional user confirmation before wipe if desired

## Shutdown and Removal Semantics

This needs to be defined carefully before implementation.

Recommended default behavior:

- If a device is removed before wiping starts, drop the queued job.
- If a device is removed during wiping, the wipe thread should fail naturally and record the error.
- On program shutdown, stop accepting new devices immediately.
- Existing wipe threads should be handled by the current cancellation logic.

This matches the principle of not hiding hardware removal or making assumptions that the disk is still present.

## Safety Considerations

Hotplug automatically increases the risk profile, so there should be explicit guardrails:

- Mode must be opt-in.
- Baseline devices must be ignored.
- Exclusion rules must still apply.
- Mounted or busy devices should remain blocked unless `--force` is used.
- Whole-disk validation should be conservative.
- Logging should clearly state when a device was accepted because it appeared after arming hotplug.

I would not recommend enabling hotplug implicitly under `--autonuke`; the user should have to request it explicitly.

## Dependency and Build Impact

Expected build changes:

- `configure.ac`
  - detect `libudev`
  - fail cleanly or disable hotplug support if the library is unavailable
- `src/Makefile.am`
  - add new source files for hotplug monitoring and registry management
  - link against `libudev`
- man page and README
  - document the new mode and its limitations

If the project wants to keep hotplug optional at compile time, the code should be guarded so nwipe still builds without `libudev`, with the feature disabled.

## Testing Plan

### Unit tests

Add tests for pure logic where possible:

- baseline device identity comparison
- exclude matching
- duplicate admission filtering
- partition-vs-whole-disk filtering

### Integration tests

Add a Linux-only test harness that can simulate hotplug-like behavior:

- loop device creation/removal
- device-mapper or sparse image-backed block devices
- event-monitor smoke test if test infrastructure supports `udev`

### Manual validation

Recommended manual test cases:

1. Start nwipe in hotplug mode with no disks inserted.
2. Insert one target disk.
3. Verify exactly one wipe job starts.
4. Insert a second disk while the first is already running.
5. Verify the second disk is queued or started according to the chosen policy.
6. Insert a disk that was already present at startup.
7. Verify it is ignored.
8. Insert a USB device when `--nousb` is set.
9. Verify it is ignored.
10. Remove a disk before wiping begins.
11. Verify the queued job is dropped.

## Risks

### Race conditions

The hardest problem is device lifecycle races:

- add event arrives before sysfs metadata is fully populated,
- device is present but not yet openable,
- device disappears while being validated,
- `/dev/sdX` is reused after disconnect.

This is why the design needs stable identity tracking and retry/debounce behavior.

### GUI complexity

Dynamic device insertion is much harder in ncurses than in a static list. The GUI should not be the first target unless the runtime registry is already robust.

### Safety regression

Hotplug makes the tool more powerful and therefore more dangerous. If admission logic is too permissive, it could wipe the wrong disk after device renaming or transient attach events.

### Compatibility

Some environments may not have `libudev` development headers installed. The build should either:

- make hotplug optional, or
- fail with a clear error message when hotplug is requested but unsupported.

## Open Questions

1. Should hotplug mode wipe immediately on insertion, or require a confirmation gate in GUI mode?
2. Should only whole disks be accepted, or should partitions be optionally supported?
3. Should a disk inserted before hotplug is armed be ignored forever, or can there be a manual “refresh baseline” action?
4. Should multiple inserted disks be wiped in parallel or serialized?
5. Should hotplug be available in `--nogui` mode only at first, or also in the GUI from the start?
6. Should removal of an active disk trigger immediate cancellation, or just let the wipe fail on I/O?

## Recommended First Implementation

If the goal is to minimize risk and get useful functionality quickly, the best first release is:

- Linux-only
- `libudev`-based
- CLI-only
- opt-in via `--hotplug`
- ignore startup baseline devices
- only accept whole disks
- reuse current wipe methods and safety checks
- queue newly inserted disks and wipe them automatically

That gives the requested behavior without forcing a large GUI rewrite in the first patch.

## Conclusion

Hotplug support is feasible, but it is not a one-line feature. The wipe engine can mostly be reused, but the device lifecycle model needs to change from static enumeration to dynamic admission.

The least risky path is to:

1. add an explicit hotplug mode,
2. implement a Linux `libudev` monitor,
3. create a stable device registry and admission queue,
4. keep the existing wipe pipeline unchanged,
5. defer GUI integration until the core model is proven.
