#include "PlaylistModel.h"
#include <QFileInfo>

PlaylistModel::PlaylistModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

// 返回列表行数
int PlaylistModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

// 根据角色返回指定行的数据
QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};

    const auto &item = m_items.at(index.row());
    switch (role) {
    case TitleRole:      return item.title;
    case FilePathRole:   return item.filePath;
    case DurationRole:   return item.duration;
    case IsPlayingRole:  return index.row() == m_currentIndex;
    case Qt::DisplayRole: return item.title;
    default:             return {};
    }
}

// 注册自定义角色名，QML 中可写 model.title / model.isPlaying
QHash<int, QByteArray> PlaylistModel::roleNames() const
{
    return {
        { TitleRole,      "title" },
        { FilePathRole,   "filePath" },
        { DurationRole,   "duration" },
        { IsPlayingRole,  "isPlaying" }
    };
}

// 添加文件到列表末尾，已存在则跳过
void PlaylistModel::addFile(const QString &filePath)
{
    for (const auto &item : m_items) {
        if (item.filePath == filePath)
            return;
    }

    int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.append({ extractTitle(filePath), filePath, 0.0 });
    endInsertRows();
    emit countChanged();
}

// 移除指定索引的项
void PlaylistModel::removeItem(int index)
{
    if (index < 0 || index >= m_items.size())
        return;

    beginRemoveRows(QModelIndex(), index, index);
    m_items.removeAt(index);
    endRemoveRows();

    if (m_currentIndex == index)
        m_currentIndex = -1;
    else if (m_currentIndex > index)
        m_currentIndex--;

    emit countChanged();
}

// 清空整个播放列表
void PlaylistModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    m_currentIndex = -1;
    endResetModel();
    emit countChanged();
}

// 设置当前播放项，自动更新旧项和新项的高亮状态
void PlaylistModel::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    if (index == m_currentIndex)
        return;

    int old = m_currentIndex;
    m_currentIndex = index;
    emit currentIndexChanged();

    if (old >= 0 && old < m_items.size())
        emit dataChanged(createIndex(old, 0), createIndex(old, 0), { IsPlayingRole });
    emit dataChanged(createIndex(m_currentIndex, 0), createIndex(m_currentIndex, 0), { IsPlayingRole });
}

// 获取当前播放索引
int PlaylistModel::currentIndex() const
{
    return m_currentIndex;
}

// 获取当前播放项的文件路径
QString PlaylistModel::currentFilePath() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size())
        return {};
    return m_items.at(m_currentIndex).filePath;
}

// 按文件路径查找索引，不存在返回 -1
int PlaylistModel::indexOf(const QString &filePath) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).filePath == filePath)
            return i;
    }
    return -1;
}

// 获取指定索引的项
PlaylistItem PlaylistModel::itemAt(int index) const
{
    if (index < 0 || index >= m_items.size())
        return {};
    return m_items.at(index);
}

// 返回列表总数
int PlaylistModel::count() const
{
    return m_items.size();
}

// 是否有上一首
bool PlaylistModel::hasPrev() const
{
    if (m_items.isEmpty())
        return false;
    switch (m_playbackMode) {
    case PlaybackMode::Loop:
    case PlaybackMode::LoopOne:
        return true;
    case PlaybackMode::Sequential:
    default:
        return m_currentIndex > 0;
    }
}

// 是否有下一首
bool PlaylistModel::hasNext() const
{
    if (m_items.isEmpty())
        return false;
    switch (m_playbackMode) {
    case PlaybackMode::Loop:
    case PlaybackMode::LoopOne:
        return true;
    case PlaybackMode::Sequential:
    default:
        return m_currentIndex >= 0 && m_currentIndex < m_items.size() - 1;
    }
}

// 获取上一首文件路径（根据播放模式）
QString PlaylistModel::prevFilePath() const
{
    if (m_items.isEmpty())
        return {};

    switch (m_playbackMode) {
    case PlaybackMode::Loop:
        return m_items.at((m_currentIndex - 1 + m_items.size()) % m_items.size()).filePath;
    case PlaybackMode::LoopOne:
        return m_items.at(m_currentIndex).filePath;
    case PlaybackMode::Sequential:
    default:
        if (m_currentIndex > 0)
            return m_items.at(m_currentIndex - 1).filePath;
        return {};
    }
}

// 获取下一首文件路径（根据播放模式）
QString PlaylistModel::nextFilePath() const
{
    if (m_items.isEmpty())
        return {};

    switch (m_playbackMode) {
    case PlaybackMode::Loop:
        return m_items.at((m_currentIndex + 1) % m_items.size()).filePath;
    case PlaybackMode::LoopOne:
        return m_items.at(m_currentIndex).filePath;
    case PlaybackMode::Sequential:
    default:
        if (m_currentIndex >= 0 && m_currentIndex < m_items.size() - 1)
            return m_items.at(m_currentIndex + 1).filePath;
        return {};
    }
}

// 设置播放模式
void PlaylistModel::setPlaybackMode(PlaybackMode mode)
{
    if (m_playbackMode == mode)
        return;
    m_playbackMode = mode;
    emit playbackModeChanged();
    emit currentIndexChanged();  // hasPrev/hasNext 可能变化
}

// 获取当前播放模式
PlaybackMode PlaylistModel::playbackMode() const
{
    return m_playbackMode;
}

// 从文件路径中提取文件名作为标题
QString PlaylistModel::extractTitle(const QString &filePath)
{
    return QFileInfo(filePath).fileName();
}
