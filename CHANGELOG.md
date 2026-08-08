# Changelog

All notable changes to Roadcast are recorded here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Roadcast versions the daemon separately from the wire protocol and the catalog
schema. A release line names all three.

## Unreleased

### Added

- GitHub Actions now publishes `RELEASE_MANIFEST.json` with Android target,
  protocol/schema compatibility, source commit, and hashes for the daemon,
  CLI, and shared client library.

### Changed

- Calibrated charger AC input measurements: `OBC_uInAct` reports voltage in
  `V`, and `OBC_iInAct` reports current in `A`; both use `scale=0.1` and
  `offset=0`. For example, `OBC_uInAct` raw value `2205` reports `220.5 V`.
- `ESC_VehicleSpeed` now reports its calibrated physical value in `km/h`, with
  `scale=1` and `offset=0`.
- `BMSH_BattCurr` reports discharge as positive and charge as negative. The
  scale changes from `-0.1` to `0.1` and the offset from `500.5` to `-500.2`.
  Clients that consumed this signal before got traction and pack current as
  mirror images of each other.
- `BMSH_BattCurr` zero moves from raw `5005` to raw `5002`, a correction of
  `0.3 A`. The zero comes from 22 recorded trips, comparing the integral of
  this signal against the pack energy the state of charge reports.
- `VCU_ThermalPwrAct` reports an estimated physical value in `kW`, with
  `scale=0.1` and `offset=0`. The flag stays `calibrated=0`: two heating steps
  measured at the pack bracket the scale at 85.5 and 104.5 W per count, which
  contains `0.1 kW` but is not tighter than 20 percent. Cooling is not
  proportional to this count, so a client may use the value only while the car
  reports the PTC heater as the load.
- `VCU_DCDCPwrAct` stays unscaled. Its steps look like 0.1 kW each, but at that
  scale the count claims more power than the whole pack delivers in 10 of 14
  recorded states, so no proportional decode fits both the steps and the
  levels.

## [0.1.0] - 2026-07-27

First public pre-release. Protocol version 3, schema version 1.

This release is a validated tracer, not a stable contract. The wire protocol is
not frozen and may change without a compatibility path. See `Known limits`.

### Added

- `roadcastd`: libuv-based multi-client daemon reading OEM VHAL memory at a
  configurable rate, 60 Hz by default.
- Read-only VHAL source with process discovery, ELF symbol resolution, and
  bounded remote memory reads.
- Deterministic in-memory fake source so the full pipeline runs on a host with
  no vehicle attached.
- Wire protocol v3 with explicit binary framing, defined byte order, and
  validated magic, version, type, flags, payload length, and element counts.
- Generated catalog of 815 decoded CAN signals across 111 receive frames, built
  from `data/dbc.json` by `scripts/generate-catalog.py`.
- Paged schema discovery with stable signal IDs and a canonical schema hash
  (`ROADCAST_CAN_SCHEMA_HASH`).
- Consistent decoded-signal snapshots with subscription catch-up bounded by the
  client queue.
- Paged raw-frame snapshots with explicit observation state and timestamps.
- Per-client bounded queues: a slow client is disconnected or resynchronized
  and never stalls acquisition.
- Directional request/response limits and operational heartbeat metrics.
- `roadcastctl`: reference CLI client, including signal inspection by name
  (`--signal VehicleSpeed --seconds 10`).
- `libroadcast_client.a` with `include/roadcast_client.h`: reusable client SDK
  resolving signals by stable ID.
- Host test suite: protocol round trips, catalog integrity, malformed and
  truncated input, sequence-gap resynchronization, multi-client and
  connect/disconnect loops, and slow-client overflow.
- Android ARM64 / API 28 cross-compilation (`scripts/build-android.sh`,
  `scripts/build-libuv-android.sh`) and a development installer
  (`scripts/install-android.sh`).
- GitHub Actions host/integration tests, Android cross-build artifacts, and
  automatic GitHub Release assets for `v*` tags.
- `scripts/analyze-vhal-surface.py`: static ELF/DWARF analysis that diffs the
  VHAL binary's CAN surface against the catalog and exits non-zero on
  divergence, for use as a post-OTA regression check.
- Documentation: architecture, protocol, decisions, validation, catalog
  surface, and Android deployment.

### Validated

- Protocol v3 confirmed against the vehicle on 2026-07-26 (IHU629G, Android 9,
  ARM64).
- `VCU_DrvPwrAct` on CAN frame `0x315` confirmed on the vehicle with
  `0.1 kW` scale and `-204.8 kW` offset and marked as calibrated.
- Source reads confirmed with SELinux Enforcing from `u:r:su:s0`.
- Catalog confirmed complete against the analyzed VHAL build: all 111 receive
  buffers and all receive-side DWARF signals are covered, with zero overlapping
  bit assignments.

### Known Limits

- The wire protocol is experimental. Message layouts, numeric values, and names
  are not a stable contract at 0.x.
- `valid` currently means "symbol resolved", not "observed". Per-frame freshness
  timestamps do not exist yet.
- 812 of 815 signals carry `scale=1.0`, `offset=0`, and no unit. Three
  signals are calibrated.
- 61 percent of the bits in the frames already being read carry no
  DWARF-defined signal and cannot be named by static analysis.
- The 44 transmit buffers and their 580 statically defined signals are not
  exposed. Reading them is read-only, but the schema cannot yet distinguish an
  observed transmit buffer from an observed receive frame.
- App-domain access to the `@roadcast` abstract socket and the full local-ADB
  bootstrap path are not yet validated together under SELinux Enforcing. Both
  remain gates for a 1.0 release.
- All symbol offsets and counts are pinned to one VHAL BuildID. A vehicle OTA
  invalidates them and can strip the symbol table entirely.
- The daemon resolver silently ignores unknown `MMI_Rx_*` symbols, so catalog
  drift must be detected with `scripts/analyze-vhal-surface.py` rather than
  inferred from a successful startup.

[0.1.0]: https://github.com/roadcast/roadcast/releases/tag/v0.1.0
