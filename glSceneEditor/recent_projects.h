#ifndef RECENT_PROJECTS_H
#define RECENT_PROJECTS_H

#include <QString>
#include <QStringList>

namespace RecentProjects
{
constexpr int kMaxProjects = 5;
QString RecordFilePath();
QStringList Read(const QString& record_file = RecordFilePath());
bool Remember(const QString& project_path, QString& error,
              const QString& record_file = RecordFilePath());
} // namespace RecentProjects

#endif
