#pragma once

#include <QString>
#include <QMutex>
#include <QFile>

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

    // Initialize the recorder with output path and video parameters
    bool initialize(const QString& outputPath, int width, int height, int fps);

    // Write a decoded frame to the output file
    bool writeFrame(AVFrame* frame);

    // Finalize and close the output file
    void finalize();

    // Check if recording is active
    bool isRecording() const { return m_Recording; }

    // Get the output file path
    QString getOutputPath() const { return m_OutputPath; }

private:
    bool m_Recording;
    QString m_OutputPath;

    QFile* m_OutputFile;
    SwsContext* m_SwsCtx;
    AVFrame* m_ConvertedFrame;
    uint8_t* m_FrameBuffer;

    int m_Width;
    int m_Height;
    int m_Fps;
    int64_t m_FrameCount;
    int m_LastInputFormat;

    QMutex m_Mutex;
};
