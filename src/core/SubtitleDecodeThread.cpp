#include "SubtitleDecodeThread.h"
#include "PacketQueue.h"
#include <QDebug>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
}

SubtitleDecodeThread::SubtitleDecodeThread(QObject *parent)
    : QThread(parent)
    , m_timeBase{1, 1000}
{
}

SubtitleDecodeThread::~SubtitleDecodeThread()
{
    stopDecode();
    wait();
}

void SubtitleDecodeThread::setCodecContext(AVCodecContext *ctx) { m_codecCtx = ctx; }
void SubtitleDecodeThread::setPacketQueue(PacketQueue *queue) { m_packetQueue = queue; }
void SubtitleDecodeThread::setTimeBase(AVRational tb) { m_timeBase = tb; }
void SubtitleDecodeThread::stopDecode()
{
    m_quit = true;
    if (m_packetQueue) {
        m_packetQueue->flush();
        m_packetQueue->setFinished(true);
    }
}

void SubtitleDecodeThread::setPausedRef(const std::atomic<bool> &paused)
{
    m_paused = &paused;
}

void SubtitleDecodeThread::clearSubtitles()
{
    QMutexLocker lock(&m_subMutex);
    m_subtitles.clear();
}

QString SubtitleDecodeThread::getSubtitleAt(qint64 positionUs) const
{
    QMutexLocker lock(&m_subMutex);
    for (const auto &entry : m_subtitles) {
        if (positionUs >= entry.startUs && positionUs < entry.endUs)
            return entry.text;
    }
    return {};
}

static QString cleanSubtitleText(const QString &raw)
{
    QString result;
    bool inBrace = false;
    bool inTag = false;
    for (QChar ch : raw) {
        if (ch == u'{') { inBrace = true; continue; }
        if (ch == u'}') { inBrace = false; continue; }
        if (ch == u'<') { inTag = true; continue; }
        if (ch == u'>') { inTag = false; continue; }
        if (!inBrace && !inTag) result += ch;
    }
    result.replace(QStringLiteral("\\N"), QStringLiteral("\n"));
    result.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    result.replace(QStringLiteral("\\h"), QStringLiteral(" "));
    return result.trimmed();
}

static QString parseAssDialogue(const QString &ass)
{
    // ASS 字段: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text
    // rect->ass 可能包含 8 或 9 个逗号；文本在最后一个逗号之后。
    // 部分解码器可能包含 "Dialogue:" 前缀。
    QString s = ass;
    if (s.startsWith(QLatin1String("Dialogue:"), Qt::CaseInsensitive))
        s = s.mid(9).trimmed();

    int lastComma = s.lastIndexOf(u',');
    if (lastComma < 0)
        return cleanSubtitleText(s);
    return cleanSubtitleText(s.mid(lastComma + 1));
}

void SubtitleDecodeThread::run()
{
    if (!m_codecCtx || !m_packetQueue)
        return;

    while (!m_quit) {
        if (m_paused && m_paused->load()) {
            while (!m_quit && m_paused->load())
                msleep(10);
            if (m_quit) break;
            continue;
        }

        AVPacket *pkt = m_packetQueue->pop();
        if (!pkt)
            break;

        int64_t pktPts = pkt->pts;
        if (pktPts == AV_NOPTS_VALUE) pktPts = 0;

        AVSubtitle sub;
        int gotSubtitle = 0;
        int ret = avcodec_decode_subtitle2(m_codecCtx, &sub, &gotSubtitle, pkt);
        av_packet_free(&pkt);

        if (ret < 0)
            continue;

        if (gotSubtitle) {
            SubtitleEntry entry;
            qint64 ptsUs = av_rescale_q(pktPts, m_timeBase, AV_TIME_BASE_Q);
            entry.startUs = ptsUs + static_cast<qint64>(sub.start_display_time) * 1000;
            entry.endUs   = ptsUs + static_cast<qint64>(sub.end_display_time) * 1000;
            if (entry.endUs <= entry.startUs)
                entry.endUs = entry.startUs + 3000000;

            for (unsigned i = 0; i < sub.num_rects; i++) {
                AVSubtitleRect *rect = sub.rects[i];
                QString text;

                if (rect->type == SUBTITLE_ASS && rect->ass) {
                    QString raw = QString::fromUtf8(rect->ass);
                    qDebug().noquote() << "[sub] ASS raw:" << raw;
                    text = parseAssDialogue(raw);
                } else if (rect->type == SUBTITLE_TEXT && rect->text) {
                    QString raw = QString::fromUtf8(rect->text);
                    qDebug().noquote() << "[sub] TEXT raw:" << raw;
                    text = cleanSubtitleText(raw);
                } else if (rect->ass) {
                    QString raw = QString::fromUtf8(rect->ass);
                    qDebug().noquote() << "[sub] ASS(type fallback) raw:" << raw;
                    text = parseAssDialogue(raw);
                } else if (rect->text) {
                    QString raw = QString::fromUtf8(rect->text);
                    qDebug().noquote() << "[sub] TEXT(type fallback) raw:" << raw;
                    text = cleanSubtitleText(raw);
                }
                qDebug().noquote() << "[sub] cleaned:" << text;

                if (!text.isEmpty()) {
                    if (!entry.text.isEmpty()) entry.text += u'\n';
                    entry.text += text;
                }
            }

            if (!entry.text.isEmpty()) {
                QMutexLocker lock(&m_subMutex);
                auto it = std::upper_bound(m_subtitles.begin(), m_subtitles.end(), entry.startUs,
                    [](qint64 val, const SubtitleEntry &e) { return val < e.startUs; });
                m_subtitles.insert(it, entry);
            }

            avsubtitle_free(&sub);
        }
    }
}
