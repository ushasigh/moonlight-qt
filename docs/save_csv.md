# Frame CSV Logging for Moonlight-Qt

This document describes the CSV frame logging feature that accompanies Moonlight-Qt's raw YUV recording. It covers code changes, output data, and how to build/test the feature.

## Overview

The decoder thread now writes per-frame metadata to a CSV file alongside the raw YUV recording. Each decoded frame is logged after it is converted to YUV420P and written to disk, so the CSV aligns 1:1 with the stored frames.

## Output Location & Generated Files

Recording starts automatically when the first frame is decoded. The output directory is currently hard-coded in the source (change `recordDir` in `app/streaming/video/ffmpeg.cpp` if a different location is needed).

Each session produces:

| Type | Path Pattern | Notes |
|------|-------------|-------|
| Raw video | `moonlight_recording_YYYYMMDD_hhmmss.yuv` | YUV420P, one file per session |
| Metadata | `moonlight_recording_YYYYMMDD_hhmmss.yuv.meta` | width/height/fps + ffmpeg convert hint |
| Frame CSV | `moonlight_recording_YYYYMMDD_hhmmss.yuv.frames.csv` | Per-frame index/timestamp log |

## CSV Contents

Header (written once on start):

```csv
local_frame_idx,frame_nr,rtp_timestamp
```

Sample rows:

```csv
local_frame_idx,frame_nr,rtp_timestamp
0,1,1500
1,2,3000
2,3,4500
3,5,7500
4,6,9000
```

In this example, `frame_nr=4` is missing — this indicates that frame 4 was dropped on the network. By joining with the Sunshine-side CSV on `frame_nr`, you can compute VMAF between corresponding frame pairs.

| Column | Description |
|--------|-------------|
| `local_frame_idx` | Zero-based counter of frames successfully written to the YUV file |
| `frame_nr` | Frame number from the sender (from `DECODE_UNIT.frameNumber`); `-1` if unavailable |
| `rtp_timestamp` | RTP timestamp in 90 kHz timebase |

## Code Changes

### Modified Files

#### 1. `app/streaming/video/videorecorder.h`

Changes:

- Added CSV members (`QFile* m_CsvFile`, `QTextStream* m_CsvStream`)
- Changed `writeFrame()` signature to accept `frameNumber` + `rtpTimestamp`

```cpp
class VideoRecorder {
public:
    // ...
    bool writeFrame(AVFrame* frame, int frameNumber, uint32_t rtpTimestamp);
    // ...
private:
    // ...
    QFile* m_CsvFile;        
    QTextStream* m_CsvStream; 
};
```

#### 2. `app/streaming/video/videorecorder.cpp`

Changes:

- **Constructor**: Added initialization for `m_CsvFile` and `m_CsvStream`
- **`initialize()`**: Creates `<output>.frames.csv`, writes header, and opens a stream
- **`writeFrame()`**: After successful YUV write, appends `local_frame_idx,frame_nr,rtp_timestamp` to CSV
- **`finalize()`**: Flushes and closes CSV resources

#### 3. `app/streaming/video/ffmpeg.cpp`

Changes:

- In the decoder loop, recording auto-starts on the first decoded frame
- Extracts `frameNumber` and `rtpTimestamp` from the dequeued `DECODE_UNIT` and passes them to `m_VideoRecorder->writeFrame(...)`

```cpp
// Widen scope so du info is available for recording
int currentFrameNumber = 0;
uint32_t currentRtpTimestamp = 0;

if (!m_FrameInfoQueue.isEmpty()) {
    DECODE_UNIT du = m_FrameInfoQueue.dequeue();
    // ...
    currentFrameNumber = du.frameNumber;
    currentRtpTimestamp = du.rtpTimestamp;
}

// Pass frame_nr and rtp_timestamp to writeFrame
m_VideoRecorder->writeFrame(frame, currentFrameNumber, currentRtpTimestamp);
```

## Recording Lifecycle

- **Start**: When the first frame is decoded and `m_VideoRecorder` exists, output files are created and logging begins
- **During decode**: Each decoded frame is converted to YUV420P → written to the YUV file → logged to CSV
- **Stop**: On decoder teardown, `VideoRecorder::finalize()` flushes/closes YUV and CSV, and frees buffers

## Building

```bash
cd moonlight-qt
mkdir build
cd build
qmake ..
make -j$(nproc)
```

Run Moonlight-Qt and start a stream; YUV + CSV are emitted to `recorded_session/`.

## Limitations

- The output path is hard-coded in the source. Change `recordDir` in `app/streaming/video/ffmpeg.cpp` if you want a different location.
- Files are raw/uncompressed YUV420P; storage usage is large (e.g., ~180 MB/s at 1080p60).

## Changed Files Summary

| File | Operation | Description |
|------|-----------|-------------|
| `app/streaming/video/videorecorder.h` | Modified | CSV members added + `writeFrame()` signature change |　
| `app/streaming/video/videorecorder.cpp` | Modified | CSV creation, writing, and cleanup |
| `app/streaming/video/ffmpeg.cpp` | Modified | Extracts frame_nr/rtp_timestamp from `DECODE_UNIT` and passes to `writeFrame()` |