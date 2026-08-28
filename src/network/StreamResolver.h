#pragma once

#include <QObject>
#include <QString>

#include "ResolveResult.h"

// 网络流解析器抽象接口（平台无关）。
// UI/控制器只依赖本接口与 ResolveResult，不感知具体工具与调用方式。
class StreamResolver : public QObject
{
    Q_OBJECT

public:
    explicit StreamResolver(QObject *parent = nullptr) : QObject(parent) {}
    ~StreamResolver() override = default;

    // 异步解析：完成后发出 finished（成功时 result.ok=true）
    virtual void resolve(const QString &url) = 0;
    virtual void cancel() = 0;

signals:
    void finished(const ResolveResult &result);
};
