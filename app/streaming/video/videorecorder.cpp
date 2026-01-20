#include "videorecorder.h"

#include <SDL.h>
#include <QDateTime>
#include <QDir>
#include <QTextStream>

VideoRecorder::VideoRecorder()
    : m_Recording(false),
      m_OutputFile(nullptr),
      m_SwsCtx(nullptr),
      m_ConvertedFrame(nullptr),
      m_FrameBuffer(nullptr),
      m_Width(0),
      m_Height(0),
      m_Fps(0),
      m_FrameCount(0),
      m_LastInputFormat(AV_PIX_FMT_NONE)
{
}

VideoRecorder::~VideoRecorder()
{
    finalize();
}

bool VideoRecorder::initialize(const QString& outputPath, int width, int height, int fps)
{
    QMutexLocker locker(&m_Mutex);

    if (m_Recording) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VideoRecorder: Already recording");
        return false;
    }

    m_OutputPath = outputPath;
    m_Width = width;
    m_Height = height;
    m_Fps = fps;
    m_FrameCount = 0;

    // Open the output file for raw YUV data
    m_OutputFile = new QFile(outputPath);
    if (!m_OutputFile->open(QIODevice::WriteOnly)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VideoRecorder: Could not open output file: %s",
                     outputPath.toUtf8().constData());
        delete m_OutputFile;
        m_OutputFile = nullptr;
        return false;
    }

    // Allocate frame for format conversion to YUV420P
    m_ConvertedFrame = av_frame_alloc();
    if (!m_ConvertedFrame) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VideoRecorder: Could not allocate converted frame");
        finalize();
        return false;
    }

    m_ConvertedFrame->format = AV_PIX_FMT_YUV420P;
    m_ConvertedFrame->width = width;
    m_ConvertedFrame->height = height;

    // Allocate buffer for converted frame
    int bufferSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
    m_FrameBuffer = (uint8_t*)av_malloc(bufferSize);
    if (!m_FrameBuffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VideoRecorder: Could not allocate frame buffer");
        finalize();
        return false;
    }

    av_image_fill_arrays(m_ConvertedFrame->data, m_ConvertedFrame->linesize,
                         m_FrameBuffer, AV_PIX_FMT_YUV420P, width, height, 1);

    m_Recording = true;

    // Write metadata file alongside the YUV file
    QString metaPath = outputPath + ".meta";
    QFile metaFile(metaPath);
    if (metaFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&metaFile);
        out << "width=" << width << "\n";
        out << "height=" << height << "\n";
        out << "fps=" << fps << "\n";
        out << "format=yuv420p\n";
        out << "# To convert to MP4, run:\n";
        out << "# ffmpeg -f rawvideo -pix_fmt yuv420p -s " << width << "x" << height 
            << " -r " << fps << " -i \"" << outputPath << "\" -c:v libx264 -pix_fmt yuv420p output.mp4\n";
        metaFile.close();
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "VideoRecorder: Started recording YUV to %s (%dx%d @ %d fps)",
                outputPath.toUtf8().constData(), width, height, fps);

    return true;
}

bool VideoRecorder::writeFrame(AVFrame* frame)
{
    QMutexLocker locker(&m_Mutex);

    if (!m_Recording || !frame || !m_OutputFile) {
        return false;
    }

    AVFrame* swFrame = frame;
    AVFrame* tempFrame = nullptr;

    // Check if frame is in hardware format (e.g., VideoToolbox)
    // Hardware formats have frame->hw_frames_ctx set or format is a hw format
    if (frame->hw_frames_ctx || frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        // Transfer from GPU to CPU
        tempFrame = av_frame_alloc();
        if (!tempFrame) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VideoRecorder: Could not allocate temp frame");
            return false;
        }

        int ret = av_hwframe_transfer_data(tempFrame, frame, 0);
        if (ret < 0) {
            char errstr[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errstr, sizeof(errstr));
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VideoRecorder: Failed to transfer hw frame: %s", errstr);
            av_frame_free(&tempFrame);
            return false;
        }

        tempFrame->width = frame->width;
        tempFrame->height = frame->height;
        swFrame = tempFrame;
    }

    // Create or update sws context if needed for format conversion
    if (!m_SwsCtx || m_LastInputFormat != swFrame->format) {
        if (m_SwsCtx) {
            sws_freeContext(m_SwsCtx);
        }

        m_SwsCtx = sws_getContext(
            swFrame->width, swFrame->height, (AVPixelFormat)swFrame->format,
            m_Width, m_Height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (!m_SwsCtx) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VideoRecorder: Could not create sws context for format %d",
                         swFrame->format);
            if (tempFrame) av_frame_free(&tempFrame);
            return false;
        }

        m_LastInputFormat = swFrame->format;
    }

    // Convert the frame to YUV420P
    sws_scale(m_SwsCtx,
              swFrame->data, swFrame->linesize, 0, swFrame->height,
              m_ConvertedFrame->data, m_ConvertedFrame->linesize);

    // Free temp frame if we allocated one
    if (tempFrame) {
        av_frame_free(&tempFrame);
    }

    // Write Y plane
    for (int y = 0; y < m_Height; y++) {
        m_OutputFile->write((const char*)(m_ConvertedFrame->data[0] + y * m_ConvertedFrame->linesize[0]), m_Width);
    }

    // Write U plane
    for (int y = 0; y < m_Height / 2; y++) {
        m_OutputFile->write((const char*)(m_ConvertedFrame->data[1] + y * m_ConvertedFrame->linesize[1]), m_Width / 2);
    }

    // Write V plane
    for (int y = 0; y < m_Height / 2; y++) {
        m_OutputFile->write((const char*)(m_ConvertedFrame->data[2] + y * m_ConvertedFrame->linesize[2]), m_Width / 2);
    }

    m_FrameCount++;

    return true;
}

void VideoRecorder::finalize()
{
    QMutexLocker locker(&m_Mutex);

    if (!m_Recording) {
        return;
    }

    m_Recording = false;

    // Close output file
    if (m_OutputFile) {
        m_OutputFile->close();
        delete m_OutputFile;
        m_OutputFile = nullptr;
    }

    // Clean up
    if (m_FrameBuffer) {
        av_free(m_FrameBuffer);
        m_FrameBuffer = nullptr;
    }

    if (m_ConvertedFrame) {
        av_frame_free(&m_ConvertedFrame);
        m_ConvertedFrame = nullptr;
    }

    if (m_SwsCtx) {
        sws_freeContext(m_SwsCtx);
        m_SwsCtx = nullptr;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "VideoRecorder: Stopped recording. Total frames: %lld, Output: %s",
                (long long)m_FrameCount, m_OutputPath.toUtf8().constData());
}
