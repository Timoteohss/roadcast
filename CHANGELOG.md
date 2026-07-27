# Changelog

All notable changes to Roadcast are recorded here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Roadcast versions the daemon separately from the wire protocol and the catalog
schema. A release line names all three.

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
- `scripts/analyze-vhal-surface.py`: static ELF/DWARF analysis that diffs the
  VHAL binary's CAN surface against the catalog and exits non-zero on
  divergence, for use as a post-OTA regression check.
- Documentation: architecture, protocol, decisions, validation, catalog
  surface, and Android deployment.

### Validated

- Protocol v3 confirmed against the vehicle on 2026-07-26 (IHU629G, Android 9,
  ARM64).
- Source reads confirmed with SELinux Enforcing from `u:r:su:s0`.
- Catalog confirmed complete against the analyzed VHAL build: all 111 receive
  buffers and all receive-side DWARF signals are covered, with zero overlapping
  bit assignments.

### Known limits

- The wire protocol is experimental. Message layouts, numeric values, and names
  are not a stable contract at 0.x.
- `valid` currently means "symbol resolved", not "observed". Per-frame freshness
  timestamps do not exist yet.
- 813 of 815 signals carry `scale=1.0`, `offset=0`, and no unit. Only two
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

[0.1.0]: https://github.com/Timoteohss/roadcast/releases/tag/v0.1.0
