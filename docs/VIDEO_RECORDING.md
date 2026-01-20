# Video Recording Feature for Moonlight-Qt

This document describes the video recording feature that captures the received video stream and saves it as raw YUV frames.

## Overview

The video recording feature captures decoded video frames from the Moonlight streaming session and saves them to a raw YUV420P file. This is useful for:
- Quality analysis and comparison
- Debugging video issues
- Frame-by-frame inspection
- VMAF/PSNR quality metrics calculation

## Output Location

Recordings are saved to:
```
/Users/ushasighosh/Desktop/moonlight-qt/recorded_session/
```

Each recording creates two files:
- `moonlight_recording_YYYYMMDD_hhmmss.yuv` - Raw YUV420P video data
- `moonlight_recording_YYYYMMDD_hhmmss.yuv.meta` - Metadata file with video parameters

## Files Modified/Added

### New Files

#### 1. `app/streaming/video/videorecorder.h`

Header file for the VideoRecorder class.

```cpp
class VideoRecorder {
public:
    VideoRecorder();
    ~VideoRecorder();

    bool initialize(const QString& outputPath, int width, int height, int fps);
    bool writeFrame(AVFrame* frame);
    void finalize();
    bool isRecording() const;
    QString getOutputPath() const;

private:
    bool m_Recording;
    QString m_OutputPath;
    QFile* m_OutputFile;
    SwsContext* m_SwsCtx;
    AVFrame* m_ConvertedFrame;
    uint8_t* m_FrameBuffer;
    int m_Width, m_Height, m_Fps;
    int64_t m_FrameCount;
    int m_LastInputFormat;
    QMutex m_Mutex;
};
```

Key features:
- Thread-safe with QMutex
- Handles hardware-accelerated frames (VideoToolbox on macOS)
- Converts any pixel format to YUV420P
- Creates metadata file for easy conversion

#### 2. `app/streaming/video/videorecorder.cpp`

Implementation of the VideoRecorder class.

Key functionality:
- **Hardware frame handling**: Detects VideoToolbox frames and transfers them from GPU to CPU memory using `av_hwframe_transfer_data()`
- **Format conversion**: Uses `swscale` to convert any input format to YUV420P
- **Raw YUV writing**: Writes Y, U, V planes sequentially for each frame

### Modified Files

#### 3. `app/streaming/video/ffmpeg.h`

**Changes:**
- Added include for videorecorder.h
- Added `VideoRecorder* m_VideoRecorder` member variable

```cpp
#include "videorecorder.h"
// ...
class FFmpegVideoDecoder : public IVideoDecoder {
    // ...
private:
    VideoRecorder* m_VideoRecorder;  // Added
};
```

#### 4. `app/streaming/video/ffmpeg.cpp`

**Changes:**

1. Added includes:
```cpp
#include <QDir>
#include <QDateTime>
```

2. Constructor - Initialize VideoRecorder:
```cpp
FFmpegVideoDecoder::FFmpegVideoDecoder(bool testOnly)
    : // ... other initializers ...
      m_VideoRecorder(nullptr)
{
    // ...
    m_VideoRecorder = new VideoRecorder();
}
```

3. Destructor - Cleanup VideoRecorder:
```cpp
FFmpegVideoDecoder::~FFmpegVideoDecoder()
{
    if (m_VideoRecorder) {
        m_VideoRecorder->finalize();
        delete m_VideoRecorder;
        m_VideoRecorder = nullptr;
    }
    // ...
}
```

4. In `decoderThreadProc()` - Record frames after decoding (around line 1790):
```cpp
// Record frame to file if recording is enabled
if (m_VideoRecorder) {
    if (!m_VideoRecorder->isRecording() && m_VideoDecoderCtx) {
        QString recordDir = "/Users/ushasighosh/Desktop/moonlight-qt/recorded_session";
        QDir().mkpath(recordDir);
        QString outputPath = QString("%1/moonlight_recording_%2.yuv")
            .arg(recordDir)
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        m_VideoRecorder->initialize(outputPath,
                                   m_VideoDecoderCtx->width,
                                   m_VideoDecoderCtx->height,
                                   m_StreamFps > 0 ? m_StreamFps : 60);
    }
    m_VideoRecorder->writeFrame(frame);
}
```

#### 5. `app/app.pro`

**Changes:**
Added new source files to the ffmpeg build section:

```qmake
ffmpeg {
    SOURCES += \
        # ... existing sources ...
        streaming/video/videorecorder.cpp

    HEADERS += \
        # ... existing headers ...
        streaming/video/videorecorder.h
}
```

## Converting YUV to MP4

After recording, convert the raw YUV file to MP4 using FFmpeg:

```bash
ffmpeg -f rawvideo -pix_fmt yuv420p -s 1920x1080 -r 60 \
    -i "recorded_session/moonlight_recording_YYYYMMDD_hhmmss.yuv" \
    -c:v libx264 -pix_fmt yuv420p -crf 18 \
    "output.mp4"
```

Parameters (check the .meta file for exact values):
- `-s WIDTHxHEIGHT` - Video resolution
- `-r FPS` - Frame rate
- `-crf 18` - Quality (lower = better, 18 is high quality)

## Customizing the Output Path

To change the recording location, modify this section in `ffmpeg.cpp` (around line 1797):

```cpp
QString recordDir = "/Users/ushasighosh/Desktop/moonlight-qt/recorded_session";
QString outputPath = QString("%1/moonlight_recording_%2.yuv")
    .arg(recordDir)
    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
```

## Storage Requirements

Raw YUV420P files are uncompressed:
- **1080p60**: ~180 MB/second (~10.8 GB/minute)
- **720p60**: ~80 MB/second (~4.8 GB/minute)
- **4K60**: ~720 MB/second (~43 GB/minute)

Ensure you have sufficient disk space before recording.

## Building

After making changes, rebuild:

```bash
cd build
make -j8
```

## Disabling Recording

To disable video recording, comment out or remove the recording block in `ffmpeg.cpp`:

```cpp
// Comment out this entire block to disable recording
/*
if (m_VideoRecorder) {
    // ... recording code ...
}
*/
```

Or delete `m_VideoRecorder = new VideoRecorder();` from the constructor.
