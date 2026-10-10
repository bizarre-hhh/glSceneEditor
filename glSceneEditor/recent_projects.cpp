#include "recent_projects.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

QString NormalizePath(const QString& path)
{
    const QFileInfo info(QDir::fromNativeSeparators(path));
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}
} // namespace

namespace RecentProjects
{
QString RecordFilePath()
{
    return QStringLiteral("D:/vs_cmake_proj/glSceneEditor/asset/recent_projects.json");
}

QStringList Read(const QString& record_file)
{
    QFile file(record_file);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return {};
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("version")).toDouble() != 1 ||
        !object.value(QStringLiteral("projects")).isArray())
        return {};

    QStringList paths;
    for (const auto& value : object.value(QStringLiteral("projects")).toArray())
    {
        const QString path = value.toString();
        if (path.isEmpty() || !QDir::isAbsolutePath(QDir::fromNativeSeparators(path)) ||
            !path.endsWith(QStringLiteral(".glproj"), Qt::CaseInsensitive))
            continue;
        const QString normalized = NormalizePath(path);
        if (!paths.contains(normalized, kPathCase))
            paths.append(normalized);
        if (paths.size() == kMaxProjects)
            break;
    }
    return paths;
}

bool Remember(const QString& project_path, QString& error, const QString& record_file)
{
    error.clear();
    if (project_path.isEmpty())
    {
        error = QStringLiteral("工程路径为空。");
        return false;
    }
    const QString normalized = NormalizePath(project_path);
    QStringList paths = Read(record_file);
    for (int i = paths.size() - 1; i >= 0; --i)
        if (paths[i].compare(normalized, kPathCase) == 0)
            paths.removeAt(i);
    paths.prepend(normalized);
    while (paths.size() > kMaxProjects)
        paths.removeLast();

    if (!QDir().mkpath(QFileInfo(record_file).absolutePath()))
    {
        error = QStringLiteral("无法创建最近工程记录目录。");
        return false;
    }
    QJsonArray projects;
    for (const QString& path : paths)
        projects.append(path);
    QJsonObject object;
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("projects"), projects);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    QSaveFile file(record_file);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        error = QStringLiteral("无法保存最近工程记录：%1").arg(file.errorString());
        return false;
    }
    return true;
}
} // namespace RecentProjects
