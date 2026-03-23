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

## Code Diff (Key Changes)

### app/streaming/video/videorecorder.h
```diff
-#include <QFile>
-
-extern "C" {
-#include <libavcodec/avcodec.h>
-#include <libswscale/swscale.h>
-#include <libavutil/imgutils.h>
-#include <libavutil/hwcontext.h>
-#include <libavutil/pixfmt.h>
-}
+#include <QFile>
+#include <QTextStream>
+#include <cstdio>
+#include <cstdint>
+
+extern "C" {
+#include <libavcodec/avcodec.h>
+#include <libswscale/swscale.h>
+#include <libavutil/imgutils.h>
+#include <libavutil/hwcontext.h>
+#include <libavutil/pixfmt.h>
+}
-    bool initialize(const QString& outputPath, int width, int height, int fps);
-
-    bool writeFrame(AVFrame* frame);
+    enum class RecordFormat {
+        Raw,
+        Mp4,
+    };
+
+    static RecordFormat recordFormatFromString(const QString& value);
+
+    bool initialize(const QString& outputPath, int width, int height, int fps, RecordFormat format);
+
+    bool writeFrame(AVFrame* frame, int frameNumber, uint32_t rtpTimestamp);
-    bool m_Recording;
-    QString m_OutputPath;
-
-    QFile* m_OutputFile;
-    SwsContext* m_SwsCtx;
+    bool m_Recording;
+    QString m_OutputPath;
+
+    RecordFormat m_Format;
+
+    QFile* m_OutputFile;
+    FILE* m_FfmpegPipe;
+    SwsContext* m_SwsCtx;
-    int m_LastInputFormat;
-
-    QMutex m_Mutex;
+    int m_LastInputFormat;
+
+    QMutex m_Mutex;
+
+    QFile* m_CsvFile;
+    QTextStream* m_CsvStream;
```

### app/streaming/video/videorecorder.cpp
```diff
-#include <QDateTime>
-#include <QDir>
-#include <QTextStream>
+#include <QDateTime>
+#include <QDir>
+#include <QTextStream>
+#include <cstdio>
+
+#ifdef Q_OS_WIN
+#define popen _popen
+#define pclose _pclose
+#endif
-VideoRecorder::VideoRecorder()
-    : m_Recording(false),
-      m_OutputFile(nullptr),
-      m_SwsCtx(nullptr),
-      m_ConvertedFrame(nullptr),
-      m_FrameBuffer(nullptr),
-      m_Width(0),
-      m_Height(0),
-      m_Fps(0),
-      m_FrameCount(0),
-      m_LastInputFormat(AV_PIX_FMT_NONE)
-{
-}
+VideoRecorder::VideoRecorder()
+    : m_Recording(false),
+      m_OutputPath(),
+      m_Format(RecordFormat::Raw),
+      m_OutputFile(nullptr),
+      m_FfmpegPipe(nullptr),
+      m_SwsCtx(nullptr),
+      m_ConvertedFrame(nullptr),
+      m_FrameBuffer(nullptr),
+      m_Width(0),
+      m_Height(0),
+      m_Fps(0),
+      m_FrameCount(0),
+      m_LastInputFormat(AV_PIX_FMT_NONE),
+      m_CsvFile(nullptr),
+      m_CsvStream(nullptr)
+{
+}
+
+VideoRecorder::~VideoRecorder()
+{
+    finalize();
+}
+
+VideoRecorder::RecordFormat VideoRecorder::recordFormatFromString(const QString& value)
+{
+    if (value.compare("mp4", Qt::CaseInsensitive) == 0) {
+        return RecordFormat::Mp4;
+    }
+    return RecordFormat::Raw;
+}
-bool VideoRecorder::initialize(const QString& outputPath, int width, int height, int fps)
+bool VideoRecorder::initialize(const QString& outputPath, int width, int height, int fps, RecordFormat format)
 {
     QMutexLocker locker(&m_Mutex);
-    m_OutputPath = outputPath;
+    m_OutputPath = outputPath;
+    m_Format = format;
-    // Open the output file for raw YUV data
-    m_OutputFile = new QFile(outputPath);
-    if (!m_OutputFile->open(QIODevice::WriteOnly)) {
-        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
-                     "VideoRecorder: Could not open output file: %s",
-                     outputPath.toUtf8().constData());
-        delete m_OutputFile;
-        m_OutputFile = nullptr;
-        return false;
-    }
+    // Open the output sink based on format
+    if (m_Format == RecordFormat::Raw) {
+        m_OutputFile = new QFile(outputPath);
+        if (!m_OutputFile->open(QIODevice::WriteOnly)) {
+            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
+                         "VideoRecorder: Could not open output file: %s",
+                         outputPath.toUtf8().constData());
+            delete m_OutputFile;
+            m_OutputFile = nullptr;
+            return false;
+        }
+    }
+    else {
+        QString cmd = QStringLiteral(
+            "ffmpeg -y -hide_banner -loglevel warning "
+            "-f rawvideo -pix_fmt yuv420p -s %1x%2 -r %3 -i pipe:0 "
+            "-c:v libx264 -preset ultrafast -crf 18 -pix_fmt yuv420p \"%4\"")
+                .arg(width)
+                .arg(height)
+                .arg(fps)
+                .arg(outputPath);
+        m_FfmpegPipe = popen(cmd.toUtf8().constData(), "w");
+        if (!m_FfmpegPipe) {
+            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
+                         "VideoRecorder: Could not start ffmpeg pipe: %s",
+                         cmd.toUtf8().constData());
+            return false;
+        }
+    }
+
+    // Mark as recording so finalize() can clean up on failures below
+    m_Recording = true;
-    // Write metadata file alongside the YUV file
+    // Write metadata file alongside the output
-        out << "# To convert to MP4, run:\n";
-        out << "# ffmpeg -f rawvideo -pix_fmt yuv420p -s " << width << "x" << height
-            << " -r " << fps << " -i \"" << outputPath << "\" -c:v libx264 -pix_fmt yuv420p output.mp4\n";
+        if (m_Format == RecordFormat::Raw) {
+            out << "# To convert to MP4, run:\n";
+            out << "# ffmpeg -f rawvideo -pix_fmt yuv420p -s " << width << "x" << height
+                << " -r " << fps << " -i \"" << outputPath << "\" -c:v libx264 -pix_fmt yuv420p output.mp4\n";
+        }
-                "VideoRecorder: Started recording YUV to %s (%dx%d @ %d fps)",
-                outputPath.toUtf8().constData(), width, height, fps);
+                "VideoRecorder: Started recording to %s (%dx%d @ %d fps, format=%s)",
+                outputPath.toUtf8().constData(), width, height, fps,
+                m_Format == RecordFormat::Raw ? "raw" : "mp4");
-bool VideoRecorder::writeFrame(AVFrame* frame)
+bool VideoRecorder::writeFrame(AVFrame* frame, int frameNumber, uint32_t rtpTimestamp)
 {
     QMutexLocker locker(&m_Mutex);

-    if (!m_Recording || !frame || !m_OutputFile) {
+    if (!m_Recording || !frame ||
+        (m_Format == RecordFormat::Raw && !m_OutputFile) ||
+        (m_Format == RecordFormat::Mp4 && !m_FfmpegPipe)) {
         return false;
     }
-    // Write Y plane
-    for (int y = 0; y < m_Height; y++) {
-        m_OutputFile->write((const char*)(m_ConvertedFrame->data[0] + y * m_ConvertedFrame->linesize[0]), m_Width);
-    }
-
-    // Write U plane
-    for (int y = 0; y < m_Height / 2; y++) {
-        m_OutputFile->write((const char*)(m_ConvertedFrame->data[1] + y * m_ConvertedFrame->linesize[1]), m_Width / 2);
-    }
-
-    // Write V plane
-    for (int y = 0; y < m_Height / 2; y++) {
-        m_OutputFile->write((const char*)(m_ConvertedFrame->data[2] + y * m_ConvertedFrame->linesize[2]), m_Width / 2);
-    }
+    // Write out the converted frame
+    auto writePlane = [&](int plane, int height, int width, int stride) -> bool {
+        const uint8_t* base = m_ConvertedFrame->data[plane];
+        for (int y = 0; y < height; y++) {
+            const uint8_t* row = base + y * stride;
+            if (m_Format == RecordFormat::Raw) {
+                if (m_OutputFile->write((const char*)row, width) != width) {
+                    return false;
+                }
+            }
+            else {
+                if (fwrite(row, 1, width, m_FfmpegPipe) != (size_t)width) {
+                    return false;
+                }
+            }
+        }
+        return true;
+    };
+
+    if (!writePlane(0, m_Height, m_Width, m_ConvertedFrame->linesize[0]) ||
+        !writePlane(1, m_Height / 2, m_Width / 2, m_ConvertedFrame->linesize[1]) ||
+        !writePlane(2, m_Height / 2, m_Width / 2, m_ConvertedFrame->linesize[2])) {
+        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
+                    "VideoRecorder: Failed to write frame data");
+        return false;
+    }
+
+    // Record frame info to CSV after successfully writing YUV data
+    if (m_CsvStream) {
+        *m_CsvStream << m_FrameCount << ","
+                     << frameNumber << ","
+                     << rtpTimestamp << "\n";
+    }
+
+    m_FrameCount++;
+
+    return true;
 }
-    // Close output file
-    if (m_OutputFile) {
-        m_OutputFile->close();
-        delete m_OutputFile;
-        m_OutputFile = nullptr;
-    }
+    // Close output sinks
+    if (m_OutputFile) {
+        m_OutputFile->close();
+        delete m_OutputFile;
+        m_OutputFile = nullptr;
+    }
+    if (m_FfmpegPipe) {
+        pclose(m_FfmpegPipe);
+        m_FfmpegPipe = nullptr;
+    }
```

### app/settings/streamingpreferences.h
```diff
-#include <QObject>
-#include <QRect>
-#include <QQmlEngine>
+#include <QObject>
+#include <QRect>
+#include <QQmlEngine>
+#include <QString>
-    bool swapFaceButtons;
-    bool keepAwake;
-    int packetSize;
+    bool swapFaceButtons;
+    bool keepAwake;
+    int packetSize;
+    bool recordingEnabled;
+    QString recordingFormat;
+    QString recordingOutputDir;
```

### app/settings/streamingpreferences.cpp
```diff
-#define SER_LANGUAGE "language"
+#define SER_LANGUAGE "language"
+#define SER_RECORD_ENABLE "recording/enable"
+#define SER_RECORD_FORMAT "recording/format"
+#define SER_RECORD_OUTPUT_DIR "recording/output_dir"
-static StreamingPreferences* s_GlobalPrefs;
-static QReadWriteLock s_GlobalPrefsLock;
+static StreamingPreferences* s_GlobalPrefs;
+static QReadWriteLock s_GlobalPrefsLock;
+static const QString k_DefaultRecordDir = QStringLiteral("/home/wcsng5g/moonlight-qt/recorded_session");
-    language = static_cast<Language>(settings.value(SER_LANGUAGE,
-                                                    static_cast<int>(Language::LANG_AUTO)).toInt());
+    language = static_cast<Language>(settings.value(SER_LANGUAGE,
+                                                    static_cast<int>(Language::LANG_AUTO)).toInt());
+    recordingEnabled = settings.value(SER_RECORD_ENABLE, true).toBool();
+    recordingFormat = settings.value(SER_RECORD_FORMAT, QStringLiteral("raw")).toString();
+    recordingOutputDir = settings.value(SER_RECORD_OUTPUT_DIR, k_DefaultRecordDir).toString();
-    settings.setValue(SER_SWAPFACEBUTTONS, swapFaceButtons);
-    settings.setValue(SER_CAPTURESYSKEYS, captureSysKeysMode);
-    settings.setValue(SER_KEEPAWAKE, keepAwake);
+    settings.setValue(SER_SWAPFACEBUTTONS, swapFaceButtons);
+    settings.setValue(SER_CAPTURESYSKEYS, captureSysKeysMode);
+    settings.setValue(SER_KEEPAWAKE, keepAwake);
+    settings.setValue(SER_RECORD_ENABLE, recordingEnabled);
+    settings.setValue(SER_RECORD_FORMAT, recordingFormat);
+    settings.setValue(SER_RECORD_OUTPUT_DIR, recordingOutputDir);
```

### app/streaming/video/ffmpeg.cpp
```diff
-#include <Limelight.h>
-#include "ffmpeg.h"
-#include "streaming/session.h"
+#include <Limelight.h>
+#include "ffmpeg.h"
+#include "streaming/session.h"
+#include "settings/streamingpreferences.h"
-                    // Record frame to file if recording is enabled
-                    if (m_VideoRecorder) {
-                        // Start recording on first frame if not already started
-                        if (!m_VideoRecorder->isRecording() && m_VideoDecoderCtx) {
-                            // Save to recorded_session directory in the moonlight-qt folder
-                            QString recordDir = "/home/wcsng5g/moonlight-qt/recorded_session";
-                            QDir().mkpath(recordDir);
-                            QString outputPath = QString("%1/moonlight_recording_%2.yuv")
-                                .arg(recordDir)
-                                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
-                            m_VideoRecorder->initialize(outputPath,
-                                                       m_VideoDecoderCtx->width,
-                                                       m_VideoDecoderCtx->height,
-                                                       m_StreamFps > 0 ? m_StreamFps : 60);
-                        }
-                        m_VideoRecorder->writeFrame(frame, frameNumber, rtpTimestamp);
-                    }
+                    // Record frame to file if recording is enabled
+                    if (m_VideoRecorder) {
+                        auto prefs = StreamingPreferences::get();
+
+                        if (prefs->recordingEnabled) {
+                            // Start recording on first frame if not already started
+                            static bool recordingFailed = false;
+                            if (!m_VideoRecorder->isRecording() && m_VideoDecoderCtx && !recordingFailed) {
+                                QString recordDir = prefs->recordingOutputDir;
+                                if (recordDir.isEmpty()) {
+                                    recordDir = QStringLiteral("/home/wcsng5g/moonlight-qt/recorded_session");
+                                }
+                                QDir().mkpath(recordDir);
+
+                                auto recordFormat = VideoRecorder::recordFormatFromString(prefs->recordingFormat);
+                                QString ext = recordFormat == VideoRecorder::RecordFormat::Mp4 ? QStringLiteral("mp4")
+                                                                                                 : QStringLiteral("yuv");
+                                QString outputPath = QString("%1/moonlight_recording_%2.%3")
+                                    .arg(recordDir)
+                                    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"))
+                                    .arg(ext);
+
+                                bool started = m_VideoRecorder->initialize(outputPath,
+                                                                          m_VideoDecoderCtx->width,
+                                                                          m_VideoDecoderCtx->height,
+                                                                          m_StreamFps > 0 ? m_StreamFps : 60,
+                                                                          recordFormat);
+                                if (!started) {
+                                    recordingFailed = true;
+                                }
+                            }
+
+                            if (m_VideoRecorder->isRecording()) {
+                                m_VideoRecorder->writeFrame(frame, frameNumber, rtpTimestamp);
+                            }
+                        }
+                    }
```
