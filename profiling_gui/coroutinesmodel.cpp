// Copyright (c) 2013 Maciej Gajewski

#include "profiling_gui/coroutinesmodel.hpp"

#include <QPixmap>
#include <QPainter>
#include <QDebug>

namespace profiling_gui {

CoroutinesModel::CoroutinesModel(QObject *parent) :
    QAbstractListModel(parent),

    _selectionModel(this)
{
    connect(&_selectionModel, SIGNAL(currentRowChanged(QModelIndex,QModelIndex)), SLOT(onSelectionChanged(QModelIndex)));
}

void CoroutinesModel::append(const CoroutinesModel::Record& record)
{
    beginInsertRows(QModelIndex(), _records.size(), _records.size());

    _records.append(record);

    endInsertRows();
}

void CoroutinesModel::clear()
{
    beginResetModel();
    _records.clear();
    endResetModel();
}

QVariant CoroutinesModel::data(const QModelIndex& index, int role) const
{
    // no nested data
    if (index.parent().isValid())
    {
        return QVariant();
    }

    const Record& record = _records[index.row()];

    if (index.column() == 0)
    {
        if (role == Qt::DisplayRole)
            return record.name;
        else if (role == Qt::DecorationRole)
            return iconFromColor(record.color);
    }
    if (index.column() == 1 && role == Qt::DisplayRole)
    {
        return nanosToString(record.timeExecuted);
    }

    return QVariant();
}

void CoroutinesModel::sort(int column, Qt::SortOrder order)
{
    emit layoutAboutToBeChanged(QList<QPersistentModelIndex>(), VerticalSortHint);

    if (column == 0)
    {
        qStableSort(_records.begin(), _records.end(), [order](const Record& a, const Record& b)
        {
            if (order == Qt::AscendingOrder)
                return a.name > b.name;
            else
                return a.name < b.name;
        });
    }
    else if (column == 1)
    {
        qStableSort(_records.begin(), _records.end(), [order](const Record& a, const Record& b)
        {
            if (order == Qt::AscendingOrder)
                return a.timeExecuted > b.timeExecuted;
            else
                return a.timeExecuted < b.timeExecuted;
        });
    }

    emit layoutChanged(QList<QPersistentModelIndex>(), VerticalSortHint);
}

QVariant CoroutinesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch(section)
        {
            case 0:
                return "Name";
            case 1:
                return "Time";
            default:
                ;
        }
    }

    return QVariant();
}

void CoroutinesModel::onCoroutineSelected(quintptr id)
{
    auto it = std::find_if(_records.begin(), _records.end(), [id](const Record& r) { return r.id == id; });
    int row = std::distance(_records.begin(), it);

    blockSignals(true);
    _selectionModel.setCurrentIndex(index(row), QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    blockSignals(false);
}

void CoroutinesModel::onSelectionChanged(QModelIndex index)
{
    emit coroSelected(_records[index.row()].id);
}

QPixmap CoroutinesModel::iconFromColor(QColor color)
{
    static const int ICON_SIZE = 16;
    QPixmap icon(ICON_SIZE, ICON_SIZE);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);

    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(color);
    painter.drawRect(1, 1, ICON_SIZE-2, ICON_SIZE-2);

    return icon;
}

}
