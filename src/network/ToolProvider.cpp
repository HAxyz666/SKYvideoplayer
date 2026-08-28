#include "ToolProvider.h"

#include <QFileInfo>
#include <QStandardPaths>

namespace ToolProvider {

static QString findInPath(const QString &name)
{
    const QStringList paths = QString::fromUtf8(qgetenv("PATH"))
        .split(u':', Qt::SkipEmptyParts);
    for (const QString &dir : paths) {
        const QString candidate = dir + u'/' + name;
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}

bool ytDlpAvailable()
{
    return !ytDlpPath().isEmpty();
}

QString ytDlpPath()
{
    static const QString path = findInPath(QStringLiteral("yt-dlp"));
    return path;
}

bool streamlinkAvailable()
{
    return !streamlinkPath().isEmpty();
}

QString streamlinkPath()
{
    static const QString path = findInPath(QStringLiteral("streamlink"));
    return path;
}

} // namespace ToolProvider

ToolRunner::ToolRunner(QObject *parent)
    : QObject(parent)
    , m_proc(new QProcess(this))
    , m_timeoutTimer(new QTimer(this))
{
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_proc && m_proc->state() != QProcess::NotRunning) {
            qWarning() << "ToolRunner: timeout, killing process";
            m_proc->kill();
        }
    });
}

void ToolRunner::start(const QString &program, const QStringList &args, int timeoutMs)
{
    // 终止并丢弃在途进程：其 finished 不转发（避免旧结果污染新解析）
    if (m_proc->state() != QProcess::NotRunning) {
        disconnect(m_proc, &QProcess::finished, this, &ToolRunner::handleFinished);
        m_proc->kill();
        m_proc->waitForFinished(1000);
    }

    m_cancelled = false;
    m_proc->setProgram(program);
    m_proc->setArguments(args);
    connect(m_proc, &QProcess::finished, this, &ToolRunner::handleFinished,
            Qt::UniqueConnection);

    m_timeoutTimer->start(timeoutMs);
    m_proc->start();
}

void ToolRunner::cancel()
{
    if (m_proc->state() != QProcess::NotRunning) {
        m_cancelled = true;
        m_proc->kill();
    }
}

void ToolRunner::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    m_timeoutTimer->stop();

    // finished 可能因进程被 kill 触发两次（cancel 后）——用 UniqueConnection
    // + 断开，避免重复汇报。
    disconnect(m_proc, &QProcess::finished, this, &ToolRunner::handleFinished);

    const bool cancelled = m_cancelled || status == QProcess::CrashExit;
    const QByteArray stdoutData = m_proc->readAllStandardOutput();
    const QByteArray stderrData = m_proc->readAllStandardError();
    emit finished(exitCode, stdoutData, stderrData, cancelled);
}
