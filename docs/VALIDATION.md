# Validation

This document records meaningful host and vehicle validation runs. Runtime
telemetry is not persisted on the vehicle.

## 2026-07-26: first raw CAN tracer

### Scope

The tracer exercised:

```text
OEM VHAL memory -> 60 Hz sampler -> libuv -> @roadcast-test
                -> two concurrent roadcastctl clients
```

The test used unique temporary binary and socket names. It did not stop or
replace the existing `vhalpeek` process.

### Target state

```text
device: IHU629G
Android: 9
architecture: ARM64
VHAL PID: 295
SELinux observed state: Permissive
existing vhalpeek PID: 29245
```

Roadcast did not change SELinux state, restart the device, stop the VHAL, or
modify vendor files.

### Build

The daemon and client were cross-compiled as PIE executables for Android API 28
with Android NDK `28.2.13676358` and libuv `1.51.0`.

Both outputs were identified as ARM AArch64 ELF64 executables using
`/system/bin/linker64`.

### Commands

```bash
adb -s <head-unit-address>:5555 push \
  build/android-arm64-v8a-api28/roadcastd \
  /data/local/tmp/roadcastd-test

adb -s <head-unit-address>:5555 shell \
  'su 0 /data/local/tmp/roadcastd-test --hz 60 --socket @roadcast-test'

adb -s <head-unit-address>:5555 shell \
  'su 0 /data/local/tmp/roadcastctl-test \
    --socket @roadcast-test --seconds 5'
```

Two instances of the final client command ran concurrently.

### Results

The source resolver found all `111/111` raw CAN frame symbols in the OEM VHAL.

Daemon statistics:

```text
effective sampling rate: 59.9-60.0 Hz
changed frames per sample: approximately 5.7
source read errors: 0
client queue drops: 0
```

Each client received:

```text
protocol version: 1
snapshot frames: 111
update batches: approximately 20 per second
changed frame records: approximately 341 per second
sequence gaps: 0
resynchronizations: 0
```

The batch rate is lower than the sampling rate because Roadcast sends an update
batch only when at least one frame changed. `sample_seq` advanced at
approximately 60 per second independently of the batch rate.

Both clients disconnected independently. The daemon was then stopped and both
temporary executables were removed from `/data/local/tmp`.

## 2026-07-26: protocol version 3 permissive diagnostic

### Scope and limitation

This run exercised the current version 3 path:

```text
OEM VHAL memory -> 60 Hz sampler -> @roadcast-v3-202045
                -> two concurrent version 3 clients for 12 seconds
```

The device was still `Permissive`. The daemon ran as `u:r:su:s0`, launched with
`su 0`. This validates the functional and performance path but **does not prove
that Roadcast works under SELinux Enforcing**. Enforcing validation remains a
release gate and must use the intended production launch domain.

Roadcast did not call `setenforce`, change policy, reboot, stop the VHAL, or
modify vendor files.

### Target and build

```text
device: IHU629G
Android: 9
architecture: ARM64
VHAL PID: 295
VHAL process: android.hardware.automotive.vehicle@2.0-service
SELinux state: Permissive
Roadcast context: u:r:su:s0
NDK: 28.2.13676358
protocol: 3
schema: version 1, hash 0x0ceade5f14ed7915
```

### Results

The source resolver found all `111/111` frame symbols. The daemon's ten-second
statistics window reported:

```text
effective sampling rate: 59.90 Hz
changed frames per sample: 6.10
source read errors: 0
/proc/PID/mem fallback batches: 0
coalesced samples: 0
client queue drops: 0
session timeouts/rejections: 0
```

Both clients received the same initial frozen snapshot and completed 12 seconds
with:

```text
frame batches: approximately 20-23 per second
changed frame records: approximately 360-375 per second
signal batches: approximately 19-22 per second
changed signal records: approximately 49-63 per second
sequence gaps: 0
resynchronizations: 0
```

A process sample while both clients were connected reported two threads,
4712 KiB RSS, 12332 KiB virtual size, and 3.3% CPU in that `top` interval.

No AVC entry matching the Roadcast process, PID, VHAL, `process_vm_readv`, or
`/proc/PID/mem` appeared in the captured denial tail. The tail did contain
pre-existing permissive denials for the old geelybattery memfd experiment and a
`su` setuid operation. Absence of a matching permissive AVC is useful evidence,
but it is not a substitute for an Enforcing run.

The daemon was terminated normally and both uniquely named test binaries were
removed from `/data/local/tmp`.

## Host validation

`make test` validates explicit wire encoding, handshake payloads, frame batches,
reserved fields, directional payload limits, page metadata, observation states
and timestamps, capacity limits, truncation, and version 3 heartbeat payloads.

`make integration` starts a high-load 240 Hz fake source over a temporary
filesystem Unix socket. It verifies:

- sixteen idle pre-HELLO connections cannot deny service after the two-second
  handshake deadline;
- malformed headers, oversized payload declarations, reserved flags, commands
  out of order, duplicate HELLO messages, random bytes, and a byte-fragmented
  HELLO remain isolated to their sessions;
- two clients receive complete snapshots and continuous delta streams without
  sequence gaps while another client stops reading;
- the 111-frame snapshot is retrieved as multiple bounded pages;
- decoded fake-source updates are observed/valid and carry non-zero
  first-observed and last-change timestamps;
- a dedicated one-slot session pauses and resumes a multipage subscription
  catch-up before receiving `SUBSCRIBED`;
- a paused client overflows its bounded queue, receives `RESYNC_REQUIRED`,
  retrieves a new consistent snapshot, subscribes again, and resumes with zero
  sequence gaps after recovery.

Accepted connections are identified with `SO_PEERCRED` on Android/Linux or
`getpeereid()` on macOS. A single UID may hold at most eight sessions. Sessions
that do not complete HELLO in two seconds or do not reach a subscribed state in
ten seconds are closed. These controls bound unauthenticated setup work; they
are not an authorization policy.

The temporary directory and socket are removed after the test.

The sampler-to-event-loop handoff no longer drops an observation when its
snapshot mutex is contended. libuv notifications may still coalesce, so the
daemon reports the number of intermediate samples represented by a newer
published snapshot as `coalesced`.

The same suite passes with UndefinedBehaviorSanitizer enabled. AddressSanitizer
could not be evaluated on the current macOS 26.5 host because the Clang ASan
runtime deadlocked inside `AsanInitInternal` before entering the test program's
`main()`.
