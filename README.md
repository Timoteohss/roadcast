# Roadcast

[![Build](https://github.com/Timoteohss/roadcast/actions/workflows/build.yml/badge.svg)](https://github.com/Timoteohss/roadcast/actions/workflows/build.yml)

Roadcast is a local, read-only broker for vehicle signals.

Its purpose is to expose, to multiple applications, the data that reaches the
VHAL but is not published through the Android Automotive APIs. The daemon reads
VHAL memory once, keeps the complete known vehicle state in RAM, and distributes
that state to every connected client.

The project grew out of the investigation done in `vhalpeek`, but it is not an
extension of it:

- `vhalpeek` remains an investigation and diagnostic tool;
- Roadcast is a long-running service with a discoverable protocol;
- consumers know nothing about the VHAL's internal memory layout;
- no consumer is treated as the "main app".

## Principles

- **Read-only:** Roadcast observes the vehicle; it never sends commands to the
  VHAL or to the CAN bus.
- **App-agnostic:** N applications can consume the same daemon.
- **Complete catalog:** useful and apparently useless signals are exposed
  equally.
- **No implicit persistence:** snapshots, queues, and transport stay in RAM.
- **Low latency:** acquisition targets 60 Hz, configurable.
- **A slow consumer never blocks the producer:** every client has a bounded
  queue.
- **Dynamic discovery:** the daemon publishes schema, IDs, types, units, and
  origin.
- **Explicit compatibility:** protocol, schema, and implementation are versioned
  independently.

## Transport

The primary transport is an abstract Unix socket, so no socket file is created
on the filesystem:

```text
@roadcast
```

Every client receives:

1. the schema/catalog;
2. a complete initial snapshot;
3. incremental batches carrying only the entries that changed;
4. heartbeats and sequence numbers, so loss or stalling is detectable.

Shared memory may later exist as an optional fast path, but it will never be
required to implement a Roadcast client.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Protocol](docs/PROTOCOL.md)
- [Decisions](docs/DECISIONS.md)
- [Validation](docs/VALIDATION.md)
- [Catalog surface](docs/CATALOG_SURFACE.md)
- [Client SDK](docs/CLIENT_SDK.md)
- [Android deployment](docs/ANDROID_DEPLOYMENT.md)
- [Changelog](CHANGELOG.md)

## Status

Version 0.1.0. Protocol version 3, schema version 1.

The first executable tracer is implemented and validated. It provides:

- a libuv-based multi-client daemon;
- a deterministic in-memory fake source for host validation;
- explicit binary framing for raw CAN snapshots and delta batches;
- a generated catalog containing 815 decoded CAN signals;
- paged schema discovery with stable IDs and a canonical schema hash;
- consistent decoded-signal snapshots with subscription catch-up;
- paged raw-frame snapshots and explicit observation state/timestamps;
- directional request/response limits and operational heartbeat metrics;
- a reference CLI client and a reusable client SDK;
- protocol, malformed-input, resynchronization, and multi-client tests;
- Android ARM64/API 28 cross-compilation scripts.

**The wire protocol is experimental and is not frozen.** At 0.x, message
layouts, numeric values, and names may change without a compatibility path. See
the known limits in [CHANGELOG.md](CHANGELOG.md).

Inspect a decoded signal by name:

```bash
roadcastctl --socket @roadcast --signal VehicleSpeed --seconds 10
```

## Host build

Requirements:

- a C11 compiler;
- libuv discoverable through `pkg-config`;
- `libelf` headers on hosts that do not provide `elf.h`.

On macOS:

```bash
brew install libuv libelf
make test
make integration
```

The host tracer uses a filesystem Unix socket because macOS does not support the
Linux abstract Unix socket namespace. The socket is created inside a temporary
test directory and removed after the test.

## Android build

```bash
export ANDROID_NDK="$HOME/Library/Android/sdk/ndk/28.2.13676358"
scripts/build-libuv-android.sh
make android
```

An existing Android libuv installation can be reused:

```bash
LIBUV_PREFIX=/path/to/libuv-android make android
```

## Continuous integration and releases

GitHub Actions runs host unit/integration tests and cross-compiles the Android
ARM64/API 28 binaries on every pull request and push to `main`. Each successful
run publishes a 14-day workflow artifact containing:

- `roadcastd`;
- `roadcastctl`;
- `libroadcast_client.so`;
- `SHA256SUMS`.

Pushing a `v*` tag, for example `v0.1.1`, runs the same gates and publishes or
updates a GitHub Release with those files. The release job uses the workflow's
scoped `contents: write` permission; pull-request and normal build jobs remain
read-only.

See [Android deployment](docs/ANDROID_DEPLOYMENT.md) for installation and the
supervision model.

## License

Apache License 2.0. See [LICENSE](LICENSE).

Roadcast is an independent research and interoperability project. It is not
affiliated with or endorsed by Geely, Google, or any vehicle manufacturer, and
it redistributes no manufacturer code, binary, or proprietary database. Read
[NOTICE](NOTICE) before using it: running this software may affect your vehicle
warranty, and you are responsible for having the right to access the vehicle you
run it on.
