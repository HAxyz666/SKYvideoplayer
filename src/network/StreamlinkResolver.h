#pragma once

#include <QByteArray>
#include <QString>

#include "StreamResolver.h"

class QJsonObject;
class ToolRunner;

// streamlink 解析器：子进程运行 `streamlink --json <url>`，解析 JSON 选取最佳流。
// 用于直播模式（虎牙/斗鱼/B站直播等平台；streamlink 自带这些插件，yt-dlp 缺失）。
class StreamlinkResolver : public StreamResolver
{
    Q_OBJECT

public:
    explicit StreamlinkResolver(QObject *parent = nullptr);
    ~StreamlinkResolver() override;

    void resolve(const QString &url) override;
    void cancel() override;

private:
    void onProcessFinished(int exitCode, const QByteArray &stdoutData,
                           const QByteArray &stderrData, bool cancelled);
    static ResolveResult parseJson(const QJsonObject &obj);

    ToolRunner *m_runner{nullptr};
    QString m_url;
};
