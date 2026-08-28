#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QByteArray>
#include <QStringList>

// 外部解析工具（yt-dlp / streamlink）的进程调用隔离点。
//
// 这是整个网络流解析层中唯一的"进程调用"位置：
//  - 桌面：QProcess 调系统/配置路径下的工具；
//  - 未来安卓移植：替换本层实现（嵌入式 Python / JNI / 禁用回退原生），
//    上层 Resolver 与 UI 零改动。
// 工具可用性探测也集中在此，不硬编码路径。

namespace ToolProvider {

// 探测 yt-dlp 是否可用（PATH 查找，缓存结果）
bool ytDlpAvailable();
QString ytDlpPath();

// 探测 streamlink 是否可用（PATH 查找，缓存结果）
bool streamlinkAvailable();
QString streamlinkPath();

} // namespace ToolProvider

// 单次工具进程运行（异步、可取消、带超时）。
class ToolRunner : public QObject
{
    Q_OBJECT

public:
    explicit ToolRunner(QObject *parent = nullptr);

    // 启动工具进程。完成（正常退出/超时/取消）后发出 finished。
    // timeoutMs 超时后强制结束进程并以 exitCode=-1 汇报。
    void start(const QString &program, const QStringList &args, int timeoutMs = 30000);
    void cancel();

signals:
    void finished(int exitCode, const QByteArray &stdoutData,
                  const QByteArray &stderrData, bool cancelled);

private:
    void handleFinished(int exitCode, QProcess::ExitStatus status);

    QProcess *m_proc{nullptr};
    QTimer *m_timeoutTimer{nullptr};
    bool m_cancelled{false};
};
