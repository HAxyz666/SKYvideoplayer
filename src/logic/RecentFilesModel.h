#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>

struct RecentFileEntry {
    QString title;
    QString filePath;
    QDateTime lastPlayed;
};

class RecentFilesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        FileNameRole = Qt::UserRole + 1,
        FilePathRole,
        LastPlayedRole
    };

    explicit RecentFilesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addFile(const QString &filePath);
    Q_INVOKABLE void removeFile(int index);
    Q_INVOKABLE void clear();

    void saveToSettings();
    void loadFromSettings();

signals:
    void countChanged();

private:
    static constexpr int kMaxItems = 10;
    QList<RecentFileEntry> m_items;
};
