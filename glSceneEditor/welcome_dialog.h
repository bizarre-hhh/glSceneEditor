#ifndef WELCOME_DIALOG_H
#define WELCOME_DIALOG_H

#include <QDialog>
#include <utility>
#include "project_file.h"
#include "recent_projects.h"

class QLabel;
class QListWidget;
class QShowEvent;

// The editor and its OpenGL viewport are created only after this dialog accepts.
class WelcomeDialog : public QDialog
{
public:
    explicit WelcomeDialog(const QString& recent_projects_file = RecentProjects::RecordFilePath());
    const QString& ProjectPath() const { return project_path_; }
    ProjectData TakeInitialProject() { return std::move(initial_project_); }

protected:
    void showEvent(QShowEvent* event) override;

private:
    void OpenProject();
    void OpenProjectPath(const QString& path);
    void RefreshRecentProjects();
    QString project_path_;
    QString recent_projects_file_;
    QListWidget* recent_list_ = nullptr;
    QLabel* recent_empty_label_ = nullptr;
    ProjectData initial_project_;
};

#endif
