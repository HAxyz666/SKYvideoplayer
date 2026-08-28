#include "StreamResolverManager.h"
#include "StreamlinkResolver.h"
#include "ToolProvider.h"
#include "YtDlpResolver.h"

StreamResolverManager::StreamResolverManager(QObject *parent)
    : QObject(parent)
    , m_ytdlp(new YtDlpResolver(this))
    , m_streamlink(new StreamlinkResolver(this))
{
    auto forward = [this](const ResolveResult &r) {
        // 自动重试：首次解析失败（瞬时网络/DNS 预热/签名直链过期）时重试一次，
        // 保持"解析中..."状态不闪烁；用户取消或工具硬错误则不重试。
        if (!r.ok && !r.cancelled && !m_cancelRequested && m_retryLeft > 0) {
            --m_retryLeft;
            startResolve();
            return;
        }
        if (m_cancelRequested) {
            // 用户已取消（退出播放/关闭对话框）：丢弃任何解析结果（含成功后到的），
            // 不再进入播放，避免隐藏的播放视图下仍在出声、标题残留。
            emit resolvingChanged(false);
            return;
        }
        emit resolvingChanged(false);
        emit resolveFinished(r, m_currentUrl, m_currentMode);
    };
    connect(m_ytdlp, &YtDlpResolver::finished, this, forward);
    connect(m_streamlink, &StreamlinkResolver::finished, this, forward);
}

void StreamResolverManager::resolve(const QString &url, int mode)
{
    if (mode == Native)
        return;   // 原生模式不解析，由控制器直接打开

    m_currentUrl = url;
    m_currentMode = mode;
    m_retryLeft = 1;
    m_cancelRequested = false;
    emit resolvingChanged(true);
    startResolve();
}

void StreamResolverManager::startResolve()
{
    if (m_currentMode == Live)
        m_streamlink->resolve(m_currentUrl);
    else
        m_ytdlp->resolve(m_currentUrl);
}

void StreamResolverManager::cancel()
{
    m_retryLeft = 0;
    m_cancelRequested = true;
    m_ytdlp->cancel();
    m_streamlink->cancel();
    emit resolvingChanged(false);
}

bool StreamResolverManager::toolAvailable(int mode)
{
    if (mode == Native)
        return true;
    if (mode == Live)
        return ToolProvider::streamlinkAvailable();
    return ToolProvider::ytDlpAvailable();
}
