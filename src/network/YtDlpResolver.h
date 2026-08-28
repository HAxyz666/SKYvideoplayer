#pragma once

#include <QByteArray>
#include <QString>

#include "StreamResolver.h"

class QJsonObject;
class ToolRunner;

// yt-dlp 解析器：子进程运行 `yt-dlp -j <url>`，解析 JSON 选取可播格式。
// 选格式策略（v1）：
//  - 优先单 URL 音视频合一格式（m3u8 合并流/渐进式），保证引擎单输入可播；
//  - 无合并格式时走 DASH 分离流（videoUrl + audioUrl），交给引擎 split 模式。
class YtDlpResolver : public StreamResolver
{
    Q_OBJECT

public:
    explicit YtDlpResolver(QObject *parent = nullptr);
    ~YtDlpResolver() override;

    void resolve(const QString &url) override;
    void cancel() override;

private:
    void onProcessFinished(int exitCode, const QByteArray &stdoutData,
                           const QByteArray &stderrData, bool cancelled);
    static ResolveResult parseJson(const QJsonObject &obj);

    ToolRunner *m_runner{nullptr};
    QString m_url;
};
