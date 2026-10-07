#ifndef SCENE_EDITOR_WINDOW_H
#define SCENE_EDITOR_WINDOW_H

#include <QMainWindow>
#include <QTimer>

class SceneViewport;

// Owns the Qt shell: menus, file dialog, status bar and render timer.
class SceneEditorWindow : public QMainWindow
{
public:
    explicit SceneEditorWindow(SceneViewport& viewport);
    ~SceneEditorWindow() override;

    bool IsReady() const { return ready_; }
    void StartRendering();

private:
    void OpenStl();

    SceneViewport& viewport_;
    QTimer render_timer_;
    bool ready_ = false;
};

#endif
