#include <QAction>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QInputDialog>
#include <QFileDialog>
#include <QMenu>
#include <QThread>
#include "playlistwindow.h"
#include "ui_playlistwindow.h"
#include "qdrawnplaylist.h"
#include "playlist.h"
#include "platform/unify.h"

PlaylistWindow::PlaylistWindow(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::PlaylistWindow),
    currentPlaylist()
{
    clipboard = new PlaylistSelection;

    ui->setupUi(this);
    setObjectName("playlistWindow");
    setWindowTitle(tr("Playlist"));
    addNewTab(QUuid(), tr("Quick Playlist"));
    addQuickQueue();
    ui->searchHost->setVisible(false);
    ui->searchField->installEventFilter(this);

    setupIconThemer();
    connectSignalsToSlots();
    Platform::disableAutomaticAccel(this);
}

PlaylistWindow::~PlaylistWindow()
{
    delete ui;
    delete clipboard;
}

void PlaylistWindow::setCurrentPlaylist(QUuid what)
{
    if (widgets.contains(what)) {
        ui->tabWidget->setCurrentWidget(widgets[what]);
        currentPlaylist = what;
    }
    updateCurrentPlaylist();
}

void PlaylistWindow::clearPlaylist(QUuid what)
{
    if (widgets.contains(what))
        widgets[what]->removeAll();
    updatePlaylistHasItems();
}

QPair<QUuid, QUuid> PlaylistWindow::addToCurrentPlaylist(QList<QUrl> what)
{
    QList<QUrl> filtered = Helpers::filterUrls(what);
    QPair<QUuid, QUuid> info;
    auto qdp = currentPlaylistWidget();
    for (QUrl &url : filtered) {
        QPair<QUuid,QUuid> itemInfo = qdp->importUrl(url);
        if (info.second.isNull())
            info = itemInfo;
    }
    updatePlaylistHasItems();
    return info;
}

QPair<QUuid, QUuid> PlaylistWindow::urlToQuickPlaylist(QUrl what)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(QUuid());
    pl->clear();
    widgets[QUuid()]->clear();
    ui->tabWidget->setCurrentWidget(widgets[QUuid()]);
    return addToCurrentPlaylist(QList<QUrl>() << what);
}

bool PlaylistWindow::isCurrentPlaylistEmpty()
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(currentPlaylist);
    return pl ? pl->isEmpty() : true;
}

bool PlaylistWindow::isPlaylistSingularFile(QUuid list)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl || pl->count() != 1)
        return false;
    auto item = pl->itemFirst();
    return item->url().isLocalFile();
}

QPair<QUuid,QUuid> PlaylistWindow::getItemAfter(QUuid list, QUuid item)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl)
        return { QUuid(), QUuid() };
    auto qpl = PlaylistCollection::getSingleton()->queuePlaylist();
    QPair<QUuid, QUuid> next = qpl->takeFirst();
    if (!next.second.isNull())
        return next;
    QSharedPointer<Item> after = pl->itemAfter(item);
    if (!after)
        return { QUuid(), QUuid() };
    return { pl->uuid(), after->uuid() };
}

QUuid PlaylistWindow::getItemBefore(QUuid list, QUuid item)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl)
        return QUuid();
    QSharedPointer<Item> before = pl->itemBefore(item);
    if (!before)
        return QUuid();
    return before->uuid();
}

QUrl PlaylistWindow::getUrlOf(QUuid list, QUuid item)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl)
        return QUrl();
    auto i = pl->itemOf(item);
    if (!i)
        return QUrl();
    return i->url();
}

QUrl PlaylistWindow::getUrlOfFirst(QUuid list)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    auto item = pl->itemFirst();
    if (item.isNull())
        return QUrl();
    return item->url();
}

void PlaylistWindow::setMetadata(QUuid list, QUuid item, const QVariantMap &map)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl)
        return;
    auto i = pl->itemOf(item);
    if (!i)
        return;
    i->setMetadata(map);

    auto qdp = currentPlaylistWidget();
    if (qdp->uuid() == list)
        qdp->viewport()->update();

}

void PlaylistWindow::replaceItem(QUuid list, QUuid item, const QList<QUrl> &urls)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl)
        return;

    QList<QUrl> filtered = Helpers::filterUrls(urls);
    if (filtered.isEmpty()) {
        // FIXME: remove the item that cannot played
        return;
    }

    QList<QUuid> addedItems = pl->replaceItem(item, filtered);
    auto listWidget = widgets[list];
    if (listWidget)
        listWidget->addItemsAfter(item, addedItems);

    auto qdp = currentPlaylistWidget();
    if (qdp->uuid() == list)
        qdp->viewport()->update();

    updatePlaylistHasItems();
}

int PlaylistWindow::extraPlayTimes(QUuid list, QUuid item)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl)
        return -1;
    auto i = pl->itemOf(item);
    return i ? i->extraPlayTimes() : -1;
}

void PlaylistWindow::setExtraPlayTimes(QUuid list, QUuid item, int amount)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl)
        return;
    auto i = pl->itemOf(item);
    if (!i)
        return;
    i->setExtraPlayTimes(amount);
}

QVariantList PlaylistWindow::tabsToVList() const
{
    QVariantList qvl;
    for (int i = 0; i < ui->tabWidget->count(); i++) {
        auto widget = reinterpret_cast<QDrawnPlaylist *>(ui->tabWidget->widget(i));
        qvl.append(widget->toVMap());
    }
    return qvl;
}

void PlaylistWindow::tabsFromVList(const QVariantList &qvl)
{
    ui->tabWidget->clear();
    widgets.clear();
    for (const QVariant &v : qvl) {
        auto qdp = new QDrawnPlaylist();
        qdp->setDisplayParser(&displayParser);
        qdp->fromVMap(v.toMap());
        connect(qdp, &QDrawnPlaylist::itemDesired,
                this, &PlaylistWindow::itemDesired);
        connect(qdp, &QDrawnPlaylist::removeItemRequested,
                this, &PlaylistWindow::playlist_removeItemRequested);
        connect(qdp, &QDrawnPlaylist::removeAllRequested,
                this, &PlaylistWindow::playlist_removeAllRequested);
        auto pl = PlaylistCollection::getSingleton()->playlistOf(qdp->uuid());
        ui->tabWidget->addTab(qdp, pl->title());
        widgets.insert(pl->uuid(), qdp);
    }
    if (widgets.count() < 1)
        addNewTab(QUuid(), tr("Quick Playlist"));
    updatePlaylistHasItems();
}

bool PlaylistWindow::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj);
    if (obj == ui->searchField && event->type() == QEvent::KeyPress) {
        auto keyEvent = reinterpret_cast<QKeyEvent*>(event);
        if (!keyEvent->modifiers() &&
                (keyEvent->key() == Qt::Key_Up ||
                 keyEvent->key() == Qt::Key_Down)) {
            if (keyEvent->key() == Qt::Key_Up)
                selectPrevious();
            else
                selectNext();
            return true;
        }
    }
    return QDockWidget::eventFilter(obj, event);
}

void PlaylistWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->accept();
}

void PlaylistWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    addToCurrentPlaylist(event->mimeData()->urls());
}

void PlaylistWindow::wheelEvent(QWheelEvent *event)
{
    // Don't pass scroll events up the chain.  They are used for e.g. tab
    // switching when over the tab bar and also scrolling the playlists.
    event->accept();
}

void PlaylistWindow::setupIconThemer()
{
    QList<IconThemer::IconData> data {
        { ui->newTab, "tab-new" },
        { ui->closeTab, "tab-close" },
        { ui->duplicateTab, "tab-duplicate" },
        { ui->importList, "document-import" },
        { ui->exportList, "document-export" },
        { ui->visibleToQueue, "media-queue-visible" },
        { ui->showQueue, "view-media-queue" }
    };
    for (auto &d : data)
        themer.addIconData(d);
}

void PlaylistWindow::connectSignalsToSlots()
{
    connect(this, &PlaylistWindow::visibilityChanged,
            this, &PlaylistWindow::self_visibilityChanged);
    connect(this, &PlaylistWindow::dockLocationChanged,
            this, &PlaylistWindow::self_dockLocationChanged);
    connect(this->toggleViewAction(), &QAction::toggled,
            this, &PlaylistWindow::viewActionChanged);

    connect(ui->newTab, &QPushButton::clicked,
            this, &PlaylistWindow::newTab);
    connect(ui->closeTab, &QPushButton::clicked,
            this, &PlaylistWindow::closeTab);
    connect(ui->duplicateTab, &QPushButton::clicked,
            this, &PlaylistWindow::duplicateTab);
    connect(ui->importList, &QPushButton::clicked,
            this, &PlaylistWindow::importTab);
    connect(ui->exportList, &QPushButton::clicked,
            this, &PlaylistWindow::exportTab);
    connect(ui->visibleToQueue, &QPushButton::clicked,
            this, &PlaylistWindow::visibleToQueue);
    connect(ui->showQueue, &QPushButton::clicked,
            this, &PlaylistWindow::setQueueMode);
}

QDrawnPlaylist *PlaylistWindow::currentPlaylistWidget()
{
    return reinterpret_cast<QDrawnPlaylist *>(ui->tabWidget->currentWidget());
}

void PlaylistWindow::updateCurrentPlaylist()
{
    auto qdp = currentPlaylistWidget();
    if (!qdp)
        return;
    currentPlaylist = qdp->uuid();
    setTabOrder(ui->tabWidget->focusProxy(), qdp);
    setTabOrder(qdp, ui->searchField);
    updatePlaylistHasItems();
}

void PlaylistWindow::updatePlaylistHasItems()
{
    auto qdp = currentPlaylistWidget();
    if (!qdp)
        return;
    emit currentPlaylistHasItems(qdp->count() > 0);
}

void PlaylistWindow::setPlaylistFilters(QString filterText)
{
    for (auto &widget : widgets) {
        widget->setFilter(filterText);
    }
    queueWidget->setFilter(filterText);
}

void PlaylistWindow::addNewTab(QUuid playlist, QString title)
{
    auto qdp = new QDrawnPlaylist();
    qdp->setDisplayParser(&displayParser);
    qdp->setUuid(playlist);
    connect(qdp, &QDrawnPlaylist::itemDesired, this, &PlaylistWindow::itemDesired);
    connect(qdp, &QDrawnPlaylist::removeItemRequested,
            this, &PlaylistWindow::playlist_removeItemRequested);
    connect(qdp, &QDrawnPlaylist::removeAllRequested,
            this, &PlaylistWindow::playlist_removeAllRequested);
    widgets.insert(playlist, qdp);
    ui->tabWidget->addTab(qdp, title);
    ui->tabWidget->setCurrentWidget(qdp);
}

void PlaylistWindow::addQuickQueue()
{
    queueWidget = new QDrawnQueue();
    queueWidget->setDisplayParser(&displayParser);
    queueWidget->setUuid(QUuid());
    connect(queueWidget, &QDrawnQueue::itemDesired,
            this, &PlaylistWindow::itemDesired);
    ui->quickPage->layout()->addWidget(queueWidget);
}

void PlaylistWindow::setIconTheme(const QString &fallback,
                                  const QString &custom)
{
    themer.setIconFolders(fallback, custom);
}

bool PlaylistWindow::activateItem(QUuid playlistUuid, QUuid itemUuid)
{
    if (!widgets.contains(playlistUuid))
        return false;
    auto qdp = widgets[playlistUuid];
    qdp->scrollToItem(itemUuid);
    qdp->setNowPlayingItem(itemUuid);
    return true;
}

void PlaylistWindow::changePlaylistSelection( QUrl itemUrl, QUuid playlistUuid, QUuid itemUuid)
{
    (void)itemUrl;
    if (!activateItem(playlistUuid, itemUuid))
        return;
    auto pl = PlaylistCollection::getSingleton()->playlistOf(playlistUuid);
    auto qpl = PlaylistCollection::getSingleton()->queuePlaylist();
    if (!itemUuid.isNull() && qpl->first().second == itemUuid) {
        queueWidget->removeItem(itemUuid);
    }
}

void PlaylistWindow::addSimplePlaylist(QStringList data)
{

    auto pl = PlaylistCollection::getSingleton()->newPlaylist(tr("New Playlist"));
    pl->fromStringList(data);
    addNewTab(pl->uuid(), pl->title());
}

void PlaylistWindow::setDisplayFormatSpecifier(QString fmt)
{
    displayParser.takeFormatString(fmt);
    ui->tabWidget->currentWidget()->update();
}

void PlaylistWindow::newTab()
{
    bool ok;
    QString title = QInputDialog::getText(this, tr("Enter Playlist Name"),
                                           "Name", QLineEdit::Normal,
                                          tr("New Playlist"), &ok);
    if (!ok)
        return;
    else if (title.isEmpty())
        title = tr("New Playlist");

    auto pl = PlaylistCollection::getSingleton()->newPlaylist(title.replace("&","+"));
    addNewTab(pl->uuid(), pl->title());
}

void PlaylistWindow::closeTab()
{
    int index = ui->tabWidget->currentIndex();
    on_tabWidget_tabCloseRequested(index);
    updateCurrentPlaylist();
}

void PlaylistWindow::duplicateTab()
{
    auto origin = currentPlaylistWidget();
    auto remote = PlaylistCollection::getSingleton()->clonePlaylist(origin->uuid());
    addNewTab(remote->uuid(), remote->title());
}

void PlaylistWindow::importTab()
{
    QString file;
    file = QFileDialog::getOpenFileName(this, tr("Import File"), QString(),
                                        tr("Playlist files (*.m3u *.m3u8)"));
    if (!file.isEmpty())
        emit importPlaylist(file);
}

void PlaylistWindow::exportTab()
{
    auto uuid = currentPlaylistWidget()->uuid();

    QString file;
    file = QFileDialog::getSaveFileName(this, tr("Export File"), QString(),
                                        tr("Playlist files (*.m3u *.m3u8)"));
    auto pl = PlaylistCollection::getSingleton()->playlistOf(uuid);
    if (!file.isEmpty() && pl)
        emit exportPlaylist(file, pl->toStringList());
}

void PlaylistWindow::copy()
{
    clipboard->fromSelected(currentPlaylistWidget());
}

void PlaylistWindow::copyQueue()
{
    clipboard->fromQueue(currentPlaylistWidget());
}

void PlaylistWindow::paste()
{
    clipboard->appendToPlaylist(currentPlaylistWidget());
}

void PlaylistWindow::pasteQueue()
{
    clipboard->appendAndQuickQueue(currentPlaylistWidget());
}

void PlaylistWindow::playCurrentItem()
{
    auto qdp = currentPlaylistWidget();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(qdp->uuid());
    auto itemUuid = qdp->currentItemUuid();
    if (itemUuid.isNull())
        return;
    emit itemDesired(pl->uuid(), itemUuid);
}

bool PlaylistWindow::playActiveItem()
{
    auto qdp = currentPlaylistWidget();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(qdp->uuid());
    auto itemUuid = qdp->nowPlayingItem();
    if (itemUuid.isNull())
        return false;
    emit itemDesired(pl->uuid(), itemUuid);
    return true;
}

void PlaylistWindow::selectNext()
{
    auto qdp = currentPlaylistWidget();
    int index = qdp->currentRow();
    if (index < qdp->count())
        qdp->setCurrentRow(index + 1);
}

void PlaylistWindow::selectPrevious()
{
    auto qdp = currentPlaylistWidget();
    int index = qdp->currentRow();
    if (index > 0)
        qdp->setCurrentRow(index - 1);
}

void PlaylistWindow::incExtraPlayTimes()
{
    auto qdp = currentPlaylistWidget();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(qdp->uuid());
    auto incrementer = [pl](QUuid uuid) {
        auto item = pl->itemOf(uuid);
        if (Q_LIKELY(!item.isNull()))
            item->incExtraPlayTimes();
    };
    qdp->traverseSelected(incrementer);
    qdp->viewport()->update();
}

void PlaylistWindow::decExtraPlayTimes()
{
    auto qdp = currentPlaylistWidget();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(qdp->uuid());
    auto decrementer = [pl](QUuid uuid) {
        auto item = pl->itemOf(uuid);
        if (Q_LIKELY(!item.isNull()))
            item->decExtraPlayTimes();
    };
    qdp->traverseSelected(decrementer);
    qdp->viewport()->update();
}

void PlaylistWindow::zeroExtraPlayTimes()
{
    auto qdp = currentPlaylistWidget();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(qdp->uuid());
    auto zeroer = [pl](QUuid uuid) {
        auto item = pl->itemOf(uuid);
        if (Q_LIKELY(!item.isNull()))
            item->setExtraPlayTimes(0);
    };
    qdp->traverseSelected(zeroer);
    qdp->viewport()->update();
}

void PlaylistWindow::activateNext()
{
    auto qdp = currentPlaylistWidget();
    auto now = qdp->nowPlayingItem();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(qdp->uuid());
    auto next = pl->itemAfter(now);
    if (!!next)
        activateItem(qdp->uuid(), next->uuid());
}

void PlaylistWindow::activatePrevious()
{
    auto qdp = currentPlaylistWidget();
    auto now = qdp->nowPlayingItem();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(qdp->uuid());
    auto prev = pl->itemBefore(now);
    if (!!prev)
        activateItem(qdp->uuid(), prev->uuid());
}

void PlaylistWindow::quickQueue()
{
    if (ui->showQueue->isChecked())
        return;

    auto qdp = currentPlaylistWidget();
    auto itemUuids = qdp->currentItemUuids();
    if (itemUuids.isEmpty())
        return;
    auto qpl = PlaylistCollection::getSingleton()->queuePlaylist();
    QList<QUuid> added;
    QList<int>removed;
    qpl->toggle(qdp->uuid(), itemUuids, added, removed);
    queueWidget->removeItems(removed);
    queueWidget->addItems(added);
    qdp->viewport()->update();
    queueWidget->viewport()->update();
}

void PlaylistWindow::visibleToQueue()
{
    if (ui->showQueue->isChecked())
        return;

    QList<QUuid> added;
    QList<int> removed;
    PlaylistCollection::getSingleton()->queuePlaylist()->\
            toggleFromPlaylist(currentPlaylistWidget()->uuid(), added, removed);
    queueWidget->removeItems(removed);
    queueWidget->addItems(added);
    queueWidget->viewport()->update();
    currentPlaylistWidget()->viewport()->update();
}

void PlaylistWindow::setQueueMode(bool yes)
{
    ui->playStack->setCurrentIndex(yes ? 1 : 0);
    setWindowTitle(yes ? "Queue" : "Playlist");
    ui->showQueue->setChecked(yes);
    emit quickQueueMode(yes);
}

void PlaylistWindow::revealSearch()
{
    showSearch = true;
    activateWindow();
    ui->searchHost->setVisible(true);
    ui->searchField->setFocus();
}

void PlaylistWindow::finishSearch()
{
    showSearch = false;
    if (!ui->searchHost->isVisible())
        return;

    if (!ui->searchField->text().isEmpty()) {
        ui->searchField->setText(QString());
        setPlaylistFilters(QString());
    }

    if (ui->searchField->hasFocus())
        currentPlaylistWidget()->setFocus();

    ui->searchHost->setVisible(false);
}

void PlaylistWindow::self_visibilityChanged()
{
    // When the window was (re)created/destroyed for whatever reason by
    // the toolkit/wm/etc, reveal the search widget if it was active last.
    if (showSearch)
        revealSearch();
    else
        finishSearch();
}

void PlaylistWindow::self_dockLocationChanged(Qt::DockWidgetArea area)
{
    if (area != Qt::NoDockWidgetArea)
        emit windowDocked();
}

void PlaylistWindow::playlist_removeItemRequested()
{
    auto qdp = currentPlaylistWidget();
    if (!qdp)
        return;

    qdp->traverseSelected([qdp](QUuid uuid) { qdp->removeItem(uuid); });
    updatePlaylistHasItems();
}

void PlaylistWindow::playlist_removeAllRequested()
{
    auto qdp = currentPlaylistWidget();
    if (!qdp)
        return;

    qdp->removeAll();
    updatePlaylistHasItems();
}

void PlaylistWindow::on_tabWidget_tabCloseRequested(int index)
{
    int current = ui->tabWidget->currentIndex();
    auto qdp = reinterpret_cast<QDrawnPlaylist *>(ui->tabWidget->widget(index));
    if (!qdp)
        return;
    if (qdp->uuid().isNull()) {
        qdp->removeAll();
    } else {
        PlaylistCollection::getSingleton()->removePlaylist(qdp->uuid());
        widgets.remove(qdp->uuid());
        ui->tabWidget->removeTab(index);
    }
    if (current == index)
        updateCurrentPlaylist();
}

void PlaylistWindow::on_tabWidget_tabBarDoubleClicked(int index)
{
    auto widget = reinterpret_cast<QDrawnPlaylist *>(ui->tabWidget->widget(index));
    QUuid tabUuid = widget->uuid();
    if (tabUuid.isNull())
        return;
    QInputDialog *qid = new QInputDialog(this);
    qid->setAttribute(Qt::WA_DeleteOnClose);
    qid->setWindowModality(Qt::ApplicationModal);
    qid->setWindowTitle(tr("Enter playlist name"));
    qid->setTextValue(ui->tabWidget->tabText(index).replace(QRegExp("&{1,}"), ""));
    connect(qid, &QInputDialog::accepted, [=]() {
        int tabIndex = ui->tabWidget->indexOf(widget);
        if (tabIndex < 0)
            return;
        auto pl = PlaylistCollection::getSingleton()->playlistOf(tabUuid);
        if (!pl)
            return;
        pl->setTitle(qid->textValue());
        ui->tabWidget->setTabText(tabIndex, qid->textValue().replace("&", "+"));
    });
    qid->show();
}

void PlaylistWindow::on_tabWidget_customContextMenuRequested(const QPoint &pos)
{
    QMenu *m = new QMenu(this);
    m->addAction(tr("&New Playlist"), this, SLOT(newTab()));
    m->addAction(tr("&Remove Playlist"), this, SLOT(closeTab()));
    m->addAction(tr("&Duplicate Playlist"), this, SLOT(duplicateTab()));
    m->addAction(tr("&Import Playlist"), this, SLOT(importTab()));
    m->addAction(tr("&Export Playlist"), this, SLOT(exportTab()));
    m->exec(ui->tabWidget->mapToGlobal(pos));
}

void PlaylistWindow::on_searchField_textEdited(const QString &arg1)
{
    setPlaylistFilters(arg1);
}

void PlaylistWindow::on_tabWidget_currentChanged(int index)
{
    Q_UNUSED(index);
    updateCurrentPlaylist();
}

void PlaylistWindow::on_searchField_returnPressed()
{
    playCurrentItem();
}
