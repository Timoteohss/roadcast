# Android Deployment

Roadcast does not modify Android system or vendor partitions. The current
vehicle integration delegates distribution and supervision to the privileged
GeelyBattery app:

- the APK bundles the ARM64/API 28 `roadcastd` executable;
- the app compares its SHA-256 hash with `/data/local/tmp/roadcastd`;
- the app copies the executable only when it is missing or its version changes;
- the installed executable is `root:root` with mode `0755`;
- the app starts it through the vehicle's root `adbd`;
- the app rejects an existing daemon outside `u:r:su:s0`;
- a foreground-service watchdog restores it after an unexpected exit.

The installed executable persists across normal app process restarts and
reboots. It is rewritten only after an APK carrying a different binary is
installed. Runtime telemetry remains entirely in memory.

## Lifecycle

On the validated IHU629G:

- root `adbd` listens on TCP port 5555;
- `adbd` runs in `u:r:su:s0`;
- a new ADB host identity is accepted without user authorization;
- a shell opened through that connection runs in `u:r:su:s0`.

The `geelybattery` client uses a minimal ADB 1.0.1 host over
`127.0.0.1:5555`. After `BOOT_COMPLETED`, and whenever its watchdog cannot
connect to `@roadcast`, it asks `adbd` to execute:

```bash
nohup /data/local/tmp/roadcastd \
  --hz 60 \
  --socket @roadcast \
  </dev/null >/dev/null 2>&1 &
```

This launcher is vehicle-specific. The app must report an explicit degraded
state if local ADB is disabled, moved to another port, or starts requiring
authentication. The launcher requires ADB `shell_v2`: the vehicle's legacy
`shell:` service kills a detached child when the session closes. Other clients
remain independent protocol consumers and do not gain permission to manage the
daemon.

## Manual Validation

The optional installer reproduces the same target path without modifying
`system`:

```bash
export ANDROID_NDK=/path/to/android-ndk
make android
scripts/install-android.sh --device <head-unit-address>:5555
```

This script is for development and recovery. Production distribution is through
the APK asset.

## Security Boundary

Calling `/system/xbin/su` from an Android `system_app` changes the Linux UID but
does not transition out of `u:r:system_app:s0` on this vehicle. The kernel then
denies access from Roadcast to `hal_vehicle_default` while SELinux is Enforcing.

Launching through root `adbd` creates Roadcast in `u:r:su:s0`, the same domain
that passed the Enforcing source-read validation. App-domain access to the
Roadcast abstract socket and the complete local-ADB bootstrap path remain
release gates until validated together under Enforcing.
