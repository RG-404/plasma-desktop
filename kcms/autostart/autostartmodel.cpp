/***************************************************************************
 *   Copyright (C) 2020 by Méven Car <meven.car@enioka.com>                *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA          *
 ***************************************************************************/
#include "autostartmodel.h"

#include <QStandardPaths>
#include <QDir>
#include <KDesktopFile>
#include <KShell>
#include <KConfigGroup>
#include <QDebug>

#include <KLocalizedString>
#include <KIO/DeleteJob>
#include <KIO/CopyJob>
#include <KService>
#include <KFileItem>
#include <KPropertiesDialog>
#include <KOpenWithDialog>

// FDO user autostart directories are
// .config/autostart which has .desktop files executed by klaunch

//Then we have Plasma-specific locations which run scripts
// .config/autostart-scripts which has scripts executed by ksmserver
// .config/plasma-workspace/shutdown which has scripts executed by startkde
// .config/plasma-workspace/env which has scripts executed by startkde

//in the case of pre-startup they have to end in .sh
//everywhere else it doesn't matter

//the comment above describes how autostart *currently* works, it is not definitive documentation on how autostart *should* work

// share/autostart shouldn't be an option as this should be reserved for global autostart entries

static bool checkEntry(const AutostartEntry &entry)
{
    const QStringList commandLine = KShell::splitArgs(entry.command);
    if (commandLine.isEmpty()) {
        return false;
    }

    if (!entry.enabled) {
        return false;
    }

    const QString exe = commandLine.first();
    if (exe.isEmpty() || QStandardPaths::findExecutable(exe).isEmpty()) {
        return false;
    }
    return true;
}

static AutostartEntry loadDesktopEntry(const QString &fileName)
{
    KDesktopFile config(fileName);
    const KConfigGroup grp = config.desktopGroup();
    const auto name = config.readName();
    const auto command = grp.readEntry("Exec");

    const bool hidden = grp.readEntry("Hidden", false);
    const QStringList notShowList = grp.readXdgListEntry("NotShowIn");
    const QStringList onlyShowList = grp.readXdgListEntry("OnlyShowIn");
    const bool enabled = !(hidden ||
                           notShowList.contains(QLatin1String("KDE")) ||
                           (!onlyShowList.isEmpty() && !onlyShowList.contains(QLatin1String("KDE"))));

    const auto lstEntry = grp.readXdgListEntry("OnlyShowIn");
    const bool onlyInPlasma = lstEntry.contains(QLatin1String("KDE"));
    const QString iconName = config.readIcon();

    return {
        name,
        command,
        AutostartModel::AutostartEntrySource::XdgAutoStart, // .config/autostart load desktop at startup
        enabled,
        fileName,
        onlyInPlasma,
        iconName
    };
}

AutostartModel::AutostartModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

QString AutostartModel::XdgAutoStartPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/autostart/");
}

void AutostartModel::load()
{
    beginResetModel();

    m_entries.clear();

    QDir autostartdir(XdgAutoStartPath());
    if (!autostartdir.exists()) {
        autostartdir.mkpath(XdgAutoStartPath());
    }

    autostartdir.setFilter(QDir::Files | QDir::NoDotAndDotDot);

    const auto filesInfo = autostartdir.entryInfoList();
    for (const QFileInfo &fi : filesInfo) {

        if (!KDesktopFile::isDesktopFile(fi.fileName())) {
            continue;
        }

        const AutostartEntry entry = loadDesktopEntry(fi.absoluteFilePath());

        if (!checkEntry(entry)) {
            continue;
        }

        m_entries.push_back(entry);
    }

    m_lastApplication = m_entries.size() - 1;

    loadScriptsFromDir(QStringLiteral("/autostart-scripts/"), AutostartModel::AutostartEntrySource::XdgScripts);
    // Treat them as XdgScripts so they appear together in the UI
    loadScriptsFromDir(QStringLiteral("/plasma-workspace/env/"), AutostartModel::AutostartEntrySource::XdgScripts);
    m_lastLoginScript = m_entries.size() - 1;

    loadScriptsFromDir(QStringLiteral("/plasma-workspace/shutdown/"), AutostartModel::AutostartEntrySource::PlasmaShutdown);

    endResetModel();
}

void AutostartModel::loadScriptsFromDir(const QString& subDir, AutostartModel::AutostartEntrySource kind)
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + subDir;
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(path);
    }

    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);

    const auto autostartDirFilesInfo = dir.entryInfoList();
    for (const QFileInfo &fi : autostartDirFilesInfo) {

        QString fileName = fi.absoluteFilePath();
        const bool isSymlink = fi.isSymLink();
        if (isSymlink) {
            fileName = fi.symLinkTarget();
        }

        m_entries.push_back({
            fi.fileName(),
            isSymlink ? fileName : QString(),
            kind,
            true,
            fi.absoluteFilePath(),
            false,
            QStringLiteral("dialog-scripts")
        });
    }
}

int AutostartModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_entries.count();
}

bool AutostartModel::reloadEntry(const QModelIndex &index, const QString &fileName)
{
    if (!checkIndex(index)) {
        return false;
    }

    AutostartEntry newEntry = loadDesktopEntry(fileName);
    if (!checkEntry(newEntry)) {
        return false;
    }

    m_entries.replace(index.row(), newEntry);
    Q_EMIT dataChanged(index, index);
    return true;
}

QVariant AutostartModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index)) {
        return QVariant();
    }

    const auto &entry = m_entries.at(index.row());

    switch (role) {
    case Qt::DisplayRole: return entry.name;
    case Command: return entry.command;
    case Enabled: return entry.enabled;
    case Source: return entry.source;
    case FileName: return entry.fileName;
    case OnlyInPlasma: return entry.onlyInPlasma;
    case IconName: return entry.iconName;
    }

    return QVariant();
}

void AutostartModel::addApplication(const KService::Ptr &service)
{
    QString desktopPath;
    // It is important to ensure that we make an exact copy of an existing
    // desktop file (if selected) to enable users to override global autostarts.
    // Also see
    // https://bugs.launchpad.net/ubuntu/+source/kde-workspace/+bug/923360
    if (service->desktopEntryName().isEmpty() || service->entryPath().isEmpty()) {
        // create a new desktop file in s_desktopPath
        desktopPath = XdgAutoStartPath() + service->name() + QStringLiteral(".desktop");

        KDesktopFile desktopFile(desktopPath);
        KConfigGroup kcg = desktopFile.desktopGroup();
        kcg.writeEntry("Name", service->name());
        kcg.writeEntry("Exec", service->exec());
        kcg.writeEntry("Icon", service->icon());
        kcg.writeEntry("Path", "");
        kcg.writeEntry("Terminal", service->terminal() ? "True": "False");
        kcg.writeEntry("Type", "Application");
        desktopFile.sync();

    } else {
        desktopPath = XdgAutoStartPath() + service->desktopEntryName() + QStringLiteral(".desktop");

        QFile::remove(desktopPath);

        // copy original desktop file to new path
        KDesktopFile desktopFile(service->entryPath());
        auto newDeskTopFile = desktopFile.copyTo(desktopPath);
        newDeskTopFile->sync();
    }

    const auto entry = AutostartEntry{
        service->name(),
        service->exec(),
        AutostartModel::AutostartEntrySource:: XdgAutoStart, // .config/autostart load desktop at startup
        true,
        desktopPath,
        false,
        service->icon()
    };

    // push before the script items
    const int index = m_lastApplication + 1;

    beginInsertRows(QModelIndex(), index, index);

    m_entries.insert(index, entry);
    ++m_lastApplication;
    ++m_lastLoginScript;

    endInsertRows();
}

void AutostartModel::showApplicationDialog()
{
    KOpenWithDialog *owdlg = new KOpenWithDialog();
    connect(owdlg, &QDialog::finished, this, [this, owdlg] (int result) {
        if (result == QDialog::Accepted) {

            const KService::Ptr service = owdlg->service();

            Q_ASSERT(service);
            if (!service) {
                return; // Don't crash if KOpenWith wasn't able to create service.
            }

            addApplication(service);
        }
    });
    owdlg->open();
}

void AutostartModel::addScript(const QUrl &url, AutostartModel::AutostartEntrySource kind)
{
    const QFileInfo file(url.toLocalFile());

    if (!file.isAbsolute()) {
        Q_EMIT error(i18n("\"%1\" is not an absolute url.", url.toLocalFile()));
        return;
    } else if (!file.exists()) {
        Q_EMIT error(i18n("\"%1\" does not exist.", url.toLocalFile()));
        return;
    } else if (!file.isFile()) {
        Q_EMIT error(i18n("\"%1\" is not a file.", url.toLocalFile()));
        return;
    } else if (!file.isReadable()) {
        Q_EMIT error(i18n("\"%1\" is not readable.", url.toLocalFile()));
        return;
    }

    const QString fileName = url.fileName();
    int index = 0;
    QString folder;

    if (kind == AutostartModel::AutostartEntrySource::XdgScripts) {
        index = m_lastLoginScript + 1;
        folder = QStringLiteral("/autostart-scripts/");
    } else if (kind == AutostartModel::AutostartEntrySource::PlasmaShutdown) {
        index = m_entries.size();
        folder = QStringLiteral("/plasma-workspace/shutdown/");
    } else {
        Q_ASSERT(0);
    }

    beginInsertRows(QModelIndex(), index, index);

    QUrl destinationScript = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + folder + fileName);
    KIO::CopyJob *job = KIO::link(url, destinationScript, KIO::HideProgressInfo);

    connect(job, &KIO::CopyJob::renamed, this, [&destinationScript](KIO::Job * job, const QUrl & from, const QUrl & to) {
        Q_UNUSED(job)
        Q_UNUSED(from)
        // in case the destination filename had to be renamed
        destinationScript = to;
    });

    job->exec();

    AutostartEntry entry = AutostartEntry{
        destinationScript.fileName(),
        url.path(),
        kind,
        true,
        destinationScript.path(),
        false,
        QStringLiteral("dialog-scripts")
    };

     m_entries.insert(index, entry);

     if (kind == AutostartModel::AutostartEntrySource::XdgScripts) {
        ++m_lastLoginScript;
     }

     endInsertRows();
}

void AutostartModel::removeEntry(int row)
{
    const auto entry = m_entries.at(row);

    KIO::DeleteJob* job = KIO::del(QUrl::fromLocalFile(entry.fileName), KIO::HideProgressInfo);

    beginRemoveRows(QModelIndex(), row, row);
    job->start();
    m_entries.remove(row);

    if (entry.source == AutostartModel::AutostartEntrySource::XdgScripts) {
        Q_ASSERT(m_lastLoginScript > 0);
        --m_lastLoginScript;
    } else if (entry.source == AutostartModel::AutostartEntrySource::XdgAutoStart) {
        Q_ASSERT(m_lastApplication > 0);
        --m_lastApplication;

        if (m_lastLoginScript > 0) {
            --m_lastLoginScript;
        }
    }

    endRemoveRows();
}

QHash<int, QByteArray> AutostartModel::roleNames() const
{
    QHash<int, QByteArray> roleNames = QAbstractListModel::roleNames();

    roleNames.insert(Name, QByteArrayLiteral("name"));
    roleNames.insert(Command, QByteArrayLiteral("command"));
    roleNames.insert(Enabled, QByteArrayLiteral("enabled"));
    roleNames.insert(Source, QByteArrayLiteral("source"));
    roleNames.insert(FileName, QByteArrayLiteral("fileName"));
    roleNames.insert(OnlyInPlasma, QByteArrayLiteral("onlyInPlasma"));
    roleNames.insert(IconName, QByteArrayLiteral("iconName"));

    return roleNames;
}

void AutostartModel::editApplication(int row)
{
    const QModelIndex idx = index(row, 0);

    const QString fileName = data(idx, AutostartModel::Roles::FileName).toString();
    KFileItem kfi(QUrl::fromLocalFile(fileName));
    kfi.setDelayedMimeTypes(true);

    KPropertiesDialog *dlg = new KPropertiesDialog(kfi, nullptr);
    connect(dlg, &QDialog::finished, this, [this, idx, dlg] (int result) {
        if (result == QDialog::Accepted) {
            reloadEntry(idx, dlg->item().localPath());
        }
    });
    dlg->open();
}
