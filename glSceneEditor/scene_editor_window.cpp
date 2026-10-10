#include "scene_editor_window.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <cmath>

#include "scene_viewport.h"

namespace
{
// Suspend native rendering and input throughout modal project operations.
class RenderPause
{
public:
    RenderPause(QTimer& timer, SceneViewport& viewport)
        : timer_(timer), was_active_(timer.isActive())
    {
        timer_.stop();
        viewport.ReleaseCursor();
    }
    ~RenderPause() { if (was_active_) timer_.start(); }
private:
    QTimer& timer_;
    bool was_active_;
};

QString FormatFileSize(qint64 bytes)
{
    const QLocale locale;
    if (bytes < 1024)
        return QStringLiteral("%1 字节").arg(locale.toString(bytes));

    double size = static_cast<double>(bytes) / 1024.0;
    const char* units[] = {"KB", "MB", "GB", "TB"};
    int unit = 0;
    while (size >= 1024.0 && unit < 3)
    {
        size /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2")
        .arg(locale.toString(size, 'f', 2), QString::fromLatin1(units[unit]));
}
} // namespace

SceneEditorWindow::SceneEditorWindow(SceneViewport& viewport, const QString& project_path,
                                     const QString& recent_projects_file)
    : viewport_(viewport), project_path_(project_path), recent_projects_file_(recent_projects_file)
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
        {
            close();
            return;
        }
        UpdateModelSelection();
        UpdateModelTransformDialog();
        UpdateProjectState();
    });

    QMenu* file_menu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    QAction* new_project_action = file_menu->addAction(QStringLiteral("新建工程(&N)"));
    new_project_action->setObjectName(QStringLiteral("newProjectAction"));
    new_project_action->setShortcut(QKeySequence::New);
    new_project_action->setIcon(QIcon(QStringLiteral(":/icons/file-plus.svg")));
    new_project_action->setIconText(QStringLiteral("新建工程"));
    connect(new_project_action, &QAction::triggered, this, &SceneEditorWindow::NewProject);
    QAction* open_project_action = file_menu->addAction(QStringLiteral("打开工程(&O)…"));
    open_project_action->setObjectName(QStringLiteral("openProjectAction"));
    open_project_action->setShortcut(QKeySequence::Open);
    open_project_action->setIcon(QIcon(QStringLiteral(":/icons/folder-open.svg")));
    open_project_action->setIconText(QStringLiteral("打开工程"));
    connect(open_project_action, &QAction::triggered, this, &SceneEditorWindow::OpenProject);
    QAction* save_project_action = file_menu->addAction(QStringLiteral("保存工程(&S)"));
    save_project_action->setObjectName(QStringLiteral("saveProjectAction"));
    save_project_action->setShortcut(QKeySequence::Save);
    save_project_action->setIcon(QIcon(QStringLiteral(":/icons/save.svg")));
    save_project_action->setIconText(QStringLiteral("保存工程"));
    connect(save_project_action, &QAction::triggered, this, [this]() { SaveProject(); });
    QAction* save_as_action = file_menu->addAction(QStringLiteral("工程另存为(&A)…"));
    save_as_action->setObjectName(QStringLiteral("saveProjectAsAction"));
    save_as_action->setShortcut(QKeySequence::SaveAs);
    save_as_action->setIcon(QIcon(QStringLiteral(":/icons/save-pen.svg")));
    save_as_action->setIconText(QStringLiteral("另存为"));
    connect(save_as_action, &QAction::triggered, this, [this]() { SaveProject(true); });
    file_menu->addSeparator();
    QAction* open_stl_action =
        file_menu->addAction(QStringLiteral("导入 STL 模型(&I)..."));
    open_stl_action->setObjectName(QStringLiteral("openStlAction"));
    open_stl_action->setIcon(QIcon(QStringLiteral(":/icons/package-plus.svg")));
    open_stl_action->setIconText(QStringLiteral("打开 STL"));
    open_stl_action->setToolTip(QStringLiteral("导入 STL 模型，可多选 (Ctrl+I)"));
    open_stl_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(open_stl_action, &QAction::triggered,
            this, &SceneEditorWindow::OpenStl);
    file_menu->addSeparator();
    QAction* quit_action = file_menu->addAction(QStringLiteral("退出(&X)"));
    connect(quit_action, &QAction::triggered, this, [this]() { close(); });

    QMenu* edit_menu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    undo_action_ = edit_menu->addAction(QStringLiteral("撤销"));
    undo_action_->setObjectName(QStringLiteral("undoAction"));
    undo_action_->setIcon(QIcon(QStringLiteral(":/icons/undo-2.svg")));
    undo_action_->setShortcut(QKeySequence::Undo);
    undo_action_->setToolTip(QStringLiteral("撤销上一步模型操作或平台尺寸修改 (Ctrl+Z)"));
    connect(undo_action_, &QAction::triggered, this, &SceneEditorWindow::UndoOperation);
    redo_action_ = edit_menu->addAction(QStringLiteral("回撤"));
    redo_action_->setObjectName(QStringLiteral("redoAction"));
    redo_action_->setIcon(QIcon(QStringLiteral(":/icons/redo-2.svg")));
    redo_action_->setShortcuts({QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Shift+Z"))});
    redo_action_->setToolTip(QStringLiteral("回撤（重做）已撤销的操作 (Ctrl+Y / Ctrl+Shift+Z)"));
    connect(redo_action_, &QAction::triggered, this, &SceneEditorWindow::RedoOperation);
    edit_menu->addSeparator();
    QAction* history_action = edit_menu->addAction(QStringLiteral("操作记录"));
    history_action->setObjectName(QStringLiteral("operationHistoryAction"));
    history_action->setIcon(QIcon(QStringLiteral(":/icons/rotate-ccw-clock.svg")));
    connect(history_action, &QAction::triggered, this, &SceneEditorWindow::ShowOperationHistory);

    QMenu* view_menu = menuBar()->addMenu(QStringLiteral("视图(&V)"));

    QAction* reset_action = view_menu->addAction(QStringLiteral("重置视角"));
    reset_action->setObjectName(QStringLiteral("resetViewAction"));
    reset_action->setIcon(QIcon(QStringLiteral(":/icons/house.svg")));
    reset_action->setToolTip(QStringLiteral("恢复初始视角（视口快捷键 Home）"));
    connect(reset_action, &QAction::triggered,
            this, [this]() { viewport_.ResetView(); });
    view_menu->addSeparator();

    QAction* plate_size_action = view_menu->addAction(QStringLiteral("平台尺寸…"));
    plate_size_action->setObjectName(QStringLiteral("plateSizeAction"));
    plate_size_action->setIcon(QIcon(QStringLiteral(":/icons/ruler.svg")));
    plate_size_action->setIconText(QStringLiteral("平台尺寸"));
    plate_size_action->setToolTip(QStringLiteral("修改基板长度（X）和宽度（Y），单位毫米"));
    connect(plate_size_action, &QAction::triggered, this, &SceneEditorWindow::EditPlateSize);

    QAction* settings_action = view_menu->addAction(QStringLiteral("场景设置"));
    settings_action->setObjectName(QStringLiteral("sceneSettingsAction"));
    settings_action->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
    settings_action->setCheckable(true);
    connect(settings_action, &QAction::toggled, this,
            [this](bool checked) { viewport_.SetSettingsVisible(checked); });

    QToolBar* toolbar = new QToolBar(QStringLiteral("主工具栏"), this);
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setAllowedAreas(Qt::TopToolBarArea);
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolbar->setIconSize(QSize(32, 32));
    toolbar->setStyleSheet(QStringLiteral(
        "QToolBar#mainToolBar QToolButton { min-width: 64px; padding: 6px 8px; }"));
    addToolBar(Qt::TopToolBarArea, toolbar);
    toolbar->addAction(new_project_action);
    toolbar->addAction(open_project_action);
    toolbar->addAction(save_project_action);
    toolbar->addAction(save_as_action);
    toolbar->addSeparator();
    toolbar->addAction(open_stl_action);
    toolbar->addSeparator();
    toolbar->addAction(undo_action_);
    toolbar->addAction(redo_action_);
    toolbar->addAction(history_action);
    toolbar->addSeparator();
    toolbar->addAction(reset_action);
    toolbar->addSeparator();
    toolbar->addAction(plate_size_action);
    toolbar->addAction(settings_action);

    CreateModelDock();
    view_menu->addSeparator();
    view_menu->addAction(model_dock_->toggleViewAction());
    view_menu->addAction(toolbar->toggleViewAction());

    statusBar()->showMessage(QStringLiteral(
        "就绪：仅显示基板 | 右键旋转，中键平移，滚轮缩放，Home 重置"));
    saved_history_revision_ = viewport_.HistoryRevision();
    UpdateProjectState();
    ready_ = true;
    if (!project_path_.isEmpty())
        RememberCurrentProject();
}

SceneEditorWindow::~SceneEditorWindow()
{
    render_timer_.stop();
    viewport_.ReleaseGraphics();
}

void SceneEditorWindow::StartRendering()
{
    viewport_.ShowNativeWindow();
    viewport_.RenderFrame();
    saved_history_revision_ = viewport_.HistoryRevision();
    UpdateProjectState();
    render_timer_.start();
}

void SceneEditorWindow::UpdateProjectState()
{
    const QString name = project_path_.isEmpty() ? QStringLiteral("未命名工程")
                                                : QFileInfo(project_path_).fileName();
    const QString title = QStringLiteral("%1[*] - glSceneEditor").arg(name);
    if (windowTitle() != title)
        setWindowTitle(title);
    setWindowModified(viewport_.HistoryRevision() != saved_history_revision_);
    setWindowFilePath(project_path_);
    UpdateOperationHistory();
    UpdateModelTransformDialog();
}

bool SceneEditorWindow::SaveProject(bool save_as)
{
    RenderPause pause(render_timer_, viewport_);
    UpdateProjectState();
    if (!save_as && !project_path_.isEmpty() && !isWindowModified())
    {
        statusBar()->showMessage(QStringLiteral("没有新的操作记录，无需保存。"));
        return true;
    }
    QString target = project_path_;
    if (save_as || target.isEmpty())
    {
        const QString initial_target = QDir(ProjectFile::DefaultOpenDirectory()).filePath(
            target.isEmpty() ? QStringLiteral("未命名工程.glproj") : QFileInfo(target).fileName());
        target = QFileDialog::getSaveFileName(this, QStringLiteral("保存工程"),
            initial_target, ProjectFile::Filter());
        if (target.isEmpty())
            return false;
        if (!target.endsWith(QStringLiteral(".glproj"), Qt::CaseInsensitive))
        {
            target += QStringLiteral(".glproj");
            // The native dialog checks its entered path, not the appended one.
            if (QFileInfo::exists(target) && QMessageBox::question(this,
                QStringLiteral("覆盖工程"), QStringLiteral("%1 已存在，是否覆盖？")
                    .arg(QDir::toNativeSeparators(target)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return false;
        }
    }
    QString error;
    if (!ProjectFile::Write(target, viewport_.SnapshotProject(), error))
    {
        QMessageBox::warning(this, QStringLiteral("无法保存工程"), error);
        return false;
    }
    project_path_ = QFileInfo(target).absoluteFilePath();
    saved_history_revision_ = viewport_.HistoryRevision();
    UpdateProjectState();
    statusBar()->showMessage(QStringLiteral("已保存工程：%1").arg(QDir::toNativeSeparators(project_path_)));
    return true;
}

bool SceneEditorWindow::ConfirmSaveChanges()
{
    UpdateProjectState();
    if (!isWindowModified())
        return true;
    const auto answer = QMessageBox::warning(this, QStringLiteral("保存工程修改"),
        QStringLiteral("当前工程有未保存的修改，是否先保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return SaveProject();
    return answer == QMessageBox::Discard;
}

void SceneEditorWindow::NewProject()
{
    RenderPause pause(render_timer_, viewport_);
    if (!ConfirmSaveChanges())
        return;
    viewport_.NewProject();
    project_path_.clear();
    saved_history_revision_ = viewport_.HistoryRevision();
    RefreshModelList();
    UpdateProjectState();
    statusBar()->showMessage(QStringLiteral("已新建工程，可以导入 STL 模型。"));
}

void SceneEditorWindow::OpenProject()
{
    RenderPause pause(render_timer_, viewport_);
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开工程"),
        ProjectFile::DefaultOpenDirectory(), ProjectFile::Filter());
    if (path.isEmpty())
        return;
    ProjectData project;
    QString error;
    if (!ProjectFile::Read(path, project, error))
    {
        QMessageBox::warning(this, QStringLiteral("无法打开工程"), error);
        return;
    }
    if (!ConfirmSaveChanges())
        return;
    // Saving in the prompt may have updated the very file being opened.
    if (!ProjectFile::Read(path, project, error) || !viewport_.ApplyProject(project, error))
    {
        QMessageBox::warning(this, QStringLiteral("无法打开工程"), error);
        return;
    }
    project_path_ = QFileInfo(path).absoluteFilePath();
    saved_history_revision_ = viewport_.HistoryRevision();
    RefreshModelList();
    UpdateProjectState();
    statusBar()->showMessage(QStringLiteral("已打开工程：%1 | %2 个模型")
        .arg(QFileInfo(path).fileName()).arg(static_cast<qulonglong>(viewport_.ModelCount())));
    RememberCurrentProject();
}

void SceneEditorWindow::EditPlateSize()
{
    RenderPause pause(render_timer_, viewport_);
    const glm::vec2 current = viewport_.PlateSizeMm();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("plateSizeDialog"));
    dialog.setWindowTitle(QStringLiteral("修改平台尺寸"));
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog.setFont(QFont(QStringLiteral("Microsoft YaHei"), 12));
    dialog.setMinimumWidth(480);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(18);
    auto* hint = new QLabel(QStringLiteral("请输入基板尺寸（毫米）。"), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(16);
    auto* length = new QDoubleSpinBox(&dialog);
    auto* width = new QDoubleSpinBox(&dialog);
    length->setObjectName(QStringLiteral("plateLengthSpinBox"));
    width->setObjectName(QStringLiteral("plateWidthSpinBox"));
    for (auto* field : {length, width})
    {
        field->setRange(PlateDimensions::kMinMm, PlateDimensions::kMaxMm);
        field->setDecimals(2);
        field->setSingleStep(10.0);
        field->setSuffix(QStringLiteral(" mm"));
        field->setMinimumHeight(36);
    }
    length->setValue(current.x);
    width->setValue(current.y);
    form->addRow(QStringLiteral("长度（X 方向）"), length);
    form->addRow(QStringLiteral("宽度（Y 方向）"), width);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString error;
    if (!viewport_.SetPlateSizeMm({static_cast<float>(length->value()), static_cast<float>(width->value())}, error))
    {
        QMessageBox::warning(this, QStringLiteral("无法修改平台尺寸"), error);
        return;
    }
    UpdateProjectState();
    const glm::vec2 size = viewport_.PlateSizeMm();
    statusBar()->showMessage(QStringLiteral("平台尺寸：%1 × %2 mm").arg(size.x, 0, 'f', 2).arg(size.y, 0, 'f', 2));
}

void SceneEditorWindow::RememberCurrentProject()
{
    QString error;
    if (!RecentProjects::Remember(project_path_, error, recent_projects_file_))
        statusBar()->showMessage(QStringLiteral("工程已打开，但最近工程记录未更新：%1").arg(error));
}

void SceneEditorWindow::closeEvent(QCloseEvent* event)
{
    RenderPause pause(render_timer_, viewport_);
    if (!ConfirmSaveChanges())
    {
        event->ignore();
        return;
    }
    event->accept();
    QMainWindow::closeEvent(event);
}

void SceneEditorWindow::UndoOperation()
{
    RenderPause pause(render_timer_, viewport_);
    QString error;
    if (!viewport_.Undo(error))
    {
        if (!error.isEmpty())
            QMessageBox::warning(this, QStringLiteral("无法撤销"), error);
        return;
    }
    UpdateModelSelection(true);
    UpdateProjectState();
    statusBar()->showMessage(viewport_.OperationHistory().back());
}

void SceneEditorWindow::RedoOperation()
{
    RenderPause pause(render_timer_, viewport_);
    QString error;
    if (!viewport_.Redo(error))
    {
        if (!error.isEmpty())
            QMessageBox::warning(this, QStringLiteral("无法回撤"), error);
        return;
    }
    UpdateModelSelection(true);
    UpdateProjectState();
    statusBar()->showMessage(viewport_.OperationHistory().back());
}

void SceneEditorWindow::ShowOperationHistory()
{
    viewport_.ReleaseCursor();
    if (!history_dialog_)
    {
        history_dialog_ = new QDialog(this, Qt::Window);
        history_dialog_->setObjectName(QStringLiteral("operationHistoryDialog"));
        history_dialog_->setWindowTitle(QStringLiteral("操作记录"));
        history_dialog_->resize(760, 460);
        history_dialog_->setMinimumSize(480, 280);
        auto* layout = new QVBoxLayout(history_dialog_);
        layout->addWidget(new QLabel(QStringLiteral("最近的操作显示在最上方"), history_dialog_));
        history_empty_label_ = new QLabel(QStringLiteral("当前工程暂无操作记录。"), history_dialog_);
        history_empty_label_->setObjectName(QStringLiteral("emptyOperationHistory"));
        layout->addWidget(history_empty_label_);
        history_list_ = new QListWidget(history_dialog_);
        history_list_->setObjectName(QStringLiteral("operationHistoryList"));
        history_list_->setWordWrap(true);
        history_list_->setSpacing(4);
        history_list_->setSelectionMode(QAbstractItemView::SingleSelection);
        layout->addWidget(history_list_, 1);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, history_dialog_);
        buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
        connect(buttons, &QDialogButtonBox::rejected, history_dialog_, &QDialog::reject);
        layout->addWidget(buttons);
        // Force the first fill even when the current history revision is zero.
        last_history_revision_ = viewport_.HistoryRevision() - 1;
    }
    UpdateOperationHistory();
    history_dialog_->show();
    history_dialog_->raise();
    history_dialog_->activateWindow();
}

void SceneEditorWindow::UpdateOperationHistory()
{
    undo_action_->setEnabled(viewport_.CanUndo());
    redo_action_->setEnabled(viewport_.CanRedo());
    if (!history_list_ || last_history_revision_ == viewport_.HistoryRevision())
        return;
    last_history_revision_ = viewport_.HistoryRevision();
    const auto& records = viewport_.OperationHistory();
    history_list_->clear();
    for (std::size_t i = records.size(); i > 0; --i)
    {
        auto* item = new QListWidgetItem(QStringLiteral("%1. %2")
            .arg(static_cast<qulonglong>(i)).arg(records[i - 1]), history_list_);
        item->setToolTip(records[i - 1]);
    }
    history_empty_label_->setVisible(records.empty());
}

void SceneEditorWindow::EditModelTransform(TransformMode mode)
{
    viewport_.ReleaseCursor();
    const auto* info = viewport_.CurrentModelInfo();
    if (!info || !info->selected) return;
    if (!viewport_.BeginTransformEditing(info->id, mode)) return;
    if (!transform_dialog_)
    {
        transform_dialog_ = new QDialog(this, Qt::Window);
        transform_dialog_->setObjectName(QStringLiteral("modelTransformDialog"));
        transform_dialog_->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        transform_dialog_->setFont(QFont(QStringLiteral("Microsoft YaHei"), 11));
        transform_dialog_->setMinimumWidth(440);
        auto* layout = new QVBoxLayout(transform_dialog_);
        layout->setContentsMargins(22, 20, 22, 20);
        layout->setSpacing(16);
        auto* hint = new QLabel(QStringLiteral("输入参数后点击应用。\n也可拖动视图中的操纵器，参数会同步更新。"), transform_dialog_);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto* form = new QFormLayout;
        form->setVerticalSpacing(12);
        const QStringList axes{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
        for (int axis = 0; axis < 3; ++axis)
        {
            auto* field = new QDoubleSpinBox(transform_dialog_);
            field->setObjectName(QStringLiteral("modelTransform") + axes[axis]);
            field->setMinimumHeight(34);
            field->setKeyboardTracking(false);
            form->addRow(axes[axis], field);
            transform_fields_[axis] = field;
        }
        layout->addLayout(form);
        auto* note = new QLabel(transform_dialog_);
        note->setObjectName(QStringLiteral("modelTransformNote"));
        note->setWordWrap(true);
        layout->addWidget(note);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, transform_dialog_);
        auto* apply = buttons->button(QDialogButtonBox::Apply);
        apply->setObjectName(QStringLiteral("applyModelTransformButton"));
        apply->setText(QStringLiteral("应用"));
        buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
        connect(apply, &QPushButton::clicked, this, &SceneEditorWindow::ApplyModelTransformDialog);
        connect(buttons, &QDialogButtonBox::rejected, transform_dialog_, &QDialog::reject);
        layout->addWidget(buttons);
        // Closing does not apply values that have only been typed into the fields.
        connect(transform_dialog_, &QDialog::finished, this, [this]()
        {
            viewport_.EndTransformEditing();
            UpdateModelSelection(true);
            UpdateProjectState();
        });
    }
    transform_dialog_model_id_ = info->id;
    transform_dialog_mode_ = mode;
    const QString name = QFileInfo(QString::fromUtf8(info->path.c_str())).fileName();
    const QString operation = mode == TransformMode::Translate ? QStringLiteral("平移") :
        mode == TransformMode::Rotate ? QStringLiteral("旋转") : QStringLiteral("缩放");
    transform_dialog_->setWindowTitle(QStringLiteral("%1模型 — %2").arg(operation, name));
    transform_dialog_->findChild<QLabel*>(QStringLiteral("modelTransformNote"))->setText(
        mode == TransformMode::Translate ? QStringLiteral("位移沿模型局部坐标轴，单位：毫米。") :
        mode == TransformMode::Rotate ? QStringLiteral("绕当前模型中心旋转，单位：度。") :
        QStringLiteral("绕当前模型中心缩放；1 为原始大小，数值必须大于 0。"));
    transform_dialog_->show();
    UpdateModelTransformDialog(true);
    UpdateModelSelection(true);
    transform_dialog_->raise();
    transform_dialog_->activateWindow();
}

void SceneEditorWindow::UpdateModelTransformDialog(bool force)
{
    if (!transform_dialog_ || !transform_dialog_->isVisible()) return;
    const auto* info = viewport_.CurrentModelInfo();
    if (!viewport_.IsTransformEditing() || !info || !info->selected || info->id != transform_dialog_model_id_)
    {
        transform_dialog_->reject();
        return;
    }
    if (!force && info->transform_revision == transform_dialog_revision_) return;
    transform_dialog_revision_ = info->transform_revision;
    const auto values = transform_dialog_mode_ == TransformMode::Translate ? info->local_translation_mm :
        transform_dialog_mode_ == TransformMode::Rotate ? info->local_rotation_degrees : info->local_scale;
    for (int axis = 0; axis < 3; ++axis)
    {
        auto* field = transform_fields_[axis];
        const QSignalBlocker blocker(field);
        field->setDecimals(transform_dialog_mode_ == TransformMode::Scale ? 4 : 3);
        const double low = transform_dialog_mode_ == TransformMode::Scale ? 0.0001 : -1000000.0;
        const double high = transform_dialog_mode_ == TransformMode::Scale ? 1000.0 : 1000000.0;
        field->setRange(std::min(low, static_cast<double>(values[axis])), std::max(high, static_cast<double>(values[axis])));
        field->setSingleStep(transform_dialog_mode_ == TransformMode::Scale ? 0.1 : 1.0);
        field->setSuffix(transform_dialog_mode_ == TransformMode::Translate ? QStringLiteral(" mm") :
            transform_dialog_mode_ == TransformMode::Rotate ? QStringLiteral(" °") : QStringLiteral(" 倍"));
        field->setValue(values[axis]);
    }
}

void SceneEditorWindow::ApplyModelTransformDialog()
{
    const auto* info = viewport_.CurrentModelInfo();
    if (!viewport_.IsTransformEditing() || !info || !info->selected || info->id != transform_dialog_model_id_)
    {
        transform_dialog_->reject();
        return;
    }
    viewport_.ReleaseCursor();
    ModelTransform transform{info->local_translation_mm, info->local_scale, info->local_rotation_degrees};
    glm::vec3 values;
    for (int axis = 0; axis < 3; ++axis)
    {
        transform_fields_[axis]->interpretText();
        values[axis] = static_cast<float>(transform_fields_[axis]->value());
    }
    const glm::vec3 previous = transform_dialog_mode_ == TransformMode::Translate ? transform.translation_mm :
        transform_dialog_mode_ == TransformMode::Rotate ? transform.rotation_degrees : transform.scale;
    // Display rounding must not turn an untouched field into an edit.
    for (int axis = 0; axis < 3; ++axis)
    {
        const int decimals = transform_fields_[axis]->decimals();
        const double factor = std::pow(10.0, decimals);
        if (std::abs(values[axis] - std::round(previous[axis] * factor) / factor) < 0.5 / factor)
            values[axis] = previous[axis];
    }
    if (transform_dialog_mode_ == TransformMode::Translate) transform.translation_mm = values;
    else if (transform_dialog_mode_ == TransformMode::Rotate && values != previous)
        transform = ModelTransformMath::RotateKeepingCenter(transform, values);
    else if (transform_dialog_mode_ == TransformMode::Scale) transform.scale = values;
    if (!viewport_.TransformModelTo(info->id, transform, transform_dialog_mode_))
        QMessageBox::warning(transform_dialog_, QStringLiteral("无法修改模型"), QStringLiteral("参数超出支持范围，请输入有限的位移和角度，以及大于 0 的缩放倍数。"));
    UpdateModelTransformDetails();
    UpdateModelTransformDialog(true);
    UpdateProjectState();
}

void SceneEditorWindow::CreateModelDock()
{
    model_dock_ = new QDockWidget(QStringLiteral("STL 模型"), this);
    model_dock_->setObjectName(QStringLiteral("stlModelDock"));
    model_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    // Keep the dock in the main window beside the embedded native viewport.
    model_dock_->setFeatures(QDockWidget::DockWidgetClosable |
                             QDockWidget::DockWidgetMovable);
    model_dock_->setMinimumWidth(480);

    QWidget* contents = new QWidget(model_dock_);
    QVBoxLayout* layout = new QVBoxLayout(contents);
    model_count_label_ = new QLabel(contents);
    layout->addWidget(model_count_label_);

    empty_models_label_ = new QLabel(
        QStringLiteral("暂无已加载的 STL 模型。\n点击工具栏的“打开 STL”加载模型。"), contents);
    empty_models_label_->setWordWrap(true);
    layout->addWidget(empty_models_label_);

    model_list_ = new QListWidget(contents);
    model_list_->setObjectName(QStringLiteral("stlModelList"));
    model_list_->setMinimumHeight(100);
    model_list_->setSelectionMode(QAbstractItemView::NoSelection);
    model_list_->setFocusPolicy(Qt::NoFocus);

    model_details_ = new QGroupBox(QStringLiteral("基本信息"), contents);
    QFormLayout* details_layout = new QFormLayout(model_details_);
    details_layout->setContentsMargins(10, 10, 10, 10);
    details_layout->setVerticalSpacing(4);
    details_layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    details_layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto add_detail = [this, details_layout](const QString& title,
                                           const QString& object_name, bool add_row = true)
    {
        QLabel* label = new QLabel(model_details_);
        label->setObjectName(object_name);
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        label->setWordWrap(true);
        if (add_row)
            details_layout->addRow(title, label);
        return label;
    };
    model_name_label_ = add_detail(QStringLiteral("文件名"), QStringLiteral("modelName"));
    model_path_edit_ = new QPlainTextEdit(model_details_);
    model_path_edit_->setObjectName(QStringLiteral("modelPath"));
    model_path_edit_->setReadOnly(true);
    model_path_edit_->setTabChangesFocus(true);
    model_path_edit_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    model_path_edit_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    model_path_edit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    model_path_edit_->setFixedHeight(model_path_edit_->fontMetrics().lineSpacing() * 2 + 8);
    details_layout->addRow(QStringLiteral("路径"), model_path_edit_);
    model_file_size_label_ = add_detail(QStringLiteral("文件大小"), QStringLiteral("modelFileSize"));
    model_triangles_label_ = add_detail(QStringLiteral("三角面数"), QStringLiteral("modelTriangles"));
    model_size_x_label_ = add_detail(QString(), QStringLiteral("modelSizeX"), false);
    model_size_y_label_ = add_detail(QString(), QStringLiteral("modelSizeY"), false);
    model_size_z_label_ = add_detail(QString(), QStringLiteral("modelSizeZ"), false);
    const QString axes[] = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    QWidget* dimensions = new QWidget(model_details_);
    QHBoxLayout* dimension_layout = new QHBoxLayout(dimensions);
    dimension_layout->setContentsMargins(0, 0, 0, 0);
    const std::array<QLabel*, 3> dimension_labels{
        model_size_x_label_, model_size_y_label_, model_size_z_label_};
    for (int axis = 0; axis < 3; ++axis)
    {
        dimension_layout->addWidget(new QLabel(axes[axis], dimensions));
        dimension_layout->addWidget(dimension_labels[axis], 1);
    }
    dimensions->setToolTip(QStringLiteral("当前世界坐标包围盒的 X/Y/Z 尺寸，随模型变换更新"));
    details_layout->addRow(QStringLiteral("尺寸"), dimensions);

    QWidget* spatial_details = new QWidget(model_details_);
    QHBoxLayout* spatial_layout = new QHBoxLayout(spatial_details);
    spatial_layout->setContentsMargins(0, 0, 0, 0);
    auto create_vector_group = [spatial_details, spatial_layout, &axes](
        const QString& title, const QString& object_name)
    {
        QGroupBox* group = new QGroupBox(title, spatial_details);
        group->setObjectName(object_name);
        QGridLayout* grid = new QGridLayout(group);
        grid->setVerticalSpacing(4);
        for (int axis = 0; axis < 3; ++axis)
        {
            QLabel* header = new QLabel(axes[axis], group);
            header->setAlignment(Qt::AlignCenter);
            grid->addWidget(header, 0, axis + 1);
            grid->setColumnStretch(axis + 1, 1);
            grid->setColumnMinimumWidth(axis + 1,
                header->fontMetrics().horizontalAdvance(QStringLiteral("-000.00")) + 4);
        }
        spatial_layout->addWidget(group, 1);
        return group;
    };
    QGroupBox* position_group = create_vector_group(
        QStringLiteral("包围盒位置 (mm)"), QStringLiteral("modelPositionGroup"));
    QGroupBox* transform_group = create_vector_group(
        QStringLiteral("自身变换"), QStringLiteral("modelTransformGroup"));
    auto add_vector = [&axes](QGroupBox* group, const QString& title,
                              const QString& prefix, std::array<QLabel*, 3>& labels)
    {
        auto* grid = static_cast<QGridLayout*>(group->layout());
        const int row = grid->rowCount();
        grid->addWidget(new QLabel(title, group), row, 0);
        for (int axis = 0; axis < 3; ++axis)
        {
            QLabel* label = new QLabel(group);
            label->setObjectName(prefix + axes[axis]);
            label->setTextFormat(Qt::PlainText);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            label->setWordWrap(true);
            grid->addWidget(label, row, axis + 1);
            labels[axis] = label;
        }
    };
    add_vector(position_group, QStringLiteral("中心"), QStringLiteral("modelPosition"),
               model_position_labels_);
    add_vector(position_group, QStringLiteral("最小"), QStringLiteral("modelBoundsMin"),
               model_bounds_min_labels_);
    add_vector(position_group, QStringLiteral("最大"), QStringLiteral("modelBoundsMax"),
               model_bounds_max_labels_);
    add_vector(transform_group, QStringLiteral("位移 mm"), QStringLiteral("modelTranslation"),
               model_translation_labels_);
    add_vector(transform_group, QStringLiteral("缩放 倍"), QStringLiteral("modelScale"),
               model_scale_labels_);
    add_vector(transform_group, QStringLiteral("旋转 °"), QStringLiteral("modelRotation"),
               model_rotation_labels_);
    auto add_transform_button = [this, transform_group](int row, const QString& text,
                                                       const QString& name, TransformMode mode)
    {
        auto* button = new QPushButton(text, transform_group);
        button->setObjectName(name);
        button->setMinimumHeight(28);
        button->setToolTip(QStringLiteral("修改参数并启用对应的视图操纵器"));
        auto* grid = static_cast<QGridLayout*>(transform_group->layout());
        QWidget* old_label = grid->itemAtPosition(row, 0)->widget();
        grid->removeWidget(old_label);
        delete old_label;
        grid->addWidget(button, row, 0);
        connect(button, &QPushButton::clicked, this, [this, mode]() { EditModelTransform(mode); });
    };
    add_transform_button(1, QStringLiteral("平移 mm"), QStringLiteral("modelTranslateButton"), TransformMode::Translate);
    add_transform_button(2, QStringLiteral("缩放 倍"), QStringLiteral("modelScaleButton"), TransformMode::Scale);
    add_transform_button(3, QStringLiteral("旋转 °"), QStringLiteral("modelRotateButton"), TransformMode::Rotate);
    auto add_note = [](QGroupBox* group, const QString& text)
    {
        auto* grid = static_cast<QGridLayout*>(group->layout());
        QLabel* note = new QLabel(text, group);
        note->setWordWrap(true);
        note->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        grid->addWidget(note, grid->rowCount(), 0, 1, 4);
    };
    add_note(position_group, QStringLiteral("场景世界坐标"));
    add_note(transform_group, QStringLiteral("相对初始姿态；视图中可拖动操纵器"));
    details_layout->addRow(spatial_details);

    QScrollArea* details_scroll = new QScrollArea(contents);
    details_scroll->setObjectName(QStringLiteral("modelDetailsScroll"));
    details_scroll->setWidgetResizable(true);
    details_scroll->setFrameShape(QFrame::NoFrame);
    details_scroll->setMinimumHeight(180);
    details_scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    details_scroll->setWidget(model_details_);
    QSplitter* splitter = new QSplitter(Qt::Vertical, contents);
    splitter->setObjectName(QStringLiteral("modelDockSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(6);
    splitter->addWidget(model_list_);
    splitter->addWidget(details_scroll);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({170, 480});
    layout->addWidget(splitter, 1);

    model_dock_->setWidget(contents);
    addDockWidget(Qt::LeftDockWidgetArea, model_dock_);
    resizeDocks({model_dock_}, {920}, Qt::Horizontal);
    RefreshModelList();
}

void SceneEditorWindow::RefreshModelList()
{
    {
        const QSignalBlocker blocker(model_list_);
        model_items_.clear();
        model_list_->clear();
        for (const auto& info : viewport_.ModelInfos())
        {
            const auto id = info.id;
            const QString path = QString::fromUtf8(info.path.c_str());
            QListWidgetItem* item = new QListWidgetItem(
                style()->standardIcon(QStyle::SP_FileIcon),
                QFileInfo(path).fileName(), model_list_);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id));
            item->setToolTip(QDir::toNativeSeparators(path));
            model_items_.emplace(id, item);

            QWidget* row = new QWidget(model_list_);
            row->setAutoFillBackground(true);
            row->setBackgroundRole(QPalette::Window);
            QHBoxLayout* row_layout = new QHBoxLayout(row);
            row_layout->setContentsMargins(4, 2, 4, 2);
            row_layout->setSpacing(4);
            QLabel* name_label = new QLabel(QFileInfo(path).fileName(), row);
            name_label->setObjectName(QStringLiteral("modelRowName"));
            name_label->setTextFormat(Qt::PlainText);
            name_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            name_label->setToolTip(item->toolTip());
            row_layout->addWidget(name_label, 1);

            QToolButton* selection_button = new QToolButton(row);
            selection_button->setObjectName(QStringLiteral("selectModelButton"));
            selection_button->setText(QStringLiteral("取消选中"));
            selection_button->setMinimumWidth(selection_button->sizeHint().width());
            selection_button->setCheckable(true);
            selection_button->setFocusPolicy(Qt::NoFocus);
            row_layout->addWidget(selection_button);
            connect(selection_button, &QToolButton::clicked, this, [this, id]()
            {
                const auto* current = viewport_.ModelInfo(id);
                if (!current)
                    return;
                viewport_.SetModelSelected(id, !current->selected);
                UpdateModelSelection(true);
            });

            QToolButton* visibility_button = new QToolButton(row);
            visibility_button->setObjectName(QStringLiteral("toggleModelVisibilityButton"));
            visibility_button->setFocusPolicy(Qt::NoFocus);
            row_layout->addWidget(visibility_button);
            connect(visibility_button, &QToolButton::clicked, this, [this, id, name_label]()
            {
                const auto* current = viewport_.ModelInfo(id);
                if (!current)
                    return;
                const bool visible = !current->visible;
                viewport_.SetModelVisible(id, visible);
                UpdateModelSelection(true);
                statusBar()->showMessage(
                    (visible ? QStringLiteral("已显示 %1") : QStringLiteral("已隐藏 %1"))
                        .arg(name_label->text()));
            });

            QToolButton* bounds_button = new QToolButton(row);
            bounds_button->setObjectName(QStringLiteral("toggleModelBoundingBoxButton"));
            bounds_button->setText(QStringLiteral("显示包围盒"));
            bounds_button->setMinimumWidth(bounds_button->sizeHint().width());
            bounds_button->setCheckable(true);
            bounds_button->setFocusPolicy(Qt::NoFocus);
            row_layout->addWidget(bounds_button);
            connect(bounds_button, &QToolButton::clicked, this, [this, id, name_label]()
            {
                const auto* current = viewport_.ModelInfo(id);
                if (!current)
                    return;
                const bool visible = !current->bounding_box_visible;
                viewport_.SetModelBoundingBoxVisible(id, visible);
                UpdateModelSelection(true);
                statusBar()->showMessage(
                    (visible ? QStringLiteral("已显示 %1 的包围盒") : QStringLiteral("已隐藏 %1 的包围盒"))
                        .arg(name_label->text()));
            });

            QToolButton* delete_button = new QToolButton(row);
            delete_button->setObjectName(QStringLiteral("deleteModelButton"));
            delete_button->setText(QStringLiteral("删除"));
            delete_button->setToolTip(QStringLiteral("从场景中删除模型"));
            delete_button->setFocusPolicy(Qt::NoFocus);
            row_layout->addWidget(delete_button);
            connect(delete_button, &QToolButton::clicked, this, [this, id]()
            {
                const auto* current = viewport_.ModelInfo(id);
                if (!current)
                    return;
                const QString name = QFileInfo(
                    QString::fromUtf8(current->path.c_str())).fileName();
                viewport_.RemoveStl(id);
                RefreshModelList();
                UpdateProjectState();
                statusBar()->showMessage(QStringLiteral("已删除 %1 | 剩余 %2 个模型")
                    .arg(name).arg(static_cast<qulonglong>(viewport_.ModelCount())));
            });

            item->setSizeHint(row->sizeHint());
            model_list_->setItemWidget(item, row);
        }
    }
    model_count_label_->setText(
        QStringLiteral("已加载模型（%1）").arg(model_list_->count()));
    empty_models_label_->setVisible(model_list_->count() == 0);
    UpdateModelSelection(true);
}

void SceneEditorWindow::UpdateModelSelection(bool force)
{
    const auto* current = viewport_.CurrentModelInfo();
    const auto details_id = current && current->selected ? current->id : 0;
    const auto transform_revision = current && current->selected ? current->transform_revision : 0;
    const auto state_revision = viewport_.ModelStateRevision();
    if (!force && state_revision == last_model_state_revision_)
    {
        if (details_id && transform_revision != last_transform_revision_)
            UpdateModelTransformDetails();
        last_transform_revision_ = transform_revision;
        return;
    }

    const auto infos = viewport_.ModelInfos();
    if (infos.size() != model_items_.size() ||
        std::any_of(infos.begin(), infos.end(), [this](const auto& info)
        { return model_items_.find(info.id) == model_items_.end(); }))
    {
        RefreshModelList();
        return;
    }
    const QSignalBlocker blocker(model_list_);
    for (const auto& info : infos)
    {
        QListWidgetItem* item = model_items_.at(info.id);
        if (info.id == details_id)
            model_list_->scrollToItem(item);
        QWidget* row = model_list_->itemWidget(item);
        auto* selection = row->findChild<QToolButton*>(QStringLiteral("selectModelButton"));
        selection->setText(info.selected ? QStringLiteral("取消选中") : QStringLiteral("选中"));
        selection->setEnabled(!viewport_.IsTransformEditing());
        selection->setToolTip(viewport_.IsTransformEditing() ? QStringLiteral("关闭变换参数窗口后可修改选中状态") :
            info.selected ? QStringLiteral("取消选中该模型") : QStringLiteral("选中该模型"));
        selection->setChecked(info.selected);
        auto* visibility = row->findChild<QToolButton*>(QStringLiteral("toggleModelVisibilityButton"));
        visibility->setText(info.visible ? QStringLiteral("隐藏") : QStringLiteral("显示"));
        visibility->setToolTip(info.visible ? QStringLiteral("隐藏模型") : QStringLiteral("显示模型"));
        auto* bounds = row->findChild<QToolButton*>(QStringLiteral("toggleModelBoundingBoxButton"));
        bounds->setText(info.bounding_box_visible ? QStringLiteral("隐藏包围盒") : QStringLiteral("显示包围盒"));
        bounds->setToolTip(info.bounding_box_visible ? QStringLiteral("隐藏模型包围盒") : QStringLiteral("显示模型包围盒"));
        bounds->setChecked(info.bounding_box_visible);
        row->findChild<QLabel*>(QStringLiteral("modelRowName"))->setEnabled(info.visible);
        QPalette palette = row->palette();
        palette.setColor(QPalette::Window, info.selected ? QColor(214, 234, 255)
            : model_list_->palette().color(QPalette::Base));
        row->setPalette(palette);
    }
    if (force || details_id != last_details_model_id_)
        UpdateModelDetails();
    else if (details_id && transform_revision != last_transform_revision_)
        UpdateModelTransformDetails();
    if (details_id != last_details_model_id_)
        statusBar()->showMessage(details_id ? QStringLiteral("已选中 %1").arg(
            model_items_.at(details_id)->text()) : QStringLiteral("已取消模型选择"));
    last_model_state_revision_ = state_revision;
    last_details_model_id_ = details_id;
    last_transform_revision_ = transform_revision;
}

void SceneEditorWindow::UpdateModelDetails()
{
    const auto& info = viewport_.CurrentModelInfo();
    const bool has_selection = info && info->selected;
    model_details_->setEnabled(has_selection);
    if (!has_selection)
    {
        for (QLabel* label : {model_name_label_,
                             model_file_size_label_, model_triangles_label_,
                             model_size_x_label_, model_size_y_label_, model_size_z_label_})
        {
            label->setText(QStringLiteral("—"));
            label->setToolTip(QString());
        }
        for (const auto& labels : {model_position_labels_, model_bounds_min_labels_,
                                   model_bounds_max_labels_, model_translation_labels_,
                                   model_scale_labels_, model_rotation_labels_})
            for (QLabel* label : labels)
                label->setText(QStringLiteral("—"));
        model_path_edit_->setPlainText(QStringLiteral("—"));
        model_path_edit_->setToolTip(QString());
        UpdateModelTransformDialog();
        return;
    }

    const QString path = QString::fromUtf8(info->path.c_str());
    const QFileInfo file(path);
    model_name_label_->setText(file.fileName());
    model_name_label_->setToolTip(file.fileName());
    model_path_edit_->setPlainText(QDir::toNativeSeparators(file.absoluteFilePath()));
    model_path_edit_->setToolTip(model_path_edit_->toPlainText());
    model_file_size_label_->setText(FormatFileSize(static_cast<qint64>(info->source_file_size)));
    model_triangles_label_->setText(
        QLocale().toString(static_cast<qulonglong>(info->triangle_count)));
    UpdateModelTransformDetails();
}

void SceneEditorWindow::UpdateModelTransformDetails()
{
    const auto& info = viewport_.CurrentModelInfo();
    if (!info || !info->selected)
        return;
    // Dimensions and bounds use the same current geometry, even while the
    // bounds wireframe is hidden. Original STL dimensions are only for placement.
    const glm::vec3 dimensions = info->world_bounds_max_mm - info->world_bounds_min_mm;
    const std::array<QLabel*, 3> size_labels{model_size_x_label_, model_size_y_label_, model_size_z_label_};
    for (int axis = 0; axis < 3; ++axis)
        size_labels[axis]->setText(QStringLiteral("%1 mm").arg(QLocale().toString(dimensions[axis], 'f', 2)));
    auto update_vector = [](const std::array<QLabel*, 3>& labels, const glm::vec3& values)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            const QString text = QLocale().toString(values[axis], 'f', 2);
            if (labels[axis]->text() != text)
                labels[axis]->setText(text);
        }
    };
    update_vector(model_position_labels_, info->world_position_mm);
    update_vector(model_bounds_min_labels_, info->world_bounds_min_mm);
    update_vector(model_bounds_max_labels_, info->world_bounds_max_mm);
    update_vector(model_translation_labels_, info->local_translation_mm);
    update_vector(model_scale_labels_, info->local_scale);
    update_vector(model_rotation_labels_, info->local_rotation_degrees);
}

void SceneEditorWindow::OpenStl()
{
    render_timer_.stop();
    viewport_.ReleaseCursor();
    const QString initial_directory =
        QStringLiteral("D:/vs_cmake_proj/glSceneEditor/asset");
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("打开 STL 模型（可多选）"), initial_directory,
        QStringLiteral("STL 模型 (*.stl)"));

    int loaded_count = 0;
    QStringList failures;
    for (const QString& path : paths)
    {
        std::string error;
        glm::vec3 size_mm(0.0f);
        const QByteArray local_path = QDir::fromNativeSeparators(path).toUtf8();
        if (viewport_.LoadStl(local_path.constData(), error, size_mm))
            ++loaded_count;
        else
            failures.append(QStringLiteral("%1：%2").arg(
                QFileInfo(path).fileName(), QString::fromLocal8Bit(error.c_str())));
    }
    if (loaded_count > 0)
    {
        RefreshModelList();
        UpdateProjectState();
        statusBar()->showMessage(QStringLiteral("已添加 %1 个模型 | 当前共 %2 个模型")
            .arg(loaded_count).arg(static_cast<qulonglong>(viewport_.ModelCount())));
    }
    if (!failures.isEmpty())
        QMessageBox::warning(this, QStringLiteral("部分 STL 无法打开"), failures.join(QStringLiteral("\n")));
    render_timer_.start();
}
