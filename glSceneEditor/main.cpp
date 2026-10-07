#include <QApplication>

#include "scene_editor_window.h"
#include "scene_viewport.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    SceneViewport viewport;
    if (!viewport.Initialize())
        return 1;

    int exit_code = 0;
    {
        SceneEditorWindow window(viewport);
        if (!window.IsReady())
            return 1;

        window.showMaximized();
        window.StartRendering();
        exit_code = app.exec();
    } // Qt releases its foreign-window wrapper before GLFW is destroyed.
    return exit_code;
}
