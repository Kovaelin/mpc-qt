#ifndef MAIN_H
#define MAIN_H
#include <QHash>
#include <QMetaMethod>
#include "ipcjson.h"
#include "helpers.h"
#include "mainwindow.h"
#include "manager.h"
#include "storage.h"
#include "settingswindow.h"
#include "propertieswindow.h"
#include "favoriteswindow.h"
#include "qscreensaver.h"

class MprisInstance;

// a simple class to control program exection and own application objects
class Flow : public QObject {
    Q_OBJECT
public:
    explicit Flow(QObject *owner = nullptr);
    ~Flow();

    void parseArgs();
    void init();
    int run();
    bool hasPrevious();

signals:
    void recentFilesChanged(QList<TrackInfo> urls);
    void windowsRestored();

private:
    void setupMpris();
    QByteArray makePayload() const;
    QString pictureTemplate(Helpers::DisabledTrack tracks, Helpers::Subtitles subs) const;
    QVariantList recentToVList() const;
    void recentFromVList(const QVariantList &list);
    QVariantMap favoritesToVMap() const;
    void favoritesFromVMap(const QVariantMap &map);
    QVariantMap saveWindows();
    void restoreWindows(const QVariantMap &geometryMap);
    void showWindows(const QVariantMap &mainWindowMap);

private slots:
    void self_windowsRestored();
    void mainwindow_instanceShouldQuit();
    void mainwindow_recentOpened(const TrackInfo &track);
    void mainwindow_recentClear();
    void mainwindow_takeImage(Helpers::ScreenshotRender render);
    void mainwindow_takeImageAutomatically(Helpers::ScreenshotRender render);
    void mainwindow_optionsOpenRequested();
    void manager_nowPlayingChanged(QUrl url, QUuid listUuid, QUuid itemUuid);
    void manager_stateChanged(PlaybackManager::PlaybackState state);
    void settingswindow_settingsData(const QVariantMap &settings);
    void settingswindow_inhibitScreensaver(bool yes);
    void settingswindow_rememberWindowGeometry(bool yes);
    void settingswindow_keymapData(const QVariantMap &keyMap);
    void settingswindow_mprisIpc(bool enabled);
    void settingswindow_screenshotDirectory(const QString &where);
    void settingswindow_encodeDirectory(const QString &where);
    void settingswindow_screenshotTemplate(const QString &fmt);
    void settingswindow_encodeTemplate(const QString &fmt);
    void settingswindow_screenshotFormat(const QString &fmt);
    void favoriteswindow_favoriteTracks(const QList<TrackInfo> &files, const QList<TrackInfo> &streams);

    void endProgram();
    void importPlaylist(QString fname);
    void exportPlaylist(QString fname, QStringList items);

private:
    MpcQtServer *server = nullptr;
    MpvServer *mpvServer = nullptr;
    MprisInstance *mpris = nullptr;
    MainWindow *mainWindow = nullptr;
    PlaybackManager *playbackManager = nullptr;
    SettingsWindow *settingsWindow = nullptr;
    PropertiesWindow *propertiesWindow = nullptr;
    FavoritesWindow *favoritesWindow = nullptr;
    Storage storage;
    QVariantMap settings;
    QVariantMap keyMap;
    QList<TrackInfo> recentFiles;
    QList<TrackInfo> favoriteFiles;
    QList<TrackInfo> favoriteStreams;
    QScreenSaver screenSaver;

    bool freestanding = false;
    QSize cliSize;
    QPoint cliPos;
    bool validCliSize = false;
    bool validCliPos = false;
    QStringList customFiles;

    bool inhibitScreensaver = false;
    bool manipulateScreensaver = false;
    bool rememberWindowGeometry = false;
    QString screenshotDirectory;
    QString encodeDirectory;
    QString screenshotTemplate;
    QString encodeTemplate;
    QString screenshotFormat;
    bool hasPrevious_ = false;
};

#endif // MAIN_H
