/*
 * Copyright (C) 2018-2020 QuasarApp.
 * Distributed under the lgplv3 software license, see the accompanying
 * Everyone is permitted to copy and distribute verbatim copies
 * of this license document, but changing it is not allowed.
 */



#include "filemanager.h"
#include <QDir>
#include <quasarapp.h>
#include "configparser.h"
#include "deploycore.h"
#include <QProcess>
#include <fstream>
#include "pathutils.h"

#ifdef Q_OS_WIN
#include "windows.h"
#endif

FileManager::FileManager() {
}

bool FileManager::initDir(const QString &path) {

    if (!QFileInfo::exists(path)) {
        addToDeployed(path);
        if (!QDir().mkpath(path)) {
            return false;
        }
    }

    return true;
}


QSet<QString> FileManager::getDeployedFiles() const {
    return _deployedFiles;
}

QStringList FileManager::getDeployedFilesStringList() const {
    return _deployedFiles.values();
}

void FileManager::loadDeployemendFiles(const QString &targetDir) {
    auto settings = QuasarAppUtils::Settings::get();

    if (targetDir.isEmpty())
        return;

    QStringList deployedFiles = settings->getValue(targetDir, "").toStringList();

//    _deployedFiles.clear();
    _deployedFiles.unite(QSet<QString>(deployedFiles.begin(), deployedFiles.end()));
}


bool FileManager::addToDeployed(const QString& path) {
    auto info = QFileInfo(path);
    if (info.isFile() || !info.exists()) {
        _deployedFiles += info.absoluteFilePath();

        auto completeSufix = info.completeSuffix();
        if (info.isFile() && (completeSufix.isEmpty() || completeSufix.toLower() == "run"
                || completeSufix.toLower() == "sh")) {

            if (!QFile::setPermissions(path, static_cast<QFile::Permission>(0x7777))) {
                QuasarAppUtils::Params::verboseLog("permishens set fail", QuasarAppUtils::Warning);
            }
        }

#ifdef Q_OS_WIN

        if (info.isFile()) {
            auto stdString = QDir::toNativeSeparators(info.absoluteFilePath()).toStdString();

            DWORD attribute = GetFileAttributesA(stdString.c_str());
            if (!SetFileAttributesA(stdString.c_str(), attribute & static_cast<DWORD>(~FILE_ATTRIBUTE_HIDDEN))) {
                QuasarAppUtils::Params::verboseLog("attribute set fail", QuasarAppUtils::Warning);
            }
        }
#endif
    }

    return true;
}

void FileManager::saveDeploymendFiles(const QString& targetDir) {
    auto settings = QuasarAppUtils::Settings::get();
    settings->setValue(targetDir, getDeployedFilesStringList());
}

bool FileManager::strip(const QString &dir) const {

#ifdef Q_OS_WIN
    Q_UNUSED(dir)
    return true;
#else
    QFileInfo info(dir);

    if (!info.exists()) {
        QuasarAppUtils::Params::verboseLog("dir not exits!");
        return false;
    }

    if (info.isDir()) {
        QDir d(dir);
        auto list = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

        bool res = false;
        for (const auto &i : list) {
            res = strip(i.absoluteFilePath()) || res;
        }

        return res;
    } else {

        auto sufix = info.completeSuffix();
        if (!sufix.contains("so") && !sufix.contains("dll")) {
            return true;
        }

        QProcess P;
        P.setProgram("strip");
        P.setArguments(QStringList() << info.absoluteFilePath());
        P.start();

        if (!P.waitForStarted())
            return false;
        if (!P.waitForFinished())
            return false;

        return P.exitCode() == 0;
    }
#endif
}


bool FileManager::fileActionPrivate(const QString &file, const QString &target,
                                         QStringList *masks, bool isMove) {
    
    auto info = QFileInfo(file);
    
    bool copy = !masks;
    if (masks) {
        for (auto mask : *masks) {
            if (info.absoluteFilePath().contains(mask, ONLY_WIN_CASE_INSENSIATIVE)) {
                copy = true;
                break;
            }
        }
    }

    if (!copy) {
        QuasarAppUtils::Params::verboseLog(((isMove)? "skip move :": "skip copy :" + file));
        return false;
    }

    auto name = info.fileName();
    info.setFile(target + QDir::separator() + name);

    if (!initDir(info.absolutePath())) {
        return false;
    }

    if (QFileInfo(file).absoluteFilePath() ==
            QFileInfo(target + QDir::separator() + name).absoluteFilePath()) {
        return true;
    }

    if (!QuasarAppUtils::Params::isEndable("noOverwrite") &&
            info.exists() && !removeFile( target + QDir::separator() + name)) {
        return false;
    }

    qInfo() << ((isMove)? "move :": "copy :") << file;

    QFile sourceFile(file);

    if (!((isMove)?
          sourceFile.rename(target + QDir::separator() + name):
          sourceFile.copy(target + QDir::separator() + name))) {

        QuasarAppUtils::Params::verboseLog("Qt Operation fail " + file + " >> " + target + QDir::separator() + name +
                                           " Qt error: " + sourceFile.errorString(),
                                           QuasarAppUtils::Warning);

        bool tarExits = QFileInfo(target + QDir::separator() + name).exists();

        if ((!tarExits) ||
            (tarExits && !QuasarAppUtils::Params::isEndable("noOverwrite"))) {

            std::ifstream  src(file.toStdString(),
                               std::ios::binary);

            std::ofstream  dst((target + QDir::separator() + name).toStdString(),
                               std::ios::binary);

            dst << src.rdbuf();

            if (!QFileInfo::exists(target + QDir::separator() + name)) {
                QuasarAppUtils::Params::verboseLog("std Operation fail file not copied. "
                                                   "Сheck if you have access to the target dir",
                                                   QuasarAppUtils::Error);
                return false;

            }

            if (isMove) {
                std::remove(file.toStdString().c_str());
            }

        } else {

            if (QFileInfo(target + QDir::separator() + name).exists()) {
                qInfo() << target + QDir::separator() + name << " already exists!";
                return true;
            }

            return false;
        }
    }

    addToDeployed(target + QDir::separator() + name);
    return true;
}

bool FileManager::removeFile(const QString &file) {
    return removeFile(QFileInfo (file));
}

bool FileManager::smartCopyFile(const QString &file, const QString &target, QStringList *mask) {
    auto config = DeployCore::_config;

    if (file.contains(config->getTargetDir(), ONLY_WIN_CASE_INSENSIATIVE)) {
        if (!moveFile(file, target, mask)) {
            QuasarAppUtils::Params::verboseLog(" file not moved! try copy");

            if (!copyFile(file, target, mask)) {
                qCritical() << "not copy target to bin dir " << file;
                return false;
            }
        }
    } else {
        if (!copyFile(file, target, mask)) {
            qCritical() << "not copy target to bin dir " << file;
            return false;
        }
    }

    return true;
}

bool FileManager::moveFile(const QString &file, const QString &target, QStringList *masks) {
    return fileActionPrivate(file, target, masks, true);
}

bool FileManager::copyFolder(const QString &from, const QString &to, const QStringList &filter,
                        QStringList *listOfCopiedItems, QStringList *mask) {

    QDir fromDir(from);

    auto list = fromDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);

    for (const auto &item : list) {
        if (QFileInfo(item).isDir()) {

            copyFolder(item.absoluteFilePath(), to + "/" + item.fileName(), filter, listOfCopiedItems, mask);
        } else {

            QString skipFilter = "";
            for (const auto &i: filter) {
                if (item.fileName().contains(i, ONLY_WIN_CASE_INSENSIATIVE)) {
                    skipFilter = i;
                    break;
                }
            }

            if (!skipFilter.isEmpty()) {
                QuasarAppUtils::Params::verboseLog(
                            item.absoluteFilePath() + " ignored by filter " + skipFilter,
                            QuasarAppUtils::VerboseLvl::Info);
                continue;
            }
            auto config = DeployCore::_config;

            LibInfo info;
            info.setName(item.fileName());
            info.setPath(item.absolutePath());
            info.setPlatform(GeneralFile);

            if (config)
                if (auto rule = config->ignoreList.isIgnore(info)) {
                    QuasarAppUtils::Params::verboseLog(
                                item.absoluteFilePath() + " ignored by rule " + rule->label,
                                QuasarAppUtils::VerboseLvl::Info);
                    continue;
                }

            if (!copyFile(item.absoluteFilePath(), to , mask)) {
                QuasarAppUtils::Params::verboseLog(
                            "not copied file " + to + "/" + item.fileName(),
                            QuasarAppUtils::VerboseLvl::Warning);
                continue;
            }

            if (listOfCopiedItems) {
                *listOfCopiedItems << to + "/" + item.fileName();
            }
        }
    }

    return true;
}

bool FileManager::moveFolder(const QString &from, const QString &to, const QString& ignore) {
    QFileInfo info(from);

    if (info.isFile()) {

        if (ignore.size() && info.absoluteFilePath().contains(ignore)) {
            return true;
        }

        if (!moveFile(info.absoluteFilePath(), to)) {
            return false;
        }
        return true;
    }

    QDir dir(from);
    auto list = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &i :list) {
        auto targetDir = to;
        if (i.isDir()) {
            targetDir += "/" + i.fileName();
        }

        if (!moveFolder(i.absoluteFilePath(), targetDir, ignore)) {
            return false;
        }
    }

    return true;
}

void FileManager::clear(const QString& targetDir, bool force) {
    qInfo() << "clear start!";

    if (force) {
        qInfo() << "clear force! " << targetDir;

        if (QDir(targetDir).removeRecursively()) {
            return;
        }

        QuasarAppUtils::Params::verboseLog("Remove target Dir fail, try remove old deployemend files",
                                           QuasarAppUtils::Warning);
    }

    QMap<int, QFileInfo> sortedOldData;
    for (auto& i : _deployedFiles) {
        sortedOldData.insertMulti(i.size(), QFileInfo(i));
    }

    for (auto it = sortedOldData.end(); it != sortedOldData.begin(); --it) {

        auto index = it - 1;

        if (!index.value().exists()) {
            continue;
        }

        if (index.value().isFile()) {
            if (removeFile(index.value())) {
                qInfo() << "Remove " << index.value().absoluteFilePath() << " because it is deployed file";
            }

        } else {
            QDir qdir(index.value().absoluteFilePath());
            if (!qdir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).count()) {
                qdir.removeRecursively();
                qInfo() << "Remove " << index.value().absoluteFilePath() << " because it is empty";
            }
        }
    }

    _deployedFiles.clear();
}

bool FileManager::copyFile(const QString &file, const QString &target,
                      QStringList *masks) {

    return fileActionPrivate(file, target, masks, false);
}

bool FileManager::removeFile(const QFileInfo &file) {

    if (!QFile::remove(file.absoluteFilePath())) {
        QuasarAppUtils::Params::verboseLog("Qt Operation fail (remove file) " + file.absoluteFilePath(),
                                           QuasarAppUtils::Warning);

        if (remove(file.absoluteFilePath().toLatin1())) {
            QuasarAppUtils::Params::verboseLog("std Operation fail file not removed." + file.absoluteFilePath(),
                                               QuasarAppUtils::Error);
            return false;
        }
    }

    return true;
}
