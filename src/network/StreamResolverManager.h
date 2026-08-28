#pragma once

#include <QObject>
#include <QString>

#include "ResolveResult.h"

class YtDlpResolver;
class StreamlinkResolver;

// 网络流模式 → 解析器分发：
//   直播 → streamlink（虎牙/斗鱼/B站直播等插件支持）
//   点播 → yt-dlp（站点覆盖广、格式选择）
// 两者均为 Python 工具，安卓移植时共享同一嵌入式 Python 运行时。
class StreamResolverManager : public QObject
{
    Q_OBJECT

public:
    enum Mode {
        Native = 0,   // 原生模式：不解析，控制器直接打开
        Live = 1,     // 直播模式
        Vod = 2,      // 点播模式
    };
    Q_ENUM(Mode)

    explicit StreamResolverManager(QObject *parent = nullptr);

    // 异步解析（Live/Vod）；Native 直接返回不处理。
    // 首次解析失败（瞬时网络/签名直链问题）会自动重试一次。
    void resolve(const QString &url, int mode);
    void cancel();

    // 指定模式对应的解析工具是否可用（Native 恒 true）
    static bool toolAvailable(int mode);

signals:
    // 解析完成（成功 result.ok=true 或失败 result.ok=false 均发出）。
    // mode 为用户请求时选择的模式（Native/Live/Vod），供上层持久化最近记录。
    void resolveFinished(const ResolveResult &result, const QString &sourceUrl, int mode);
    void resolvingChanged(bool resolving);

private:
    // 按当前模式启动解析器（重试复用）
    void startResolve();

    YtDlpResolver *m_ytdlp{nullptr};
    StreamlinkResolver *m_streamlink{nullptr};
    QString m_currentUrl;
    int m_currentMode{Native};
    int m_retryLeft{0};           // 剩余自动重试次数
    bool m_cancelRequested{false};
};
