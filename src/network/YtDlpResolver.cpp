#include "YtDlpResolver.h"
#include "ToolProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>

// 单条格式描述（仅解析所需字段）
struct FormatEntry {
    QString url;
    QVariantMap headers;
    QString vcodec;
    QString acodec;
    QString protocol;
    QString ext;
    int width = 0;
    int height = 0;
    double tbr = 0.0;
    double abr = 0.0;
    double fps = 0.0;

    bool hasVideo() const { return !vcodec.isEmpty() && vcodec != u"none"; }
    bool hasAudio() const { return !acodec.isEmpty() && acodec != u"none"; }
};

static QVariantMap jsonToHeaders(const QJsonValue &v)
{
    QVariantMap map;
    if (!v.isObject())
        return map;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
        map.insert(it.key(), it.value().toVariant());
    return map;
}

// 从 stdout 提取第一个 JSON 对象（yt-dlp 对播放列表会输出多行 JSON，
// 取首个视频条目即可；用括号扫描避开字符串内的花括号）。
static QByteArray extractFirstJsonObject(const QByteArray &data)
{
    const int start = data.indexOf('{');
    if (start < 0)
        return data;
    bool inString = false;
    bool escape = false;
    int depth = 0;
    for (int i = start; i < data.size(); ++i) {
        const char c = data[i];
        if (inString) {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0)
                return data.mid(start, i - start + 1);
        }
    }
    return data.mid(start);
}

// 从 stderr 提取最后一条有效错误（去掉 "ERROR: " 前缀）
static QString extractStderrError(const QByteArray &stderrData)
{
    const QStringList lines = QString::fromUtf8(stderrData)
        .split(u'\n', Qt::SkipEmptyParts);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        QString line = it->trimmed();
        if (line.startsWith(u"ERROR:"))
            line = line.mid(6).trimmed();
        if (!line.isEmpty())
            return line;
    }
    return QString();
}

// 视频格式优劣：分辨率优先，其次总码率/帧率
static bool isBetterVideo(const FormatEntry &a, const FormatEntry &b)
{
    const int aRes = a.height * (a.width ? a.width : a.height);
    const int bRes = b.height * (b.width ? b.width : b.height);
    if (aRes != bRes)
        return aRes > bRes;
    if (!qFuzzyCompare(a.tbr, b.tbr))
        return a.tbr > b.tbr;
    return a.fps > b.fps;
}

// 音频格式优劣：音频码率优先，其次优先 m3u8（FFmpeg 兼容性）
static bool isBetterAudio(const FormatEntry &a, const FormatEntry &b)
{
    const double aBit = a.abr > 0 ? a.abr : a.tbr;
    const double bBit = b.abr > 0 ? b.abr : b.tbr;
    if (!qFuzzyCompare(aBit, bBit))
        return aBit > bBit;
    const bool aHls = a.protocol.startsWith(u"m3u8");
    const bool bHls = b.protocol.startsWith(u"m3u8");
    if (aHls != bHls)
        return aHls;
    return false;
}

YtDlpResolver::YtDlpResolver(QObject *parent)
    : StreamResolver(parent)
    , m_runner(new ToolRunner(this))
{
    connect(m_runner, &ToolRunner::finished,
            this, &YtDlpResolver::onProcessFinished);
}

YtDlpResolver::~YtDlpResolver() = default;

void YtDlpResolver::resolve(const QString &url)
{
    m_url = url;

    if (!ToolProvider::ytDlpAvailable()) {
        ResolveResult r;
        r.ok = false;
        r.error = tr("未检测到 yt-dlp，请先安装：pip install yt-dlp");
        emit finished(r);
        return;
    }

    m_runner->start(ToolProvider::ytDlpPath(),
                    { QStringLiteral("-j"),
                      QStringLiteral("--no-warnings"),
                      QStringLiteral("--skip-download"),
                      QStringLiteral("--no-color"),
                      url },
                    30000);
}

void YtDlpResolver::cancel()
{
    m_runner->cancel();
}

void YtDlpResolver::onProcessFinished(int exitCode, const QByteArray &stdoutData,
                                      const QByteArray &stderrData, bool cancelled)
{
    ResolveResult r;
    if (cancelled) {
        r.ok = false;
        r.cancelled = true;
        r.error = tr("解析已取消");
        emit finished(r);
        return;
    }
    if (exitCode != 0) {
        r.ok = false;
        r.error = extractStderrError(stderrData);
        if (r.error.isEmpty())
            r.error = tr("解析失败（yt-dlp 退出码 %1）").arg(exitCode);
        emit finished(r);
        return;
    }

    const QByteArray json = extractFirstJsonObject(stdoutData);
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        r.ok = false;
        r.error = tr("解析工具输出无效");
        emit finished(r);
        return;
    }

    r = parseJson(doc.object());
    if (!r.ok && r.error.isEmpty())
        r.error = tr("未找到可播放的格式");
    emit finished(r);
}

ResolveResult YtDlpResolver::parseJson(const QJsonObject &obj)
{
    ResolveResult r;
    r.title = obj.value(u"title").toString();
    r.isLive = obj.value(u"is_live").toBool();

    // 兜底：顶层 url（yt-dlp 默认格式选择结果）
    const QString topUrl = obj.value(u"url").toString();
    const QVariantMap topHeaders = jsonToHeaders(obj.value(u"http_headers"));

    QVector<FormatEntry> singleFormats, videoOnly, audioOnly;
    const QJsonArray formats = obj.value(u"formats").toArray();
    for (const QJsonValue &fv : formats) {
        const QJsonObject f = fv.toObject();
        const QString url = f.value(u"url").toString();
        if (url.isEmpty())
            continue;
        FormatEntry e;
        e.url = url;
        e.headers = jsonToHeaders(f.value(u"http_headers"));
        e.vcodec = f.value(u"vcodec").toString();
        e.acodec = f.value(u"acodec").toString();
        e.protocol = f.value(u"protocol").toString();
        e.ext = f.value(u"ext").toString();
        e.width = f.value(u"width").toInt();
        e.height = f.value(u"height").toInt();
        e.tbr = f.value(u"tbr").toDouble();
        e.abr = f.value(u"abr").toDouble();
        e.fps = f.value(u"fps").toDouble();

        if (e.hasVideo() && e.hasAudio())
            singleFormats.append(e);
        else if (e.hasVideo())
            videoOnly.append(e);
        else if (e.hasAudio())
            audioOnly.append(e);
    }

    // 优先单 URL 音视频合一格式
    if (!singleFormats.isEmpty()) {
        std::sort(singleFormats.begin(), singleFormats.end(), isBetterVideo);
        r.directUrl = singleFormats.first().url;
        r.headers = singleFormats.first().headers;
        r.ok = true;
        return r;
    }

    // DASH 分离流：视频/音频各选最优
    if (!videoOnly.isEmpty() && !audioOnly.isEmpty()) {
        std::sort(videoOnly.begin(), videoOnly.end(), isBetterVideo);
        std::sort(audioOnly.begin(), audioOnly.end(), isBetterAudio);
        r.videoUrl = videoOnly.first().url;
        r.videoHeaders = videoOnly.first().headers;
        r.audioUrl = audioOnly.first().url;
        r.audioHeaders = audioOnly.first().headers;
        r.ok = true;
        return r;
    }

    // 兜底顶层 url（无 formats 数组的提取器，或恰好未解析出分类）
    if (!topUrl.isEmpty()) {
        r.directUrl = topUrl;
        r.headers = topHeaders;
        r.ok = true;
        return r;
    }

    r.ok = false;
    r.error = tr("未找到可播放的格式");
    return r;
}
