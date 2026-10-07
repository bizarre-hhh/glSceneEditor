#include "scene_editor_window.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QWidget>
#include <QWindow>

#include "scene_viewport.h"

SceneEditorWindow::SceneEditorWindow(SceneViewport& viewport)
    : viewport_(viewport)
{
    setWindowTitle(QStringLiteral("glSceneEditor"));
    resize(SceneViewport::kInitialWidth, SceneViewport::kInitialHeight);

    QWindow* glfw_window = QWindow::fromWinId(
        reinterpret_cast<WId>(viewport_.NativeHandle()));
    if (glfw_window == nullptr)
        return;

    QWidget* scene_area = QWidget::createWindowContainer(glfw_window, this);
    scene_area->setFocusPolicy(Qt::StrongFocus);
    setCentralWidget(scene_area);

    render_timer_.setTimerType(Qt::PreciseTimer);
    render_timer_.setInterval(8);
    connect(&render_timer_, &QTimer::timeout, this, [this]()
    {
        if (!viewport_.RenderFrame())
            QApplication::quit();
    });

    QMenu* file_menu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    QAction* open_stl_action =
        file_menu->addAction(QStringLiteral("打开 STL 模型(&O)..."));
    open_stl_action->setShortcut(QKeySequence::Open);
    connect(open_stl_action, &QAction::triggered,
            this, &SceneEditorWindow::OpenStl);
    file_menu->addSeparator();
    QAction* quit_action = file_menu->addAction(QStringLiteral("退出(&X)"));
    connect(quit_action, &QAction::triggered, qApp, &QApplication::quit);

    QMenu* view_menu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    QAction* settings_action = view_menu->addAction(QStringLiteral("场景设置"));
    settings_action->setCheckable(true);
    connect(settings_action, &QAction::toggled, this,
            [this](bool checked) { viewport_.SetSettingsVisible(checked); });

    statusBar()->showMessage(QStringLiteral(
        "就绪：仅显示基板 | 右键旋转，中键平移，滚轮缩放，F 适配，Home 重置"));
    ready_ = true;
}

SceneEditorWindow::~SceneEditorWindow()
{
    render_timer_.stop();
    viewport_.ReleaseGraphics();
}

void SceneEditorWindow::StartRendering()
{
    viewport_.ShowNativeWindow();
    render_timer_.start();
}

void SceneEditorWindow::OpenStl()
{
    render_timer_.stop();
    viewport_.ReleaseCursor();
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开 STL 模型"),
        QStringLiteral("D:/vs_proj/glSceneEditor/asset"),
        QStringLiteral("STL 模型 (*.stl)"));

    if (!path.isEmpty())
    {
        std::string error;
        glm::vec3 size_mm(0.0f);
        const QByteArray local_path = QDir::fromNativeSeparators(path).toLocal8Bit();
        if (viewport_.LoadStl(local_path.constData(), error, size_mm))
        {
            statusBar()->showMessage(
                QStringLiteral("已加载 %1 | 尺寸 %2 × %3 × %4 mm")
                    .arg(QFileInfo(path).fileName())
                    .arg(size_mm.x, 0, 'f', 1)
                    .arg(size_mm.y, 0, 'f', 1)
                    .arg(size_mm.z, 0, 'f', 1));
        }
        else
        {
            QMessageBox::warning(this, QStringLiteral("无法打开 STL"),
                                 QString::fromLocal8Bit(error.c_str()));
        }
    }
    render_timer_.start();
}
