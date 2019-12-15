/*
 * Copyright (C) 2018-2020 QuasarApp.
 * Distributed under the lgplv3 software license, see the accompanying
 * Everyone is permitted to copy and distribute verbatim copies
 * of this license document, but changing it is not allowed.
 */

#include "extracter.h"
#include "deploycore.h"
#include "pluginsparser.h"
#include "configparser.h"
#include "metafilemanager.h"
#include "pathutils.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <quasarapp.h>
#include <stdio.h>


#include <fstream>

bool Extracter::deployMSVC() {
    qInfo () << "try deploy msvc";
    auto msvcInstaller = DeployCore::getVCredist(DeployCore::_config->qtDir.bins);

    if (msvcInstaller.isEmpty()) {
        return false;
    }

    return _fileManager->copyFile(msvcInstaller, DeployCore::_config->getTargetDir());
}

bool Extracter::isWebEngine(const QString &prefix) const {
    auto qtModules = _prefixDependencyes.value(prefix).qtModules();

    return static_cast<quint64>(qtModules) & static_cast<quint64>(DeployCore::QtModule::QtWebEngineModule);
}

bool Extracter::extractWebEngine() {

    auto cnf = DeployCore::_config;

    for (auto i = cnf->prefixes.cbegin(); i != cnf->prefixes.cend(); ++i) {

        auto prefix = i.key();
        if (isWebEngine(prefix)) {
            auto webEngeneBin = DeployCore::_config->qtDir.libexecs;
            if (DeployCore::_config->qtDir.qtPlatform & Platform::Unix) {
                webEngeneBin += "/QtWebEngineProcess";
            } else {
                webEngeneBin += "/QtWebEngineProcess.exe";
            }

            auto destWebEngine = DeployCore::_config->getTargetDir() + prefix + DeployCore::_config->prefixes[prefix].getBinOutDir();
            auto resOut = DeployCore::_config->getTargetDir() + prefix + DeployCore::_config->prefixes[prefix].getResOutDir();
            auto res = DeployCore::_config->qtDir.resources;

            if (!_fileManager->copyFile(webEngeneBin, destWebEngine)) {
                return false;
            }

            if (!_fileManager->copyFolder(res, resOut)) {
                return false;
            }
        }
    }

    return true;
}

bool Extracter::copyPlugin(const QString &plugin, const QString& prefix) {

    QStringList listItems;

    auto cnf = DeployCore::_config;
    auto targetPath = cnf->getTargetDir() + prefix;
    auto distro = cnf->getDistroFromPrefix(prefix);


    auto pluginPath = targetPath + distro.getPluginsOutDir() +
            QFileInfo(plugin).fileName();

    if (!_fileManager->copyFolder(plugin, pluginPath,
                    QStringList() << ".so.debug" << "d.dll", &listItems)) {
        return false;
    }

    for (auto item : listItems) {
        if (QuasarAppUtils::Params::isEndable("extractPlugins")) {
            extract(item, &_prefixDependencyes[prefix]);
        } else {
            extract(item, &_prefixDependencyes[prefix], "Qt");
        }
    }

    return true;
}

void Extracter::copyExtraPlugins(const QString& prefix) {
    QFileInfo info;

    auto cnf = DeployCore::_config;
    auto targetPath = cnf->getTargetDir() + prefix;
    auto distro = cnf->getDistroFromPrefix(prefix);

    for (auto extraPlugin : DeployCore::_config->extraPlugins) {

        if (!PathUtils::isPath(extraPlugin)) {
            extraPlugin = DeployCore::_config->qtDir.plugins + "/" + extraPlugin;
        }

        info.setFile(extraPlugin);
        if (info.isDir() && DeployCore::_config->qtDir.isQt(info.absoluteFilePath())) {
            copyPlugin(info.absoluteFilePath(), prefix);

        } else if (info.exists()) {
            _fileManager->copyFile(info.absoluteFilePath(),
                                  targetPath + distro.getPluginsOutDir());

            if (QuasarAppUtils::Params::isEndable("extractPlugins")) {
                extract(info.absoluteFilePath(), &_prefixDependencyes[prefix]);
            } else {
                extract(info.absoluteFilePath(), &_prefixDependencyes[prefix], "Qt");
            }
        }
    }
}

void Extracter::copyPlugins(const QStringList &list, const QString& prefix) {
    for (auto plugin : list) {        
        if (!copyPlugin(plugin, prefix)) {
            qWarning() << plugin << " not copied!";
        }
    }
    copyExtraPlugins(prefix);
}

void Extracter::extractAllTargets() {
    auto cfg = DeployCore::_config;
    for (auto i = cfg->prefixes.cbegin(); i != cfg->prefixes.cend(); ++i) {
        _prefixDependencyes[i.key()] = {};

        for (auto target : i.value().targets()) {
            extract(target, &_prefixDependencyes[i.key()]);
        }
    }
}

void Extracter::clear() {
    if (QuasarAppUtils::Params::isEndable("clear") ||
            QuasarAppUtils::Params::isEndable("force-clear")) {
        qInfo() << "clear old data";

        _fileManager->clear(DeployCore::_config->getTargetDir(),
                            QuasarAppUtils::Params::isEndable("force-clear"));
    }
}

void Extracter::extractPlugins() {
    auto cnf = DeployCore::_config;
    PluginsParser pluginsParser;

    for (auto i = cnf->prefixes.cbegin(); i != cnf->prefixes.cend(); ++i) {
        auto targetPath = cnf->getTargetDir() + i.key();
        auto distro = cnf->getDistroFromPrefix(i.key());

        QStringList plugins;
        pluginsParser.scan(cnf->qtDir.plugins, plugins, _prefixDependencyes[i.key()].qtModules());
        copyPlugins(plugins, i.key());
    }
}

void Extracter::copyLibs(const QSet<QString> &files, const QString& prefix) {
    auto cnf = DeployCore::_config;
    auto targetPath = cnf->getTargetDir() + prefix;
    auto distro = cnf->getDistroFromPrefix(prefix);

    for (auto file : files) {
        QFileInfo target(file);

        if (!_fileManager->smartCopyFile(file, targetPath + distro.getLibOutDir())) {
            QuasarAppUtils::Params::verboseLog(file + " not copied");
        }
    }
}

void Extracter::copyFiles() {
    auto cnf = DeployCore::_config;

    for (auto i = cnf->prefixes.cbegin(); i != cnf->prefixes.cend(); ++i) {

        copyLibs(_prefixDependencyes[i.key()].neadedLibs(), i.key());

        if (QuasarAppUtils::Params::isEndable("deploySystem")) {
            copyLibs(_prefixDependencyes[i.key()].systemLibs(), i.key());
        }

        if (!QuasarAppUtils::Params::isEndable("noStrip") && !_fileManager->strip(cnf->getTargetDir())) {
            QuasarAppUtils::Params::verboseLog("strip failed!");
        }
    }
}

void Extracter::copyTr() {

    if (!QuasarAppUtils::Params::isEndable("noTranslations")) {

        auto cnf = DeployCore::_config;

        for (auto i = cnf->prefixes.cbegin(); i != cnf->prefixes.cend(); ++i) {
            if (!copyTranslations(DeployCore::extractTranslation(_prefixDependencyes[i.key()].neadedLibs()),
                                  i.key())) {
                QuasarAppUtils::Params::verboseLog("Failed to copy standard Qt translations",
                                                   QuasarAppUtils::Warning);
            }
        }

    }
}

void Extracter::deploy() {
    qInfo() << "target deploy started!!";

    clear();
    _cqt->smartMoveTargets();
    _scaner->setEnvironment(DeployCore::_config->envirement.deployEnvironment());
    extractAllTargets();

    if (DeployCore::_config->deployQml && !extractQml()) {
        qCritical() << "qml not extacted!";
    }

    extractPlugins();

    copyFiles();

    copyTr();

    if (!extractWebEngine()) {
        QuasarAppUtils::Params::verboseLog("deploy webEngine failed", QuasarAppUtils::Error);
    }

    if (!deployMSVC()) {
        QuasarAppUtils::Params::verboseLog("deploy msvc failed");
    }

    _metaFileManager->createRunMetaFiles();

    qInfo() << "deploy done!";

}

bool Extracter::copyTranslations(QStringList list, const QString& prefix) {

    auto cnf = DeployCore::_config;

    QDir dir(cnf->qtDir.translations);
    if (!dir.exists() || list.isEmpty()) {
        return false;
    }

    QStringList filters;
    for (auto &&i: list) {
        filters.push_back("*" + i + "*");
    }

    auto listItems = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot);

    auto targetPath = cnf->getTargetDir() + prefix;
    auto distro = cnf->getDistroFromPrefix(prefix);

    for (auto &&i: listItems) {
        _fileManager->copyFile(i.absoluteFilePath(), targetPath + distro.getTrOutDir());
    }

    if (isWebEngine(prefix)) {
        auto trOut = targetPath + distro.getTrOutDir();
        auto tr = DeployCore::_config->qtDir.translations + "/qtwebengine_locales";
        _fileManager->copyFolder(tr, trOut + "/qtwebengine_locales");
    }

    return true;
}



QFileInfoList Extracter::findFilesInsideDir(const QString &name,
                                         const QString &dirpath) {
    QFileInfoList files;

    QDir dir(dirpath);

    auto list = dir.entryInfoList( QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (auto && item :list) {
        if (item.isFile()) {
            if (item.fileName().contains(name)) {
                files += item;
            }
        } else {
            files += findFilesInsideDir(name, item.absoluteFilePath());
        }
    }

    return files;
}

void Extracter::extractLib(const QString &file,
                           DependencyMap* depMap,
                           const QString& mask) {

    assert(depMap);

    qInfo() << "extract lib :" << file;

    auto data = _scaner->scan(file);

    for (auto &line : data) {

        if (mask.size() && !line.getName().contains(mask)) {
            continue;
        }

        if (DeployCore::_config->ignoreList.isIgnore(line)) {
            continue;
        }

        if (line.getPriority() != LibPriority::SystemLib && !depMap->containsNeadedLib(line.fullPath())) {
            depMap->addNeadedLib(line.fullPath());

        } else if (QuasarAppUtils::Params::isEndable("deploySystem") &&
                    line.getPriority() == LibPriority::SystemLib &&
                    !depMap->containsSysLib(line.fullPath())) {

            depMap->addSystemLib(line.fullPath());
        }
    }
}

bool Extracter::extractQmlAll() {

    if (!QFileInfo::exists(DeployCore::_config->qtDir.qmls)) {
        qWarning() << "qml dir wrong!";
        return false;
    }

    auto cnf = DeployCore::_config;


    for (auto i = cnf->prefixes.cbegin(); i != cnf->prefixes.cend(); ++i) {
        auto targetPath = cnf->getTargetDir() + i.key();
        auto distro = cnf->getDistroFromPrefix(i.key());

        QStringList listItems;

        if (!_fileManager->copyFolder(cnf->qtDir.qmls, targetPath + distro.getQmlOutDir(),
                        QStringList() << ".so.debug" << "d.dll" << ".pdb",
                        &listItems)) {
            return false;
        }

        for (auto item : listItems) {
            if (QuasarAppUtils::Params::isEndable("extractPlugins")) {
                extract(item, &_prefixDependencyes[i.key()]);
            } else {
                extract(item, &_prefixDependencyes[i.key()], "Qt");
            }
        }

    }

    return true;
}

bool Extracter::extractQmlFromSource(const QString& sourceDir) {

    QFileInfo info(sourceDir);
    auto cnf = DeployCore::_config;

    if (!info.isDir()) {
        qCritical() << "extract qml fail! qml source dir not exits or is not dir " << sourceDir;
        return false;
    }

    QuasarAppUtils::Params::verboseLog("extractQmlFromSource " + info.absoluteFilePath());

    if (!QFileInfo::exists(cnf->qtDir.qmls)) {
        qWarning() << "qml dir wrong!";
        return false;
    }

    QStringList plugins;
    QStringList listItems;
    QStringList filter;
    filter << ".so.debug" << "d.dll" << ".pdb";

    QML ownQmlScaner(cnf->qtDir.qmls);

    if (!ownQmlScaner.scan(plugins, info.absoluteFilePath())) {
        QuasarAppUtils::Params::verboseLog("qml scaner run failed!");
        return false;
    }

    for (auto i = cnf->prefixes.cbegin(); i != cnf->prefixes.cend(); ++i) {
        auto targetPath = cnf->getTargetDir() + i.key();
        auto distro = cnf->getDistroFromPrefix(i.key());

        if (!_fileManager->copyFolder(cnf->qtDir.qmls,
                                     targetPath + distro.getQmlOutDir(),
                        filter , &listItems, &plugins)) {
            return false;
        }

        for (auto item : listItems) {
            if (QuasarAppUtils::Params::isEndable("extractPlugins")) {
                extract(item, &_prefixDependencyes[i.key()]);
            } else {
                extract(item, &_prefixDependencyes[i.key()], "Qt");
            }
        }

    }

    return true;
}

bool Extracter::extractQml() {

    if (QuasarAppUtils::Params::isEndable("qmlDir")) {
        return extractQmlFromSource(
                    QuasarAppUtils::Params::getStrArg("qmlDir"));

    } else if (QuasarAppUtils::Params::isEndable("allQmlDependes")) {
        return extractQmlAll();

    } else {
        return false;
    }
}

void Extracter::extract(const QString &file,
                        DependencyMap *depMap,
                        const QString &mask) {

    assert(depMap);

    QFileInfo info(file);

    auto sufix = info.completeSuffix();

    if (sufix.compare("dll", Qt::CaseSensitive) == 0 ||
            sufix.compare("exe", Qt::CaseSensitive) == 0 ||
            sufix.isEmpty() || sufix.contains("so", Qt::CaseSensitive)) {

        extractLib(file, depMap, mask);
    } else {
        QuasarAppUtils::Params::verboseLog("file with sufix " + sufix + " not supported!");
    }

}

Extracter::Extracter(FileManager *fileManager, ConfigParser *cqt,
                     DependenciesScanner *scaner):
    _scaner(scaner),
    _fileManager(fileManager),
    _cqt(cqt)
    {

    assert(_cqt);
    assert(_fileManager);
    assert(DeployCore::_config);

    _metaFileManager = new MetaFileManager(_fileManager);
}

