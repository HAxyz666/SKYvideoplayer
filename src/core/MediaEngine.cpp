#include "MediaEngine.h"
#include "AVSyncController.h"
#include "AudioOutput.h"
#include <QDebug>
#include <cstring>

MediaEngine::MediaEngine(const QString &filename, QObject *parent)
    : QThread(parent)
    , m_filename(filename)
    , m_stopRequested(false)
    , m_formatCtx(nullptr)
    , m_videoStreamIndex(-1)
    , m_audioStreamIndex(-1)
    , m_videoCodecCtx(nullptr)
    , m_audioCodecCtx(nullptr)
    , m_swsCtx(nullptr)
    , m_swrCtx(nullptr)
    , m_startTime(0)
    , m_syncController(new AVSyncController(this))
    , m_audioOutput(new AudioOutput(this))
{
}

MediaEngine::~MediaEngine()
{
    stop();
    wait();
    cleanup();
}

void MediaEngine::stop()
{
    m_stopRequested = true;
}

bool MediaEngine::initFFmpeg()
{
    if (avformat_open_input(&m_formatCtx, m_filename.toUtf8().constData(), nullptr, nullptr) != 0) {
        qCritical() << "Could not open file:" << m_filename;
        return false;
    }

    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        qCritical() << "Could not find stream info";
        return false;
    }

    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex == -1) {
            m_videoStreamIndex = i;
        } else if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex == -1) {
            m_audioStreamIndex = i;
        }
    }

    if (m_videoStreamIndex == -1) {
        qCritical() << "No video stream found";
        return false;
    }

    AVCodecParameters *videoPar = m_formatCtx->streams[m_videoStreamIndex]->codecpar;
    const AVCodec *videoCodec = avcodec_find_decoder(videoPar->codec_id);
    if (!videoCodec) {
        qCritical() << "Video codec not found";
        return false;
    }

    m_videoCodecCtx = avcodec_alloc_context3(videoCodec);
    avcodec_parameters_to_context(m_videoCodecCtx, videoPar);

    if (avcodec_open2(m_videoCodecCtx, videoCodec, nullptr) < 0) {
        qCritical() << "Could not open video codec";
        return false;
    }

    m_swsCtx = sws_getContext(
        m_videoCodecCtx->width, m_videoCodecCtx->height, m_videoCodecCtx->pix_fmt,
        m_videoCodecCtx->width, m_videoCodecCtx->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (m_audioStreamIndex != -1) {
        AVCodecParameters *audioPar = m_formatCtx->streams[m_audioStreamIndex]->codecpar;
        const AVCodec *audioCodec = avcodec_find_decoder(audioPar->codec_id);

        if (audioCodec) {
            m_audioCodecCtx = avcodec_alloc_context3(audioCodec);
            avcodec_parameters_to_context(m_audioCodecCtx, audioPar);

            if (avcodec_open2(m_audioCodecCtx, audioCodec, nullptr) >= 0) {
                m_swrCtx = swr_alloc();

                AVChannelLayout in_ch_layout = m_audioCodecCtx->ch_layout;
                av_opt_set_chlayout(m_swrCtx, "in_chlayout", &in_ch_layout, 0);
                av_opt_set_int(m_swrCtx, "in_sample_rate", m_audioCodecCtx->sample_rate, 0);
                av_opt_set_sample_fmt(m_swrCtx, "in_sample_fmt", m_audioCodecCtx->sample_fmt, 0);

                AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
                av_opt_set_chlayout(m_swrCtx, "out_chlayout", &out_ch_layout, 0);
                av_opt_set_int(m_swrCtx, "out_sample_rate", 44100, 0);
                av_opt_set_sample_fmt(m_swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

                swr_init(m_swrCtx);
            }
        }
    }

    av_dump_format(m_formatCtx, 0, m_filename.toUtf8().constData(), 0);

    return true;
}

void MediaEngine::cleanup()
{
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
    }

    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
    }

    if (m_audioCodecCtx) {
        avcodec_free_context(&m_audioCodecCtx);
    }
    if (m_videoCodecCtx) {
        avcodec_free_context(&m_videoCodecCtx);
    }

    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
    }
}

void MediaEngine::run()
{
    if (!initFFmpeg()) {
        return;
    }

    bool audioInited = m_audioOutput->init(m_audioCodecCtx);

    AVPacket *packet = av_packet_alloc();
    AVFrame *videoFrame = av_frame_alloc();
    AVFrame *audioFrame = av_frame_alloc();
    AVFrame *rgbFrame = av_frame_alloc();

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, m_videoCodecCtx->width, m_videoCodecCtx->height, 1);
    uint8_t *buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24,
                         m_videoCodecCtx->width, m_videoCodecCtx->height, 1);

    m_startTime = av_gettime();
    m_syncController->reset();

    while (!m_stopRequested && av_read_frame(m_formatCtx, packet) >= 0) {

        if (packet->stream_index == m_videoStreamIndex) {
            int ret = avcodec_send_packet(m_videoCodecCtx, packet);

            while (ret >= 0) {
                ret = avcodec_receive_frame(m_videoCodecCtx, videoFrame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }

                double pts = 0;
                if (videoFrame->pts != AV_NOPTS_VALUE) {
                    pts = videoFrame->pts * av_q2d(m_formatCtx->streams[m_videoStreamIndex]->time_base);
                }

                pts = m_syncController->synchronizeVideo(videoFrame, pts, m_formatCtx, m_videoStreamIndex);

                pts *= 1000000;

                int64_t realTime = av_gettime() - m_startTime;
                while (pts > realTime && !m_stopRequested) {
                    SDL_Delay(10);
                    realTime = av_gettime() - m_startTime;
                }

                sws_scale(m_swsCtx, videoFrame->data, videoFrame->linesize, 0,
                          m_videoCodecCtx->height, rgbFrame->data, rgbFrame->linesize);

                QImage image(rgbFrame->data[0], m_videoCodecCtx->width, m_videoCodecCtx->height,
                             rgbFrame->linesize[0], QImage::Format_RGB888);
                emit frameReady(image.copy());
            }
        }
        else if (packet->stream_index == m_audioStreamIndex && m_audioCodecCtx) {
            int ret = avcodec_send_packet(m_audioCodecCtx, packet);

            while (ret >= 0) {
                ret = avcodec_receive_frame(m_audioCodecCtx, audioFrame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }

                int outSamples = swr_get_out_samples(m_swrCtx, audioFrame->nb_samples);

                uint8_t *output = nullptr;
                av_samples_alloc(&output, nullptr, 2, outSamples, AV_SAMPLE_FMT_S16, 0);

                int converted = swr_convert(m_swrCtx, &output, outSamples,
                                            (const uint8_t **)audioFrame->data, audioFrame->nb_samples);

                m_audioOutput->enqueue(output, converted * 2 * 2);
            }
        }

        av_packet_unref(packet);
    }

    av_free(buffer);
    av_frame_free(&rgbFrame);
    av_frame_free(&audioFrame);
    av_frame_free(&videoFrame);
    av_packet_free(&packet);

    emit playbackFinished();
}
