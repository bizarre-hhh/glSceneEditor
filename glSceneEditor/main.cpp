#include <QApplication>
#include <QMessageBox>

#include "scene_editor_window.h"
#include "scene_viewport.h"
#include "welcome_dialog.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("glSceneEditor"));
    WelcomeDialog welcome;
    while (welcome.exec() == QDialog::Accepted)
    {
        SceneViewport viewport;
        if (!viewport.Initialize())
        {
            QMessageBox::critical(&welcome, QStringLiteral("无法启动编辑器"),
                                  QStringLiteral("无法初始化 OpenGL 视口，请检查显卡和程序资源。"));
            return 1;
        }
        if (!welcome.ProjectPath().isEmpty())
        {
            QString error;
            if (!viewport.ApplyProject(welcome.TakeInitialProject(), error))
            {
                QMessageBox::warning(&welcome, QStringLiteral("无法打开工程"), error);
                continue;
            }
        }

        int exit_code = 0;
        {
            SceneEditorWindow window(viewport, welcome.ProjectPath());
            if (!window.IsReady())
                return 1;
            window.showMaximized();
            window.StartRendering();
            exit_code = app.exec();
        } // Qt releases its foreign-window wrapper before GLFW is destroyed.
        return exit_code;
    }
    return 0;
}
