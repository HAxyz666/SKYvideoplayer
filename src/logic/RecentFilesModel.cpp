#include "RecentFilesModel.h"

#include <QFileInfo>
#include <QSettings>

RecentFilesModel::RecentFilesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    loadFromSettings();
}

int RecentFilesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant RecentFilesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};

    const auto &item = m_items.at(index.row());
    switch (role) {
    case FileNameRole:     return item.title;
    case FilePathRole:     return item.filePath;
    case LastPlayedRole:   return item.lastPlayed;
    case Qt::DisplayRole:  return item.title;
    default:               return {};
    }
}

QHash<int, QByteArray> RecentFilesModel::roleNames() const
{
    return {
        { FileNameRole,    "fileName" },
        { FilePathRole,    "filePath" },
        { LastPlayedRole,  "lastPlayed" }
    };
}

void RecentFilesModel::addFile(const QString &filePath)
{
    int existingIndex = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).filePath == filePath) {
            existingIndex = i;
            break;
        }
    }

    if (existingIndex >= 0) {
        if (existingIndex != 0) {
            beginMoveRows(QModelIndex(), existingIndex, existingIndex, QModelIndex(), 0);
            auto entry = m_items.takeAt(existingIndex);
            entry.lastPlayed = QDateTime::currentDateTime();
            m_items.prepend(entry);
            endMoveRows();
        } else {
            m_items[0].lastPlayed = QDateTime::currentDateTime();
        }
    } else {
        beginInsertRows(QModelIndex(), 0, 0);
        RecentFileEntry entry;
        entry.title = QFileInfo(filePath).fileName();
        entry.filePath = filePath;
        entry.lastPlayed = QDateTime::currentDateTime();
        m_items.prepend(entry);
        endInsertRows();

        if (m_items.size() > kMaxItems) {
            beginRemoveRows(QModelIndex(), kMaxItems, m_items.size() - 1);
            m_items.erase(m_items.begin() + kMaxItems, m_items.end());
            endRemoveRows();
        }
    }

    saveToSettings();
    emit countChanged();
}

void RecentFilesModel::removeFile(int index)
{
    if (index < 0 || index >= m_items.size())
        return;

    beginRemoveRows(QModelIndex(), index, index);
    m_items.removeAt(index);
    endRemoveRows();
    saveToSettings();
    emit countChanged();
}

void RecentFilesModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    saveToSettings();
    emit countChanged();
}

void RecentFilesModel::saveToSettings()
{
    QSettings settings;
    settings.beginGroup("RecentFiles");
    settings.setValue("count", m_items.size());
    for (int i = 0; i < m_items.size(); ++i) {
        QString prefix = QStringLiteral("item%1_").arg(i);
        settings.setValue(prefix + "title", m_items.at(i).title);
        settings.setValue(prefix + "path", m_items.at(i).filePath);
        settings.setValue(prefix + "time", m_items.at(i).lastPlayed);
    }
    settings.endGroup();
}

void RecentFilesModel::loadFromSettings()
{
    QSettings settings;
    settings.beginGroup("RecentFiles");
    int count = settings.value("count", 0).toInt();
    if (count <= 0) {
        settings.endGroup();
        return;
    }

    QList<RecentFileEntry> items;
    for (int i = 0; i < count; ++i) {
        QString prefix = QStringLiteral("item%1_").arg(i);
        QString path = settings.value(prefix + "path").toString();
        if (path.isEmpty())
            continue;
        RecentFileEntry entry;
        entry.title = settings.value(prefix + "title").toString();
        entry.filePath = path;
        entry.lastPlayed = settings.value(prefix + "time").toDateTime();
        items.append(entry);
    }
    settings.endGroup();

    if (!items.isEmpty()) {
        beginResetModel();
        m_items = items;
        endResetModel();
        emit countChanged();
    }
}
