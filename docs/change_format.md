# Recording Format Configuration (raw/mp4) - Change Notes

## Purpose
- Allow Moonlight to switch recording formats from a config file.

## Added Configuration Keys (QSettings)
- `recording/enable` : bool (default `true`) — Enable/disable recording.
- `recording/format` : string (`raw` or `mp4`, default `raw`).
- `recording/output_dir` : string (default `/home/wcsng5g/moonlight-qt/recorded_session`). Falls back to default if empty.


## Usage
1. Add/edit the following in the config file:
   ```ini
   [recording]
   enable=true
   format=mp4      ; or raw
   output_dir=/home/wcsng5g/moonlight-qt/recorded_session
   ```
2. Launch the app and start streaming. On the first decoded frame, a file named `moonlight_recording_YYYYMMDD_hhmmss.[yuv|mp4]` is created under `output_dir`, along with `.meta` and `.frames.csv` files.
3. When `format=mp4`, if `ffmpeg` is not in PATH, recording will fail with a log message (recording is skipped).


## Config File Locations
- Windows: `HKEY_CURRENT_USER\Software\Moonlight Game Streaming Project\Moonlight`
- Linux/WSL: `~/.config/Moonlight Game Streaming Project/Moonlight.conf`
- macOS: `~/Library/Preferences/com.moonlight-stream.Moonlight.plist`

## Build Instructions
```bash
cd moonlight-qt
mkdir -p build && cd build
qmake ..
make -j$(nproc)
```
Note: mp4 recording requires `ffmpeg` in PATH at runtime.

## Running & Verification
Add `[recording]` section to the config file (use `format=mp4` to test mp4).
After modifying the config, run via one of:

From the command line:
```bash
cd moonlight-qt/build/app
./moonlight
```

From a file manager:
Double-click the `moonlight` binary in `moonlight-qt/build/app`.

## Detailed Changes
- `app/settings/streamingpreferences.h/.cpp`
  - Added recording-related members (`recordingEnabled`, `recordingFormat`, `recordingOutputDir`) with QSettings read/write.
  - Defaults set to the existing path `/home/wcsng5g/moonlight-qt/recorded_session` + `format=raw`.
- `app/streaming/video/videorecorder.h/.cpp`
  - Added `RecordFormat`; extended `initialize()` to `(path, w, h, fps, format)`.
  - `raw` uses the existing `QFile` write path; `mp4` uses ffmpeg pipe with x264 encoding.
  - Unified cleanup for both output modes. Defined Windows `_popen/_pclose` macros.
  - CSV/meta file output unchanged.
- `app/streaming/video/ffmpeg.cpp`
  - On first decode, reads `StreamingPreferences`; starts recording only if `recording/enable` is true.
  - Generates file extension as `yuv`/`mp4` based on format and passes it to `VideoRecorder::initialize()`.
  - On ffmpeg pipe startup failure, retries once then skips subsequent attempts.


