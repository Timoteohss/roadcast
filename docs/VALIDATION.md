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

## Host validation

`make test` validates explicit wire encoding, handshake payloads, frame batches,
reserved fields, capacity limits, truncation, and heartbeat payloads.

`make integration` starts a high-load 240 Hz fake source over a temporary
filesystem Unix socket. It verifies that two clients receive complete snapshots
and continuous delta streams without sequence gaps while a third client stops
reading after subscription. The slow client reaches its bounded queue and is
marked for resynchronization without interrupting the other clients. The
temporary directory and socket are removed after the test.

The same suite passes with UndefinedBehaviorSanitizer enabled. AddressSanitizer
could not be evaluated on the current macOS 26.5 host because the Clang ASan
runtime deadlocked inside `AsanInitInternal` before entering the test program's
`main()`.
