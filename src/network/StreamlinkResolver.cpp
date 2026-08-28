#include "StreamlinkResolver.h"
#include "ToolProvider.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

// 从 JSON 值提取 http_headers（streamlink 流条目的 headers 字段）
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

// streamlink --json 出错时向 stdout 输出 {"error": "..."}，取其中的错误文案
static QString extractStdoutJsonError(const QByteArray &stdoutData)
{
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(stdoutData, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object().value(u"error").toString();
}

// 从 stderr 提取最后一条有效错误（剥掉 "error:" / "streamlink: error:" 前缀）
static QString extractStderrError(const QByteArray &stderrData)
{
    const QStringList lines = QString::fromUtf8(stderrData)
        .split(u'\n', Qt::SkipEmptyParts);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        QString line = it->trimmed();
        if (line.isEmpty())
            continue;
        // 兼容 argparse 的 "streamlink: error: ..." 与普通 "error: ..." 两种前缀
        if (line.startsWith(u"error:"))
            line = line.mid(6).trimmed();
        else {
            const int idx = line.indexOf(u"error:");
            if (idx >= 0)
                line = line.mid(idx + 6).trimmed();
        }
        if (!line.isEmpty())
            return line;
    }
    return QString();
}

// 从流质量名解析分辨率（"1080p60" -> 1080；"best"/"worst" -> 0）
static int qualityHeight(const QString &quality)
{
    int i = 0;
    while (i < quality.size() && quality.at(i).isDigit())
        ++i;
    return i > 0 ? quality.left(i).toInt() : 0;
}

StreamlinkResolver::StreamlinkResolver(QObject *parent)
    : StreamResolver(parent)
    , m_runner(new ToolRunner(this))
{
    connect(m_runner, &ToolRunner::finished,
            this, &StreamlinkResolver::onProcessFinished);
}

StreamlinkResolver::~StreamlinkResolver() = default;

void StreamlinkResolver::resolve(const QString &url)
{
    m_url = url;

    if (!ToolProvider::streamlinkAvailable()) {
        ResolveResult r;
        r.ok = false;
        r.error = tr("未检测到 streamlink，请先安装：pip install streamlink");
        emit finished(r);
        return;
    }

    // 不带流参数：--json 仅列出可用流（含 best/worst 别名），不会实际拉流。
    // 不传 --no-version-check：部分旧版本 streamlink 不识别该参数会直接报错。
    m_runner->start(ToolProvider::streamlinkPath(),
                    { QStringLiteral("--json"), url },
                    30000);
}

void StreamlinkResolver::cancel()
{
    m_runner->cancel();
}

void StreamlinkResolver::onProcessFinished(int exitCode, const QByteArray &stdoutData,
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
        // streamlink 出错时 --json 把错误写进 stdout（{"error": "..."}），优先取用；
        // 其余情况（argparse 等）错误在 stderr。
        r.error = extractStdoutJsonError(stdoutData);
        if (r.error.isEmpty())
            r.error = extractStderrError(stderrData);
        if (r.error.isEmpty())
            r.error = tr("解析失败（streamlink 退出码 %1）").arg(exitCode);
        emit finished(r);
        return;
    }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(stdoutData, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        r.ok = false;
        r.error = tr("解析工具输出无效");
        emit finished(r);
        return;
    }

    r = parseJson(doc.object());
    if (!r.ok && r.error.isEmpty())
        r.error = tr("未找到可播放的直播流");
    emit finished(r);
}

ResolveResult StreamlinkResolver::parseJson(const QJsonObject &obj)
{
    ResolveResult r;
    r.isLive = true;

    // 部分插件提供标题/作者元数据
    const QJsonObject meta = obj.value(u"metadata").toObject();
    r.title = meta.value(u"title").toString();

    // 优先 best 别名；缺失时取分辨率最高的条目
    const QJsonObject streams = obj.value(u"streams").toObject();
    QJsonValue chosen = streams.value(u"best");
    if (!chosen.isObject()) {
        int bestHeight = 0;
        for (auto it = streams.begin(); it != streams.end(); ++it) {
            const QJsonObject s = it.value().toObject();
            const int h = qualityHeight(it.key());
            if (h > bestHeight) {
                bestHeight = h;
                chosen = it.value();
            }
        }
    }

    if (!chosen.isObject()) {
        r.ok = false;
        r.error = tr("未找到可播放的直播流");
        return r;
    }

    const QJsonObject s = chosen.toObject();
    r.directUrl = s.value(u"url").toString();
    r.headers = jsonToHeaders(s.value(u"headers"));
    if (r.directUrl.isEmpty()) {
        r.ok = false;
        r.error = tr("直播流地址为空");
        return r;
    }
    r.ok = true;
    return r;
}
