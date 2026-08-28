#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>

struct RecentFileEntry {
    QString title;
    QString filePath;
    QDateTime lastPlayed;
    // 播放模式：0=原生 1=直播 2=点播（StreamResolverManager::Mode）。
    // 网络流解析条目点击时按此模式重新解析（拿新鲜直链与真实标题）。
    int mode = 0;
};

class RecentFilesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        FileNameRole = Qt::UserRole + 1,
        FilePathRole,
        LastPlayedRole,
        ModeRole
    };

    explicit RecentFilesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // title 为空时从 filePath 派生文件名（网络流解析模式传入真实标题）；
    // mode 记录条目播放模式，网络流解析条目点击时据此重新解析。
    Q_INVOKABLE void addFile(const QString &filePath, const QString &title = {}, int mode = 0);
    Q_INVOKABLE void removeFile(int index);

    void saveToSettings();
    void loadFromSettings();

signals:
    void countChanged();

private:
    static constexpr int kMaxItems = 50;
    QList<RecentFileEntry> m_items;
};
