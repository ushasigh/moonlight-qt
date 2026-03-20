#include "videorecorder.h"

#include <SDL.h>
#include <QDateTime>
#include <QDir>
#include <QTextStream>
#include <cstdio>

#ifdef Q_OS_WIN
#define popen _popen
#define pclose _pclose
#endif

VideoRecorder::VideoRecorder()
    : m_Recording(false),
      m_OutputPath(),
      m_Format(RecordFormat::Raw),
      m_OutputFile(nullptr),
      m_FfmpegPipe(nullptr),
      m_SwsCtx(nullptr),
      m_ConvertedFrame(nullptr),
      m_FrameBuffer(nullptr),
      m_Width(0),
      m_Height(0),
      m_Fps(0),
      m_FrameCount(0),
      m_LastInputFormat(AV_PIX_FMT_NONE),
      m_CsvFile(nullptr),
      m_CsvStream(nullptr)
{
}

VideoRecorder::~VideoRecorder()
{
    finalize();
}

VideoRecorder::RecordFormat VideoRecorder::recordFormatFromString(const QString& value)
{
    if (value.compare("mp4", Qt::CaseInsensitive) == 0) {
        return RecordFormat::Mp4;
    }
    return RecordFormat::Raw;
}

bool VideoRecorder::initialize(const QString& outputPath, int width, int height, int fps, RecordFormat format)
{
    QMutexLocker locker(&m_Mutex);

    if (m_Recording) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VideoRecorder: Already recording");
        return false;
    }

    m_OutputPath = outputPath;
    m_Format = format;
    m_Width = width;
    m_Height = height;
    m_Fps = fps;
    m_FrameCount = 0;

    // Open the output sink based on format
    if (m_Format == RecordFormat::Raw) {
        m_OutputFile = new QFile(outputPath);
        if (!m_OutputFile->open(QIODevice::WriteOnly)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VideoRecorder: Could not open output file: %s",
                         outputPath.toUtf8().constData());
            delete m_OutputFile;
            m_OutputFile = nullptr;
            return false;
        }
    }
    else {
        QString cmd = QStringLiteral(
            "ffmpeg -y -hide_banner -loglevel warning "
            "-f rawvideo -pix_fmt yuv420p -s %1x%2 -r %3 -i pipe:0 "
            "-c:v libx264 -preset ultrafast -crf 18 -pix_fmt yuv420p \"%4\"")
                .arg(width)
                .arg(height)
                .arg(fps)
                .arg(outputPath);
        m_FfmpegPipe = popen(cmd.toUtf8().constData(), "w");
        if (!m_FfmpegPipe) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VideoRecorder: Could not start ffmpeg pipe: %s",
                         cmd.toUtf8().constData());
            return false;
        }
    }

    // Mark as recording so finalize() can clean up on failures below
    m_Recording = true;

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

    // フレームcsvファイルの作成
     QString csvPath = outputPath + ".frames.csv";
    m_CsvFile = new QFile(csvPath);
    if (m_CsvFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_CsvStream = new QTextStream(m_CsvFile);
        *m_CsvStream << "local_frame_idx,frame_nr,rtp_timestamp\n";
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VideoRecorder: Could not open CSV: %s",
                    csvPath.toUtf8().constData());
        delete m_CsvFile;
        m_CsvFile = nullptr;
    }

    // Write metadata file alongside the output
    QString metaPath = outputPath + ".meta";
    QFile metaFile(metaPath);
    if (metaFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&metaFile);
        out << "width=" << width << "\n";
        out << "height=" << height << "\n";
        out << "fps=" << fps << "\n";
        out << "format=yuv420p\n";
        if (m_Format == RecordFormat::Raw) {
            out << "# To convert to MP4, run:\n";
            out << "# ffmpeg -f rawvideo -pix_fmt yuv420p -s " << width << "x" << height 
                << " -r " << fps << " -i \"" << outputPath << "\" -c:v libx264 -pix_fmt yuv420p output.mp4\n";
        }
        metaFile.close();
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "VideoRecorder: Started recording to %s (%dx%d @ %d fps, format=%s)",
                outputPath.toUtf8().constData(), width, height, fps,
                m_Format == RecordFormat::Raw ? "raw" : "mp4");

    return true;
}

bool VideoRecorder::writeFrame(AVFrame* frame, int frameNumber, uint32_t rtpTimestamp)
{
    QMutexLocker locker(&m_Mutex);

    if (!m_Recording || !frame ||
        (m_Format == RecordFormat::Raw && !m_OutputFile) ||
        (m_Format == RecordFormat::Mp4 && !m_FfmpegPipe)) {
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

    // Write out the converted frame
    auto writePlane = [&](int plane, int height, int width, int stride) -> bool {
        const uint8_t* base = m_ConvertedFrame->data[plane];
        for (int y = 0; y < height; y++) {
            const uint8_t* row = base + y * stride;
            if (m_Format == RecordFormat::Raw) {
                if (m_OutputFile->write((const char*)row, width) != width) {
                    return false;
                }
            }
            else {
                if (fwrite(row, 1, width, m_FfmpegPipe) != (size_t)width) {
                    return false;
                }
            }
        }
        return true;
    };

    if (!writePlane(0, m_Height, m_Width, m_ConvertedFrame->linesize[0]) ||
        !writePlane(1, m_Height / 2, m_Width / 2, m_ConvertedFrame->linesize[1]) ||
        !writePlane(2, m_Height / 2, m_Width / 2, m_ConvertedFrame->linesize[2])) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VideoRecorder: Failed to write frame data");
        return false;
    }

    // Record frame info to CSV after successfully writing YUV data
    if (m_CsvStream) {
        *m_CsvStream << m_FrameCount << ","
                     << frameNumber << ","
                     << rtpTimestamp << "\n";
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

    // Close CSV resources
    if (m_CsvStream) {
        m_CsvStream->flush();
        delete m_CsvStream;
        m_CsvStream = nullptr;
    }
    if (m_CsvFile) {
        m_CsvFile->close();
        delete m_CsvFile;
        m_CsvFile = nullptr;
    }

    // Close output sinks
    if (m_OutputFile) {
        m_OutputFile->close();
        delete m_OutputFile;
        m_OutputFile = nullptr;
    }
    if (m_FfmpegPipe) {
        pclose(m_FfmpegPipe);
        m_FfmpegPipe = nullptr;
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
