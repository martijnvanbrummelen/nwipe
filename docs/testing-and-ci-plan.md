# Testing And CI Plan (Issue #719)

## Goal

Add reproducible runtime testing and a first deterministic unit-test module without large invasive refactoring.

## Design Principles

The CI harness follows the useful core ideas from existing manual loop tooling:

- use real loopback block devices (`losetup`)
- support targeted fault devices (`dmsetup ... error`)
- always clean up reliably (`trap`)

For CI stability, the harness deliberately avoids:

- interactive prompts (`read`, "press enter")
- self-escalation (`pkexec`, internal `sudo`)
- broad multipurpose command modes in one script

## Implemented In This Change

### CI Harness Scripts

- `tests/ci/loopback_e2e.sh`
  - headless integration tests for `zero`, `verify_zero`, `one`, `verify_one`
  - in `full` mode also runs `PRNG Stream` once per PRNG (`twister`, `isaac`, `isaac64`, `add_lagg_fibonacci_prng`,
    `xoroshiro256_prng`, `aes_ctr_prng` when AES-NI is available)
  - optional STS check per PRNG stream case (enabled in CI): parses `result.txt` pass ratio and enforces threshold
    (default `>= 0.9`)
  - streams `nwipe` stdout/stderr live into CI logs (while still writing per-case files)
  - asserts exit status + success log marker
  - verifies resulting data blocks (`0x00` / `0xFF`)
  - supports `full` and `smoke` modes

- `tests/ci/fault_injection.sh`
  - creates a mapped device with deterministic bad sectors via `dmsetup`
  - runs a wipe and asserts expected error outcome (no crash / no false success)

### CI Workflows

- `.github/workflows/ci_runtime.yml`
  - `unit-tests`: compiles and runs deterministic unit tests
  - `integration-loopback`: full loopback integration suite
  - `sanitizer-smoke`: ASan+UBSan build + short smoke suite

- `.github/workflows/ci_nightly_faults.yml`
  - nightly + manual fault-injection run
  - uploads logs as artifacts for failure analysis

### Unit-Test Entry Point

- `src/round_size.c` / `src/round_size.h`
  - extracted pure round-size computation helper
  - preserves existing behavior from `calculate_round_size(...)`

- `tests/unit/test_round_size.c`
  - deterministic tests for default/OPS2/IS5 combinations
  - verifies both effective pass-size and final round-size calculations
- `unit_nwipe_cli_smoke` (Meson test)
  - executes the real `nwipe` binary in CLI mode (`--autonuke --nogui`) via loopback smoke harness
  - provides a CLI-path smoke check as part of the unit-test stage

- `meson.build`
  - adds `unit_round_size` test target

## Why This Scope

`nwipe` is strongly tied to real block devices and runtime state.
This approach gives fast practical coverage first, then grows deterministic unit tests where extraction is low-risk.

## Next Logical Extensions

1. Add deterministic tests for method label mapping and selected option parsing.
2. Add further nightly fault patterns (multiple bad ranges, sparse failures).
3. Promote sanitizer job to strict required gate once flakiness remains low over time.
