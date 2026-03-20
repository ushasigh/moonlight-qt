#pragma once

#include <QString>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <cstdio>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

class VideoRecorder {
public:
    VideoRecorder();
    ~VideoRecorder();

    enum class RecordFormat {
        Raw,
        Mp4,
    };

    static RecordFormat recordFormatFromString(const QString& value);

    // Initialize the recorder with output path and video parameters
    bool initialize(const QString& outputPath, int width, int height, int fps, RecordFormat format);

    // Write a decoded frame to the output file
    bool writeFrame(AVFrame* frame, int frameNumber, uint32_t rtpTimestamp);

    // Finalize and close the output file
    void finalize();

    // Check if recording is active
    bool isRecording() const { return m_Recording; }

    // Get the output file path
    QString getOutputPath() const { return m_OutputPath; }

private:
    bool m_Recording;
    QString m_OutputPath;

    RecordFormat m_Format;

    QFile* m_OutputFile;
    FILE* m_FfmpegPipe;
    SwsContext* m_SwsCtx;
    AVFrame* m_ConvertedFrame;
    uint8_t* m_FrameBuffer;

    int m_Width;
    int m_Height;
    int m_Fps;
    int64_t m_FrameCount;
    int m_LastInputFormat;

    QMutex m_Mutex;

    QFile* m_CsvFile;
    QTextStream* m_CsvStream;
};
