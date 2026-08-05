#include "SubtitleDecodeThread.h"
#include "PacketQueue.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
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

// 结束时间缺失/非法（<= 开始时间）时：补为下一条开始时间，末条补到播放结束。
// 调用前须保证 subs 已按 startUs 升序。
static void patchMissingEnds(QList<SubtitleEntry> &subs)
{
    for (int i = 0; i < subs.size(); ++i) {
        if (subs[i].endUs <= subs[i].startUs) {
            if (i + 1 < subs.size())
                subs[i].endUs = subs[i + 1].startUs;
            else
                subs[i].endUs = INT64_MAX;
        }
    }
}

// --- 外挂字幕解析辅助 ---

// SRT 时间戳 H:MM:SS,mmm 转毫秒（支持小时个位、毫秒 1~3 位）
static qint64 parseSrtTime(const QString &s)
{
    QStringList hm = s.split(u':');
    if (hm.size() != 3)
        return -1;
    QStringList secMs = hm[2].split(u',');
    if (secMs.size() != 2)
        return -1;

    qint64 h = hm[0].toLongLong();
    qint64 m = hm[1].toLongLong();
    qint64 sec = secMs[0].toLongLong();
    QString frac = secMs[1];
    while (frac.size() < 3)
        frac += u'0';
    if (frac.size() > 3)
        frac.truncate(3);
    qint64 ms = frac.toLongLong();
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
        m_packetQueue->requestQuit();
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
    // 二分定位最后一条 startUs <= positionUs 的条目，再向前回退：
    // SRT/ASS 条目允许重叠（一条未结束、下一条已开始），
    // 应显示"最晚开始且仍在显示区间内"的一条，避免前一条被提前截断。
    auto it = std::upper_bound(m_subtitles.begin(), m_subtitles.end(), positionUs,
        [](qint64 val, const SubtitleEntry &e) { return val < e.startUs; });
    const int kMaxWalkback = 16;
    for (int i = 0; i < kMaxWalkback && it != m_subtitles.begin(); ++i) {
        --it;
        if (positionUs >= it->startUs && positionUs < it->endUs)
            return it->text;
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
        int64_t pktDuration = pkt->duration;

        AVSubtitle sub;
        int gotSubtitle = 0;
        int ret = avcodec_decode_subtitle2(m_codecCtx, &sub, &gotSubtitle, pkt);
        av_packet_free(&pkt);

        if (ret < 0)
            continue;

        if (gotSubtitle) {
            SubtitleEntry entry;
            // 条目保持原始流时间戳（与主时钟/音频时钟同基），不做任何偏移换算。
            qint64 ptsUs = av_rescale_q(pktPts, m_timeBase, AV_TIME_BASE_Q);
            entry.startUs = ptsUs + static_cast<qint64>(sub.start_display_time) * 1000;
            // 结束时间优先级：end_display_time > 包 duration（MKV BlockDuration）> 下一条字幕开始时间
            entry.endUs = INT64_MAX;
            if (sub.end_display_time > 0)
                entry.endUs = ptsUs + static_cast<qint64>(sub.end_display_time) * 1000;
            else if (pktDuration > 0)
                entry.endUs = av_rescale_q(pktPts + pktDuration, m_timeBase, AV_TIME_BASE_Q);

            for (unsigned i = 0; i < sub.num_rects; i++) {
                AVSubtitleRect *rect = sub.rects[i];
                QString text;

                if (rect->type == SUBTITLE_ASS && rect->ass) {
                    QString raw = QString::fromUtf8(rect->ass);
#ifdef QT_DEBUG
                    qDebug().noquote() << "[sub] ASS raw:" << raw;
#endif
                    text = parseAssDialogue(raw);
                } else if (rect->type == SUBTITLE_TEXT && rect->text) {
                    QString raw = QString::fromUtf8(rect->text);
#ifdef QT_DEBUG
                    qDebug().noquote() << "[sub] TEXT raw:" << raw;
#endif
                    text = cleanSubtitleText(raw);
                } else if (rect->ass) {
                    QString raw = QString::fromUtf8(rect->ass);
#ifdef QT_DEBUG
                    qDebug().noquote() << "[sub] ASS(type fallback) raw:" << raw;
#endif
                    text = parseAssDialogue(raw);
                } else if (rect->text) {
                    QString raw = QString::fromUtf8(rect->text);
#ifdef QT_DEBUG
                    qDebug().noquote() << "[sub] TEXT(type fallback) raw:" << raw;
#endif
                    text = cleanSubtitleText(raw);
                }
#ifdef QT_DEBUG
                qDebug().noquote() << "[sub] cleaned:" << text;
#endif

                if (!text.isEmpty()) {
                    if (!entry.text.isEmpty()) entry.text += u'\n';
                    entry.text += text;
                }
            }

            if (!entry.text.isEmpty()) {
                QMutexLocker lock(&m_subMutex);
                // 上一条字幕若结束时间未知，收尾到本条开始时间
                if (!m_subtitles.isEmpty()) {
                    SubtitleEntry &last = m_subtitles.last();
                    if (last.endUs == INT64_MAX && entry.startUs > last.startUs)
                        last.endUs = entry.startUs;
                }
                auto it = std::upper_bound(m_subtitles.begin(), m_subtitles.end(), entry.startUs,
                    [](qint64 val, const SubtitleEntry &e) { return val < e.startUs; });
                m_subtitles.insert(it, entry);
            }

            avsubtitle_free(&sub);
        }
    }

    // 解码结束：中间仍有未知结束时间的条目（乱序插入场景），收尾到各自下一条
    {
        QMutexLocker lock(&m_subMutex);
        for (int i = 0; i + 1 < m_subtitles.size(); ++i) {
            if (m_subtitles[i].endUs == INT64_MAX && m_subtitles[i + 1].startUs > m_subtitles[i].startUs)
                m_subtitles[i].endUs = m_subtitles[i + 1].startUs;
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
        if (!m.hasMatch()) {
#ifdef QT_DEBUG
            qDebug().noquote() << "[sub] srt block skipped (no time line):" << lines[lineIdx].left(48);
#endif
            continue;
        }

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
        e.text = text;
        subs.append(e);
    }

    std::stable_sort(subs.begin(), subs.end(),
        [](const SubtitleEntry &a, const SubtitleEntry &b) { return a.startUs < b.startUs; });
    patchMissingEnds(subs);
#ifdef QT_DEBUG
    for (const SubtitleEntry &e : subs) {
        qDebug().noquote() << QStringLiteral("[sub] srt %1us..%2us \"%3\"")
            .arg(e.startUs).arg(e.endUs).arg(e.text.left(16));
    }
#endif
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
            e.text = text;
            subs.append(e);
        }
    }

    std::stable_sort(subs.begin(), subs.end(),
        [](const SubtitleEntry &a, const SubtitleEntry &b) { return a.startUs < b.startUs; });
    patchMissingEnds(subs);
    qDebug() << "[extsub] ASS loaded:" << subs.size() << "entries from" << QFileInfo(path).fileName();
    return subs;
}

QList<SubtitleEntry> SubtitleDecodeThread::loadLrc(const QString &path)
{
    QList<SubtitleEntry> entries;
    QString content = readAllText(path);
    if (content.isEmpty())
        return entries;

    static const QRegularExpression tsRe(QStringLiteral(R"(\[(\d{1,3}):(\d{2})[.:](\d{2,3})\])"));
    QStringList lines = content.split(QRegularExpression(QStringLiteral("\\r?\\n")));

    qint64 offsetMs = 0;   // [offset:] 元数据，需加到所有时间戳上

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        // 跳过元数据标签 [ti:xxx], [ar:xxx], [al:xxx], [by:xxx], [offset:xxx]
        if (trimmed.startsWith(u'[') && trimmed.contains(u':') && !trimmed[1].isDigit()) {
            if (trimmed.startsWith(u"[offset:", Qt::CaseInsensitive)) {
                bool ok = false;
                qint64 off = trimmed.mid(8).trimmed().remove(u']').toLongLong(&ok);
                if (ok)
                    offsetMs = off;
            }
            continue;
        }

        // 收集所有时间戳
        QList<qint64> timestamps;
        QString text;
        int pos = 0;
        QRegularExpressionMatch match;
        while ((match = tsRe.match(trimmed, pos)).hasMatch()) {
            qint64 min = match.captured(1).toLongLong();
            qint64 sec = match.captured(2).toLongLong();
            QString fracStr = match.captured(3);
            qint64 ms;
            if (fracStr.length() == 2)
                ms = fracStr.toLongLong() * 10;  // [mm:ss.xx]
            else
                ms = fracStr.toLongLong();        // [mm:ss.xxx]
            timestamps.append((min * 60 + sec) * 1000 + ms);
            pos = match.capturedEnd();
        }

        text = trimmed.mid(pos).trimmed();
        if (timestamps.isEmpty() || text.isEmpty())
            continue;

        for (qint64 ts : timestamps) {
            SubtitleEntry e;
            e.startUs = (ts + offsetMs) * 1000;  // 转换为微秒（含 offset 偏移）
            e.text = text;
            entries.append(e);
        }
    }

    // 按时间排序
    std::stable_sort(entries.begin(), entries.end(),
        [](const SubtitleEntry &a, const SubtitleEntry &b) { return a.startUs < b.startUs; });

    // 设置 endUs 为下一条的开始时间（最后一条使用 INT64_MAX）
    for (int i = 0; i < entries.size(); i++) {
        if (i + 1 < entries.size())
            entries[i].endUs = entries[i + 1].startUs;
        else
            entries[i].endUs = INT64_MAX;
    }

    qDebug() << "[lrc] loaded:" << entries.size() << "entries from" << QFileInfo(path).fileName();
    return entries;
}

QList<SubtitleEntry> SubtitleDecodeThread::loadFromFile(const QString &path)
{
    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == u"srt")
        return loadSrt(path);
    if (suffix == u"ass" || suffix == u"ssa")
        return loadAss(path);
    if (suffix == u"lrc")
        return loadLrc(path);
    qWarning() << "[extsub] unsupported format:" << suffix;
    return {};
}
