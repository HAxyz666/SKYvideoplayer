#include "SubtitleDecodeThread.h"
#include "PacketQueue.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
}

// --- 字幕文本清洗工具 ---

// 剥离 {...} 和 <...> 标签，处理 ASS 换行/空格转义。
// 供内嵌字幕（FFmpeg 解码后）和外挂字幕（文件解析）共用。
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

// 内嵌 ASS rect->ass 文本提取：文本在最后一个逗号之后。
static QString parseAssDialogue(const QString &ass)
{
    QString s = ass;
    if (s.startsWith(QLatin1String("Dialogue:"), Qt::CaseInsensitive))
        s = s.mid(9).trimmed();

    int lastComma = s.lastIndexOf(u',');
    if (lastComma < 0)
        return cleanSubtitleText(s);
    return cleanSubtitleText(s.mid(lastComma + 1));
}

// --- 外挂字幕解析辅助 ---

// SRT 时间戳 HH:MM:SS,mmm 转毫秒
static qint64 parseSrtTime(const QString &s)
{
    if (s.length() < 12) return -1;
    qint64 h = s.mid(0, 2).toLongLong();
    qint64 m = s.mid(3, 2).toLongLong();
    qint64 sec = s.mid(6, 2).toLongLong();
    qint64 ms = s.mid(9, 3).toLongLong();
    return ((h * 60 + m) * 60 + sec) * 1000 + ms;
}

// ASS 时间戳 H:MM:SS.cc 转毫秒
static qint64 parseAssTime(const QString &s)
{
    QStringList parts = s.split(u':');
    if (parts.size() != 3) return -1;
    qint64 h = parts[0].toLongLong();
    qint64 m = parts[1].toLongLong();
    qint64 sec = parts[2].split(u'.').value(0).toLongLong();
    qint64 cs = parts[2].split(u'.').value(1).toLongLong();
    return ((h * 60 + m) * 60 + sec) * 1000 + cs * 10;
}

static QString readAllText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[extsub] cannot open:" << path << f.errorString();
        return {};
    }
    QByteArray raw = f.readAll();
    if (raw.startsWith("\xEF\xBB\xBF"))
        raw = raw.mid(3);
    QString text = QString::fromUtf8(raw);
    if (text.isNull())
        text = QString::fromLocal8Bit(raw);
    return text;
}

// --- SubtitleDecodeThread 实现 ---

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

void SubtitleDecodeThread::setExternalSubtitles(const QList<SubtitleEntry> &subs)
{
    QMutexLocker lock(&m_subMutex);
    m_subtitles = subs;
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

// --- 外挂字幕文件加载 ---

QList<SubtitleEntry> SubtitleDecodeThread::loadSrt(const QString &path)
{
    QList<SubtitleEntry> subs;
    QString content = readAllText(path);
    if (content.isEmpty())
        return subs;

    static const QRegularExpression blockSep(QStringLiteral("\\r?\\n\\s*\\r?\\n"));
    QStringList blocks = content.split(blockSep, Qt::SkipEmptyParts);

    static const QRegularExpression timeRe(
        QStringLiteral("(\\d{1,2}:\\d{2}:\\d{2}[.,]\\d{1,3})\\s*-->\\s*(\\d{1,2}:\\d{2}:\\d{2}[.,]\\d{1,3})"));

    for (const QString &block : blocks) {
        QStringList lines = block.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
        if (lines.isEmpty()) continue;

        int lineIdx = 0;
        if (lines[0].trimmed().toInt() > 0)
            lineIdx = 1;

        if (lineIdx >= lines.size()) continue;
        QRegularExpressionMatch m = timeRe.match(lines[lineIdx]);
        if (!m.hasMatch()) continue;

        QString startStr = m.captured(1).replace(u'.', u',');
        QString endStr = m.captured(2).replace(u'.', u',');
        qint64 startMs = parseSrtTime(startStr);
        qint64 endMs = parseSrtTime(endStr);
        if (startMs < 0 || endMs < 0) continue;

        QString text = lines.mid(lineIdx + 1).join(u'\n');
        text = cleanSubtitleText(text);
        if (text.isEmpty()) continue;

        SubtitleEntry e;
        e.startUs = startMs * 1000;
        e.endUs = endMs * 1000;
        if (e.endUs <= e.startUs) e.endUs = e.startUs + 3000000;
        e.text = text;
        subs.append(e);
    }

    std::stable_sort(subs.begin(), subs.end(),
        [](const SubtitleEntry &a, const SubtitleEntry &b) { return a.startUs < b.startUs; });
    qDebug() << "[extsub] SRT loaded:" << subs.size() << "entries from" << QFileInfo(path).fileName();
    return subs;
}

QList<SubtitleEntry> SubtitleDecodeThread::loadAss(const QString &path)
{
    QList<SubtitleEntry> subs;
    QString content = readAllText(path);
    if (content.isEmpty())
        return subs;

    QStringList lines = content.split(QRegularExpression(QStringLiteral("\\r?\\n")));

    int textCol = -1, startCol = -1, endCol = -1;
    bool inEvents = false;

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();

        if (trimmed.startsWith(u"[Events]", Qt::CaseInsensitive)) {
            inEvents = true;
            continue;
        }
        if (trimmed.startsWith(u'[') && trimmed.endsWith(u']') && inEvents)
            break;
        if (!inEvents) continue;

        if (trimmed.startsWith(u"Format:", Qt::CaseInsensitive)) {
            QString fmt = trimmed.mid(7).trimmed();
            QStringList fields = fmt.split(u',');
            for (int i = 0; i < fields.size(); ++i) {
                QString f = fields[i].trimmed().toLower();
                if (f == u"text") textCol = i;
                else if (f == u"start") startCol = i;
                else if (f == u"end") endCol = i;
            }
            continue;
        }

        if (trimmed.startsWith(u"Dialogue:", Qt::CaseInsensitive)) {
            if (textCol < 0 || startCol < 0 || endCol < 0)
                continue;
            QString body = trimmed.mid(9).trimmed();
            int maxCol = qMax(qMax(textCol, startCol), endCol);
            QStringList parts = body.split(u',', Qt::KeepEmptyParts);
            if (parts.size() <= maxCol) continue;

            qint64 startMs = parseAssTime(parts[startCol].trimmed());
            qint64 endMs = parseAssTime(parts[endCol].trimmed());
            if (startMs < 0 || endMs < 0) continue;

            // Text 字段：第 textCol 个字段之前的最后一个逗号之后的所有内容。
            int commaIdx = -1;
            int cur = 0;
            for (int i = 0; i < body.size(); ++i) {
                if (body[i] == u',') {
                    if (cur == textCol - 1) { commaIdx = i; break; }
                    cur++;
                }
            }
            if (commaIdx < 0) continue;
            QString text = body.mid(commaIdx + 1);
            text = cleanSubtitleText(text);
            if (text.isEmpty()) continue;

            SubtitleEntry e;
            e.startUs = startMs * 1000;
            e.endUs = endMs * 1000;
            if (e.endUs <= e.startUs) e.endUs = e.startUs + 3000000;
            e.text = text;
            subs.append(e);
        }
    }

    std::stable_sort(subs.begin(), subs.end(),
        [](const SubtitleEntry &a, const SubtitleEntry &b) { return a.startUs < b.startUs; });
    qDebug() << "[extsub] ASS loaded:" << subs.size() << "entries from" << QFileInfo(path).fileName();
    return subs;
}

QList<SubtitleEntry> SubtitleDecodeThread::loadFromFile(const QString &path)
{
    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == u"srt")
        return loadSrt(path);
    if (suffix == u"ass" || suffix == u"ssa")
        return loadAss(path);
    qWarning() << "[extsub] unsupported format:" << suffix;
    return {};
}
