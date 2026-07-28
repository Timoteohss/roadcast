# Architecture

## Purpose

Roadcast is a read-only vehicle signal broker. It provides a second source of
vehicle properties that is complete, independent, and local.

Roadcast does not register as the official Android VHAL. It uses a local protocol
to expose data that the OEM VHAL keeps in memory but does not publish through
`CarPropertyManager`.

## Responsibilities

Roadcast performs the following tasks:

- Locates the VHAL process and its relevant memory structures.
- Reads CAN frames and the VehiclePropertyStore.
- Decodes and normalizes known data.
- Preserves raw data and provenance metadata.
- Maintains a complete in-memory snapshot.
- Distributes schema, snapshots, and change batches to clients.
- Reports liveness, losses, capacity, and versions.

Roadcast does NOT perform the following tasks:

- Modifies the VHAL.
- Writes vehicle properties.
- Transmits CAN commands.
- Persists trip data or telemetry.
- Defines how applications display or store data.
- Invents scale, unit, or validity when the information is unknown.

## Overview

```text
+---------------------------------------------------------------+
| OEM VHAL Process                                               |
|                                                                |
|  CAN Buffers                   VehiclePropertyStore            |
+-------------------+--------------------------+-----------------+
                    | Memory Read              |
                    v                          v
+---------------------------------------------------------------+
| roadcastd                                                      |
|                                                                |
|  source/vhal-reader                                            |
|       |                                                        |
|       +-- Raw CAN Frames                                       |
|       +-- Raw VHAL Properties                                  |
|                 |                                              |
|                 v                                              |
|  decode/catalog                                                |
|                 |                                              |
|                 v                                              |
|  Canonical In-Memory Snapshot                                  |
|                 |                                              |
|                 v                                              |
|  client/session manager                                        |
+-------------------+--------------------------+-----------------+
                    |                          |
              @roadcast                  @roadcast
                    |                          |
            +-------v-------+          +-------v-------+
            | Client A      |          | Client B      |
            | Local RAM     |          | Local RAM     |
            +---------------+          +---------------+
```

## Data Flow

### 1. Acquisition

A single sampler reads all sources. The number of clients does not increase the
number of reads performed on the VHAL process.

The initial target rate is 60 Hz. This rate is configurable at runtime. The rate
defines Roadcast's observation cadence. It does not promise that every ECU or
CAN frame will produce new values at 60 Hz.

### 2. Decoding

Each sample can produce three classes of entries:

- A raw CAN frame.
- A signal extracted from a frame.
- A property found in VHAL memory.

Raw data is never discarded because a physical decoded representation exists.

### 3. Canonical Snapshot

The daemon maintains one entry per catalog item. Each entry contains at minimum:

- A stable ID.
- The current schema index.
- The raw value.
- The physical value (when known).
- The origin timestamp (when available).
- The daemon's monotonic observation timestamp.
- The presence and validity state.
- The last-change counter.
- Calibration and provenance flags.

The snapshot exists only in RAM.

### 4. Distribution

When a client connects, it receives the schema and a complete snapshot. After
that, it receives incremental batches. All signals are exposed. Sending only
deltas is not filtering, because the client already has the complete state.

A client can request a full resynchronization at any time.

## Client Isolation

The sampler never writes directly to a blocking socket.

Each session has a bounded in-memory queue. When a consumer cannot keep up, the
daemon can discard intermediate batches and mark the session as needing
resynchronization. A slow client CANNOT:

- Delay the VHAL read.
- Grow memory without limit.
- Degrade other clients.
- Force the daemon to write backlog to disk.

## Runtime and Transport

`roadcastd` uses libuv as its runtime infrastructure. libuv manages:

- The event loop.
- Accepted client sessions.
- Asynchronous reads and writes.
- Timers and Unix signal handling.
- Per-stream write queue observation.
- Orderly connection shutdown.

The acquisition path is independent from the libuv loop. It publishes normalized
changes into a bounded in-memory handoff. Socket activity cannot delay VHAL
sampling.

Roadcast uses the Linux abstract Unix socket `@roadcast`. libuv does not bind
abstract namespace sockets through `uv_pipe_bind()`. A small Linux-specific
adapter creates and binds the listening socket with `socket()` and `bind()`.
The resulting descriptor is adopted by libuv with `uv_pipe_open()`. This adapter
is the only transport-specific socket setup that Roadcast implements directly.

Each client has a bounded logical queue. libuv's stream write queue metrics
provide an additional signal for detecting slow consumers. They do not replace
the protocol-level resynchronization and discard policy.

Roadcast does not combine libuv with another event library.

## Serialization Boundary

The high-frequency data path uses Roadcast's own explicit binary framing and
fixed-width delta records. This approach keeps decoding deterministic, avoids
allocation in the common path, and allows a client to implement the protocol
without linking a Roadcast-specific dependency.

FlatBuffers is the preferred candidate for catalog, schema, and control payloads
when generated schema bindings become necessary. It is not required for the
first tracer. It is not used to wrap each small CAN or property delta.

The daemon must not introduce a FlatBuffers dependency through an unofficial C
binding without a separate decision. The preferred adoption path is to keep the
VHAL reader boundary in C and compile the protocol/catalog boundary as C++11 or
newer, using the official FlatBuffers C++ implementation.

The following components are intentionally NOT part of the baseline architecture:

- NNG or ZeroMQ as the client transport.
- gRPC as the local telemetry protocol.
- protobuf-c or nanopb for high-frequency delta records.
- libevent alongside libuv.
- Runtime DBC parsing when a generated catalog is sufficient.

## Liveness and Consistency

The daemon publishes the following values:

- `sample_seq`: Incremented at each acquisition cycle.
- `change_seq`: Incremented at each change batch.
- `sample_time_ns`: Monotonic instant of the last cycle.
- Schema hash and version.
- Lost batch counters per client.

A client that detects a sequence gap requests a new snapshot.

## Rate and Gauges

The 60 Hz target reduces latency and allows clients to render at the screen's
rate. However, a signal produced by the car at 10 Hz will continue to change at
10 Hz. Visual interpolation and smoothing are the consumer's responsibility. The
daemon must preserve the actual measurement and its timestamp.

## Absence of Disk Pressure

During normal operation, Roadcast does NOT create:

- Snapshot files.
- Socket files.
- Journals.
- Persistent queues.
- Periodic dumps.
- Databases.

High-frequency logs are also prohibited. Diagnostics must use queryable counters
and sparse messages. The method to install the executable is external to this
guarantee. The guarantee applies to data produced at runtime.

## Source Layout

The first tracer keeps a small number of modules while preserving the
documented responsibility boundaries:

```text
roadcast/
+-- README.md
+-- Makefile
+-- docs/
|   +-- ARCHITECTURE.md
|   +-- DECISIONS.md
|   +-- PROTOCOL.md
|   +-- VALIDATION.md
+-- include/
|   +-- roadcast_frames.h
|   +-- roadcast_protocol.h
|   +-- roadcast_vhal.h
+-- scripts/
|   +-- build-android.sh
|   +-- build-libuv-android.sh
+-- src/
|   +-- protocol.c
|   +-- roadcastctl.c
|   +-- roadcastd.c
|   +-- vhal_source.c
+-- tests/
    +-- integration.sh
    +-- test_protocol.c
```

`roadcastd.c` currently contains the sampler handoff and server/session runtime.
These responsibilities should move into separate modules when the next tracer
requires independent snapshot or server tests. Files must be extracted around
testable ownership boundaries, not merely to match a directory diagram.
