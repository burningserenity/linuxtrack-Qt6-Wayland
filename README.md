# lockd — lock-on-away daemon

`lockd` is a tiny Linux/Wayland daemon that watches your webcam for a face and
locks the screen when you walk away. It uses OpenCV for camera capture and Haar
cascade face detection, and triggers a Wayland screen locker once no face has
been seen for a configurable timeout.

This is a stripped-down fork of
[linuxtrack-Qt6-Wayland](https://github.com/burningserenity/linuxtrack-Qt6-Wayland):
all head-tracking, Qt UI, TrackIR, Wine bridge, and IPC code has been removed.
Only the camera capture and face-detection pipeline remain.

## How it works

1. Reads frames from the camera (OpenCV `VideoCapture`, V4L2).
2. Detects whether a face is present (OpenCV `CascadeClassifier`).
3. Tracks how long no face has been seen.
4. After the timeout elapses with no face, invokes the first available locker:
   `swaylock`, then `hyprlock`, then `loginctl lock-session`.
5. Resets the timer the moment a face is detected again.
6. Does not lock again while the session is already locked
   (checked via `loginctl ... LockedHint`).

All logging goes to stderr; there is no GUI.

## Build

Requires a C++17 compiler, CMake (>= 3.16), and OpenCV (>= 4).

```bash
cmake -B build
cmake --build build
```

The binary is produced at `build/daemon/lockd`. Installing also places the
bundled Haar cascade under `<prefix>/share/lockd/`:

```bash
sudo cmake --install build
```

## Usage

```bash
lockd [--timeout <seconds>] [--camera <index>] [--cascade <path>]
```

| Flag        | Default | Description                                       |
|-------------|---------|---------------------------------------------------|
| `--timeout` | `30`    | Seconds with no face before locking.              |
| `--camera`  | `0`     | V4L2 camera index.                                |
| `--cascade` | bundled | Path to a Haar cascade XML (overrides the default).|

Example — lock after 60 seconds away, using camera 1:

```bash
lockd --timeout 60 --camera 1
```

If `lockd` is run from the build tree (not installed), pass `--cascade` pointing
at `src/haarcascade_frontalface_alt2.xml`.

## Supported lockers

`lockd` tries these in order and uses the first one found on `$PATH`:

1. `swaylock`
2. `hyprlock`
3. `loginctl lock-session`

There are no X11-specific locking mechanisms.

## License

Inherited from the upstream linuxtrack project; see `LICENSE.md` and `COPYING`.
