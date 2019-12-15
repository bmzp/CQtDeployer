/*
 * Copyright (C) 2018-2020 QuasarApp.
 * Distributed under the lgplv3 software license, see the accompanying
 * Everyone is permitted to copy and distribute verbatim copies
 * of this license document, but changing it is not allowed.
 */

#ifndef EXTRACTER_H
#define EXTRACTER_H
#include <QDir>
#include <QString>
#include <QStringList>
#include <dependenciesscanner.h>
#include "dependencymap.h"
#include "deploy_global.h"
#include "filemanager.h"
#include "qml.h"

class ConfigParser;
class MetaFileManager;

class DEPLOYSHARED_EXPORT Extracter {
  private:

    QHash<QString, DependencyMap> _prefixDependencyes;

    DependenciesScanner *_scaner;
    FileManager *_fileManager;
    ConfigParser *_cqt;
    MetaFileManager *_metaFileManager;

    void extract(const QString &file, DependencyMap* depMap, const QString& mask = "");
    bool copyTranslations(QStringList list, const QString &prefix);

    bool extractQml();

    QFileInfoList findFilesInsideDir(const QString &name, const QString &dirpath);
    bool extractQmlAll();
    bool extractQmlFromSource(const QString &sourceDir);
    /**
     * @brief extractLib
     * @param file file of lib
     * @param mask  extraction mask. Used to filter extracts objects
     */
    void extractLib(const QString & file, DependencyMap *depMap, const QString& mask = "");

    bool deployMSVC();
    bool extractWebEngine();


    bool copyPlugin(const QString &plugin, const QString &prefix);
    void copyPlugins(const QStringList &list, const QString &prefix);

    /**
     * @brief compress - this function join all target dependecies in to one struct
     */
    void compress();
    void extractAllTargets();
    void extractPlugins();
    void copyFiles();
    void copyTr();
    void copyExtraPlugins(const QString &prefix);
    void copyLibs(const QSet<QString> &files, const QString &prefix);

    bool isWebEngine(const QString& prefix) const;
public:
    explicit Extracter(FileManager *fileManager, ConfigParser * cqt, DependenciesScanner *_scaner);
    void deploy();
    void clear();

    friend class deploytest;
};

#endif // EXTRACTER_H_H
