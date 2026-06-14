#pragma once

#include <QAbstractListModel>
#include <QList>
#include "PlaylistItem.h"

// 播放列表数据模型，继承 QAbstractListModel 以支持 QML ListView 绑定
class PlaylistModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    // 自定义数据角色，供 QML 通过 model.xxx 访问
    enum Roles {
        TitleRole = Qt::UserRole + 1,   // 标题
        FilePathRole,                    // 文件路径
        DurationRole,                    // 时长
        IsPlayingRole                    // 是否为当前播放项
    };

    explicit PlaylistModel(QObject *parent = nullptr);

    // --- QAbstractListModel 接口 ---
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // --- 列表操作 ---
    void addFile(const QString &filePath);       // 添加文件（已存在则跳过）
    void removeItem(int index);                  // 移除指定项
    void clear();                                // 清空全部

    // --- 当前播放项 ---
    void setCurrentIndex(int index);             // 设置当前播放项，更新高亮
    int currentIndex() const;                    // 获取当前播放索引
    QString currentFilePath() const;             // 获取当前播放文件路径

    // --- 查询 ---
    int indexOf(const QString &filePath) const;  // 按路径查找索引
    PlaylistItem itemAt(int index) const;        // 获取指定索引的项

    // --- 属性 ---
    int count() const;

signals:
    void currentIndexChanged();  // 当前播放项变化
    void countChanged();         // 列表数量变化

private:
    static QString extractTitle(const QString &filePath);  // 从路径提取文件名作为标题

    QList<PlaylistItem> m_items;
    int m_currentIndex = -1;  // -1 表示无选中
};
