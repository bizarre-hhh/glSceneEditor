#include "scene_editor_window.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QColor>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
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
#include <QLineEdit>
#include <QMouseEvent>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <limits>

#include "scene_viewport.h"
#include "model_names.h"

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
    duplicate_action_ = edit_menu->addAction(QStringLiteral("复制模型"));
    duplicate_action_->setObjectName(QStringLiteral("duplicateModelsAction"));
    duplicate_action_->setIcon(QIcon(QStringLiteral(":/icons/duplicate-model.svg")));
    duplicate_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    duplicate_action_->setToolTip(QStringLiteral("复制选中模型，沿 X 方向保留 5 mm 间隙 (Ctrl+D)"));
    duplicate_action_->setEnabled(false);
    connect(duplicate_action_, &QAction::triggered, this, &SceneEditorWindow::DuplicateModels);
    array_action_ = edit_menu->addAction(QStringLiteral("矩形阵列…"));
    array_action_->setObjectName(QStringLiteral("modelArrayAction"));
    array_action_->setIcon(QIcon(QStringLiteral(":/icons/model-array.svg")));
    array_action_->setIconText(QStringLiteral("矩形阵列"));
    array_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    array_action_->setToolTip(QStringLiteral("按行列及 X/Y 间隙重复排列选中模型 (Ctrl+Shift+D)"));
    array_action_->setEnabled(false);
    connect(array_action_, &QAction::triggered, this, &SceneEditorWindow::ArrayModels);
    place_on_plate_action_ = edit_menu->addAction(QStringLiteral("贴合基板"));
    place_on_plate_action_->setObjectName(QStringLiteral("placeOnPlateAction"));
    place_on_plate_action_->setIcon(QIcon(QStringLiteral(":/icons/drop-to-plate.svg")));
    place_on_plate_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    place_on_plate_action_->setToolTip(QStringLiteral("使选中模型的最低 Z 为 0，保持 X/Y 位置不变 (Ctrl+L)"));
    place_on_plate_action_->setEnabled(false);
    connect(place_on_plate_action_, &QAction::triggered, this, &SceneEditorWindow::PlaceModelsOnPlate);
    edit_menu->addSeparator();
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

    QMenu* slice_menu = menuBar()->addMenu(QStringLiteral("切片(&S)"));
    slice_action_ = slice_menu->addAction(QStringLiteral("生成切片…"));
    slice_action_->setObjectName(QStringLiteral("generateSliceAction"));
    slice_action_->setIcon(QIcon(QStringLiteral(":/icons/slice.svg")));
    slice_action_->setIconText(QStringLiteral("切片"));
    slice_action_->setToolTip(QStringLiteral("按层厚切片所有模型并预览轮廓"));
    slice_action_->setEnabled(false);
    connect(slice_action_, &QAction::triggered, this, &SceneEditorWindow::GenerateSlice);
    clear_slice_action_ = slice_menu->addAction(QStringLiteral("清除切片预览"));
    clear_slice_action_->setObjectName(QStringLiteral("clearSliceAction"));
    clear_slice_action_->setEnabled(false);
    connect(clear_slice_action_, &QAction::triggered,
            this, &SceneEditorWindow::ClearSlicePreview);

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
    plate_size_action->setToolTip(QStringLiteral("修改平台长度（X）、宽度（Y）和高度（Z），单位毫米"));
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
    toolbar->addAction(duplicate_action_);
    toolbar->addAction(array_action_);
    toolbar->addAction(place_on_plate_action_);
    toolbar->addSeparator();
    toolbar->addAction(undo_action_);
    toolbar->addAction(redo_action_);
    toolbar->addAction(history_action);
    toolbar->addSeparator();
    toolbar->addAction(reset_action);
    toolbar->addSeparator();
    toolbar->addAction(plate_size_action);
    toolbar->addAction(slice_action_);
    toolbar->addAction(settings_action);

    CreateModelDock();
    CreateSliceDock();
    view_menu->addSeparator();
    view_menu->addAction(model_dock_->toggleViewAction());
    view_menu->addAction(slice_dock_->toggleViewAction());
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
    UpdateSlicePanel();
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
    // Confirm before the native file dialog: showing a Qt message box directly
    // inside the dialog's closing callback can leave it behind the disabled
    // editor and make the application appear frozen.
    if (!ConfirmSaveChanges())
        return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开工程"),
        ProjectFile::DefaultOpenDirectory(), ProjectFile::Filter());
    if (path.isEmpty())
        return;
    // Let the native dialog finish unwinding before the synchronous project
    // load; otherwise the editor can look unresponsive while it is reading.
    statusBar()->showMessage(QStringLiteral("正在打开工程…"));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    ProjectData project;
    QString error;
    // Read after the prompt so reopening a project uses content saved there.
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

void SceneEditorWindow::DuplicateModels()
{
    RenderPause pause(render_timer_, viewport_);
    QString error;
    if (!viewport_.DuplicateSelectedModels(error))
    {
        QMessageBox::warning(this, QStringLiteral("无法复制模型"), error);
        return;
    }
    UpdateModelSelection(true);
    UpdateProjectState();
    statusBar()->showMessage(viewport_.OperationHistory().back());
}

void SceneEditorWindow::PlaceModelsOnPlate()
{
    RenderPause pause(render_timer_, viewport_);
    const auto history = viewport_.HistoryRevision();
    QString error;
    if (!viewport_.PlaceSelectedModelsOnPlate(error))
    {
        QMessageBox::warning(this, QStringLiteral("无法贴合基板"), error);
        return;
    }
    UpdateModelSelection(true);
    UpdateProjectState();
    statusBar()->showMessage(viewport_.HistoryRevision() == history
        ? QStringLiteral("选中模型已贴合基板，无需移动。") : viewport_.OperationHistory().back());
}

void SceneEditorWindow::ArrayModels()
{
    RenderPause pause(render_timer_, viewport_);
    if (viewport_.IsTransformEditing()) return;
    std::size_t selected_count = 0;
    glm::vec2 minimum(std::numeric_limits<float>::max());
    glm::vec2 maximum(std::numeric_limits<float>::lowest());
    for (const auto& info : viewport_.ModelInfos())
        if (info.selected)
        {
            ++selected_count;
            minimum = glm::min(minimum, glm::vec2(info.world_bounds_min_mm));
            maximum = glm::max(maximum, glm::vec2(info.world_bounds_max_mm));
        }
    if (selected_count == 0) return;
    const glm::vec2 group_size = maximum - minimum;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("modelArrayDialog"));
    dialog.setWindowTitle(QStringLiteral("矩形阵列"));
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog.setFont(QFont(QStringLiteral("Microsoft YaHei"), 12));
    dialog.setMinimumWidth(640);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(18);
    auto* hint = new QLabel(QStringLiteral(
        "行列数包含原模型，沿世界 X/Y 正方向展开。\n"
        "多选模型作为一个单元重复排列，间隙按包围盒计算。"), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(16);
    auto* columns = new QSpinBox(&dialog);
    auto* rows = new QSpinBox(&dialog);
    columns->setObjectName(QStringLiteral("arrayColumnsSpinBox"));
    rows->setObjectName(QStringLiteral("arrayRowsSpinBox"));
    for (auto* field : {columns, rows})
    {
        field->setRange(1, 100);
        field->setValue(2);
        field->setMinimumHeight(36);
    }
    auto* gap_x = new QDoubleSpinBox(&dialog);
    auto* gap_y = new QDoubleSpinBox(&dialog);
    gap_x->setObjectName(QStringLiteral("arrayGapXSpinBox"));
    gap_y->setObjectName(QStringLiteral("arrayGapYSpinBox"));
    for (auto* field : {gap_x, gap_y})
    {
        field->setRange(0.0, 10000.0);
        field->setDecimals(2);
        field->setSingleStep(1.0);
        field->setSuffix(QStringLiteral(" mm"));
        field->setValue(5.0);
        field->setMinimumHeight(36);
    }
    form->addRow(QStringLiteral("列数（X 方向）"), columns);
    form->addRow(QStringLiteral("行数（Y 方向）"), rows);
    form->addRow(QStringLiteral("X 方向间隙"), gap_x);
    form->addRow(QStringLiteral("Y 方向间隙"), gap_y);
    layout->addLayout(form);
    auto* summary = new QLabel(&dialog);
    summary->setObjectName(QStringLiteral("arraySummaryLabel"));
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* generate = buttons->button(QDialogButtonBox::Ok);
    generate->setText(QStringLiteral("生成阵列"));
    generate->setObjectName(QStringLiteral("generateModelArrayButton"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    const auto update_summary = [=, this]()
    {
        const auto total = selected_count * rows->value() * columns->value();
        const auto added = total - selected_count;
        const double width = group_size.x * static_cast<double>(columns->value()) + gap_x->value() * (columns->value() - 1);
        const double height = group_size.y * static_cast<double>(rows->value()) + gap_y->value() * (rows->value() - 1);
        const bool valid = added > 0 && added <= SceneViewport::kMaxGeneratedModels &&
                           viewport_.ModelCount() + added <= 10000 &&
                           std::isfinite(width) && std::isfinite(height);
        generate->setEnabled(valid);
        QString text = QStringLiteral("选中 %1 个模型；新增 %2 个副本。\n阵列总计 %3 个模型。\n占用范围：%4 × %5 mm")
            .arg(static_cast<qulonglong>(selected_count)).arg(static_cast<qulonglong>(added))
            .arg(static_cast<qulonglong>(total)).arg(width, 0, 'f', 2).arg(height, 0, 'f', 2);
        if (!valid)
            text += QStringLiteral("\n至少生成一个副本；一次最多新增 %1 个，工程最多 10000 个模型。")
                .arg(static_cast<qulonglong>(SceneViewport::kMaxGeneratedModels));
        else if (minimum.x < 0 || minimum.y < 0 || minimum.x + width > viewport_.PlateSizeMm().x ||
                 minimum.y + height > viewport_.PlateSizeMm().y)
            text += QStringLiteral("\n阵列将超出当前平台范围，可在生成后调整位置或平台尺寸。");
        summary->setText(text);
    };
    connect(columns, qOverload<int>(&QSpinBox::valueChanged), &dialog, [=](int) { update_summary(); });
    connect(rows, qOverload<int>(&QSpinBox::valueChanged), &dialog, [=](int) { update_summary(); });
    connect(gap_x, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, [=](double) { update_summary(); });
    connect(gap_y, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, [=](double) { update_summary(); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    update_summary();
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    if (!viewport_.ArraySelectedModels(rows->value(), columns->value(),
        {static_cast<float>(gap_x->value()), static_cast<float>(gap_y->value())}, error))
    {
        QMessageBox::warning(this, QStringLiteral("无法生成阵列"), error);
        return;
    }
    UpdateModelSelection(true);
    UpdateProjectState();
    statusBar()->showMessage(viewport_.OperationHistory().back());
}

void SceneEditorWindow::EditPlateSize()
{
    RenderPause pause(render_timer_, viewport_);
    const glm::vec3 current = viewport_.PlateSizeMm();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("plateSizeDialog"));
    dialog.setWindowTitle(QStringLiteral("修改平台尺寸"));
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog.setFont(QFont(QStringLiteral("Microsoft YaHei"), 12));
    dialog.setMinimumWidth(480);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(18);
    auto* hint = new QLabel(
        QStringLiteral("请输入平台尺寸（毫米）。高度（Z）只保存在工程中，"
                       "不会在视口中显示。"),
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(16);
    auto* length = new QDoubleSpinBox(&dialog);
    auto* width = new QDoubleSpinBox(&dialog);
    auto* height = new QDoubleSpinBox(&dialog);
    length->setObjectName(QStringLiteral("plateLengthSpinBox"));
    width->setObjectName(QStringLiteral("plateWidthSpinBox"));
    height->setObjectName(QStringLiteral("plateHeightSpinBox"));
    for (auto* field : {length, width, height})
    {
        field->setRange(PlateDimensions::kMinMm, PlateDimensions::kMaxMm);
        field->setDecimals(2);
        field->setSingleStep(10.0);
        field->setSuffix(QStringLiteral(" mm"));
        field->setMinimumHeight(36);
    }
    length->setValue(current.x);
    width->setValue(current.y);
    height->setValue(current.z);
    form->addRow(QStringLiteral("长度（X 方向）"), length);
    form->addRow(QStringLiteral("宽度（Y 方向）"), width);
    form->addRow(QStringLiteral("高度（Z 方向）"), height);
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
    if (!viewport_.SetPlateSizeMm(
            {static_cast<float>(length->value()),
             static_cast<float>(width->value()),
             static_cast<float>(height->value())}, error))
    {
        QMessageBox::warning(this, QStringLiteral("无法修改平台尺寸"), error);
        return;
    }
    UpdateProjectState();
    const glm::vec3 size = viewport_.PlateSizeMm();
    statusBar()->showMessage(
        QStringLiteral("平台尺寸：%1 × %2 × %3 mm")
            .arg(size.x, 0, 'f', 2)
            .arg(size.y, 0, 'f', 2)
            .arg(size.z, 0, 'f', 2));
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
    const QString name = info->name;
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
    const QString operation = transform_dialog_mode_ == TransformMode::Translate ? QStringLiteral("平移") :
        transform_dialog_mode_ == TransformMode::Rotate ? QStringLiteral("旋转") : QStringLiteral("缩放");
    transform_dialog_->setWindowTitle(QStringLiteral("%1模型 — %2").arg(operation, info->name));
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
        QMessageBox::warning(
            transform_dialog_, QStringLiteral("无法修改模型"),
            QStringLiteral("参数超出支持范围：位移后的模型位置不能超出平台范围两侧"
                           "各一个平台尺寸（X 为 -长度～2×长度，Y、Z 同理），"
                           "角度和位移须为有限值，缩放倍数须大于 0。"));
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
    model_list_->setToolTip(QStringLiteral("双击模型名称或列表行可修改名称"));
    connect(model_list_, &QListWidget::itemDoubleClicked, this,
        [this](QListWidgetItem* item) { RenameModel(item->data(Qt::UserRole).toULongLong()); });

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
    model_name_label_ = add_detail(QStringLiteral("模型名"), QStringLiteral("modelName"));
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

void SceneEditorWindow::CreateSliceDock()
{
    slice_dock_ = new QDockWidget(QStringLiteral("切片预览"), this);
    slice_dock_->setObjectName(QStringLiteral("slicePreviewDock"));
    slice_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    slice_dock_->setFeatures(QDockWidget::DockWidgetClosable |
                             QDockWidget::DockWidgetMovable);
    slice_dock_->setMinimumWidth(320);

    QWidget* contents = new QWidget(slice_dock_);
    QVBoxLayout* layout = new QVBoxLayout(contents);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    QLabel* title = new QLabel(QStringLiteral("切片预览"), contents);
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    layout->addWidget(title);

    slice_layer_label_ = new QLabel(QStringLiteral("尚未生成切片。"), contents);
    slice_layer_label_->setObjectName(QStringLiteral("sliceLayerLabel"));
    slice_layer_label_->setWordWrap(true);
    slice_layer_label_->setTextFormat(Qt::PlainText);
    layout->addWidget(slice_layer_label_);

    slice_z_label_ = new QLabel(
        QStringLiteral("使用“切片 → 生成切片…”开始。"), contents);
    slice_z_label_->setObjectName(QStringLiteral("sliceZLabel"));
    slice_z_label_->setWordWrap(true);
    slice_z_label_->setTextFormat(Qt::PlainText);
    layout->addWidget(slice_z_label_);

    QHBoxLayout* layer_controls = new QHBoxLayout;
    slice_slider_ = new QSlider(Qt::Horizontal, contents);
    slice_slider_->setObjectName(QStringLiteral("sliceLayerSlider"));
    slice_slider_->setRange(1, 1);
    slice_slider_->setEnabled(false);
    layer_controls->addWidget(slice_slider_, 1);
    slice_layer_spin_ = new QSpinBox(contents);
    slice_layer_spin_->setObjectName(QStringLiteral("sliceLayerSpin"));
    slice_layer_spin_->setRange(1, 1);
    slice_layer_spin_->setEnabled(false);
    slice_layer_spin_->setFixedWidth(76);
    layer_controls->addWidget(slice_layer_spin_);
    layout->addLayout(layer_controls);

    slice_contour_label_ = new QLabel(QString(), contents);
    slice_contour_label_->setObjectName(QStringLiteral("sliceContourLabel"));
    slice_contour_label_->setWordWrap(true);
    slice_contour_label_->setTextFormat(Qt::PlainText);
    layout->addWidget(slice_contour_label_);

    slice_hatch_label_ = new QLabel(QString(), contents);
    slice_hatch_label_->setObjectName(QStringLiteral("sliceHatchLabel"));
    slice_hatch_label_->setWordWrap(true);
    slice_hatch_label_->setTextFormat(Qt::PlainText);
    layout->addWidget(slice_hatch_label_);

    slice_jump_label_ = new QLabel(QString(), contents);
    slice_jump_label_->setObjectName(QStringLiteral("sliceJumpLabel"));
    slice_jump_label_->setWordWrap(true);
    slice_jump_label_->setTextFormat(Qt::PlainText);
    layout->addWidget(slice_jump_label_);

    slice_total_label_ = new QLabel(QString(), contents);
    slice_total_label_->setObjectName(QStringLiteral("sliceTotalLabel"));
    slice_total_label_->setWordWrap(true);
    slice_total_label_->setTextFormat(Qt::PlainText);
    layout->addWidget(slice_total_label_);

    slice_hatch_settings_label_ = new QLabel(QString(), contents);
    slice_hatch_settings_label_->setObjectName(
        QStringLiteral("sliceHatchSettingsLabel"));
    slice_hatch_settings_label_->setWordWrap(true);
    slice_hatch_settings_label_->setTextFormat(Qt::PlainText);
    layout->addWidget(slice_hatch_settings_label_);

    slice_visible_check_ = new QCheckBox(QStringLiteral("显示切片"), contents);
    slice_visible_check_->setObjectName(QStringLiteral("sliceVisibleCheck"));
    slice_visible_check_->setChecked(true);
    slice_visible_check_->setEnabled(false);
    layout->addWidget(slice_visible_check_);

    slice_contour_visible_check_ =
        new QCheckBox(QStringLiteral("显示轮廓"), contents);
    slice_contour_visible_check_->setObjectName(
        QStringLiteral("sliceContourVisibleCheck"));
    slice_contour_visible_check_->setChecked(true);
    slice_contour_visible_check_->setEnabled(false);
    layout->addWidget(slice_contour_visible_check_);

    slice_hatch_visible_check_ =
        new QCheckBox(QStringLiteral("显示填充"), contents);
    slice_hatch_visible_check_->setObjectName(
        QStringLiteral("sliceHatchVisibleCheck"));
    slice_hatch_visible_check_->setChecked(true);
    slice_hatch_visible_check_->setEnabled(false);
    layout->addWidget(slice_hatch_visible_check_);

    slice_direction_visible_check_ =
        new QCheckBox(QStringLiteral("显示方向"), contents);
    slice_direction_visible_check_->setObjectName(
        QStringLiteral("sliceDirectionVisibleCheck"));
    slice_direction_visible_check_->setChecked(false);
    slice_direction_visible_check_->setEnabled(false);
    slice_direction_visible_check_->setToolTip(
        QStringLiteral("需要开启“显示填充”才能显示方向箭头"));
    layout->addWidget(slice_direction_visible_check_);

    slice_jump_visible_check_ =
        new QCheckBox(QStringLiteral("显示跳转"), contents);
    slice_jump_visible_check_->setObjectName(
        QStringLiteral("sliceJumpVisibleCheck"));
    slice_jump_visible_check_->setChecked(false);
    slice_jump_visible_check_->setEnabled(false);
    slice_jump_visible_check_->setToolTip(
        QStringLiteral("显示同一模型内相邻填充线之间的跳转"));
    layout->addWidget(slice_jump_visible_check_);

    slice_hide_models_check_ =
        new QCheckBox(QStringLiteral("隐藏全部模型"), contents);
    slice_hide_models_check_->setObjectName(
        QStringLiteral("sliceHideModelsCheck"));
    slice_hide_models_check_->setChecked(false);
    slice_hide_models_check_->setEnabled(false);
    slice_hide_models_check_->setToolTip(
        QStringLiteral("只隐藏模型显示，不影响切片数据"));
    layout->addWidget(slice_hide_models_check_);

    QHBoxLayout* button_row = new QHBoxLayout;
    slice_reslice_button_ = new QPushButton(QStringLiteral("重新切片…"), contents);
    slice_reslice_button_->setObjectName(QStringLiteral("sliceResliceButton"));
    slice_reslice_button_->setEnabled(false);
    button_row->addWidget(slice_reslice_button_);
    slice_clear_button_ = new QPushButton(QStringLiteral("清除"), contents);
    slice_clear_button_->setObjectName(QStringLiteral("sliceClearButton"));
    slice_clear_button_->setEnabled(false);
    button_row->addWidget(slice_clear_button_);
    layout->addLayout(button_row);

    QLabel* note = new QLabel(
        QStringLiteral("仅预览当前层轮廓、填充、方向和跳转，不写入工程，也不导出文件。"),
        contents);
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);
    layout->addWidget(note);
    layout->addStretch(1);

    slice_dock_->setWidget(contents);
    addDockWidget(Qt::RightDockWidgetArea, slice_dock_);
    resizeDocks({slice_dock_}, {360}, Qt::Horizontal);
    slice_dock_->hide();

    auto set_layer = [this](int value)
    {
        if (viewport_.SetSliceLayer(static_cast<std::size_t>(value)))
            UpdateSlicePanel(true);
    };
    connect(slice_slider_, &QSlider::valueChanged, this, set_layer);
    connect(slice_layer_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, set_layer);
    connect(slice_visible_check_, &QCheckBox::toggled, this, [this](bool checked)
    {
        viewport_.SetSlicePreviewVisible(checked);
        UpdateSlicePanel(true);
    });
    connect(slice_contour_visible_check_, &QCheckBox::toggled, this,
            [this](bool checked)
    {
        viewport_.SetSliceContoursVisible(checked);
        UpdateSlicePanel(true);
    });
    connect(slice_hatch_visible_check_, &QCheckBox::toggled, this,
            [this](bool checked)
    {
        viewport_.SetSliceHatchesVisible(checked);
        UpdateSlicePanel(true);
    });
    connect(slice_direction_visible_check_, &QCheckBox::toggled, this,
            [this](bool checked)
    {
        viewport_.SetSliceDirectionVisible(checked);
        UpdateSlicePanel(true);
    });
    connect(slice_jump_visible_check_, &QCheckBox::toggled, this,
            [this](bool checked)
    {
        viewport_.SetSliceJumpsVisible(checked);
        UpdateSlicePanel(true);
    });
    connect(slice_hide_models_check_, &QCheckBox::toggled, this,
            [this](bool checked)
    {
        viewport_.SetModelsHidden(checked);
        UpdateSlicePanel(true);
    });
    connect(slice_reslice_button_, &QPushButton::clicked,
            this, &SceneEditorWindow::GenerateSlice);
    connect(slice_clear_button_, &QPushButton::clicked,
            this, &SceneEditorWindow::ClearSlicePreview);
    UpdateSlicePanel(true);
}

void SceneEditorWindow::UpdateSlicePanel(bool force)
{
    if (!slice_action_ || !clear_slice_action_ || !slice_dock_)
        return;

    const bool can_slice = viewport_.ModelCount() > 0 &&
                           !viewport_.IsTransformEditing();
    slice_action_->setEnabled(can_slice);

    if (viewport_.ConsumeSliceInvalidation())
        statusBar()->showMessage(QStringLiteral("模型已变更，切片预览已清除。"));

    const auto info = viewport_.SliceInfo();
    clear_slice_action_->setEnabled(info.has_preview);
    if (slice_hide_models_check_)
        slice_hide_models_check_->setEnabled(
            info.has_preview && !viewport_.IsTransformEditing());
    const auto revision = viewport_.SliceRevision();
    if (!force && revision == last_slice_revision_)
        return;
    last_slice_revision_ = revision;

    if (!info.has_preview)
    {
        slice_had_preview_ = false;
        if (slice_dock_->isVisible())
            slice_dock_->hide();
        slice_layer_label_->setText(QStringLiteral("尚未生成切片。"));
        slice_z_label_->setText(
            QStringLiteral("使用“切片 → 生成切片…”开始。"));
        slice_contour_label_->clear();
        slice_hatch_label_->clear();
        slice_jump_label_->clear();
        slice_total_label_->clear();
        slice_hatch_settings_label_->clear();
        const QSignalBlocker slider_blocker(slice_slider_);
        const QSignalBlocker spin_blocker(slice_layer_spin_);
        const QSignalBlocker check_blocker(slice_visible_check_);
        const QSignalBlocker contour_check_blocker(slice_contour_visible_check_);
        const QSignalBlocker hatch_check_blocker(slice_hatch_visible_check_);
        const QSignalBlocker direction_check_blocker(
            slice_direction_visible_check_);
        const QSignalBlocker jump_check_blocker(slice_jump_visible_check_);
        const QSignalBlocker hide_models_blocker(slice_hide_models_check_);
        slice_slider_->setRange(1, 1);
        slice_layer_spin_->setRange(1, 1);
        slice_slider_->setValue(1);
        slice_layer_spin_->setValue(1);
        slice_slider_->setEnabled(false);
        slice_layer_spin_->setEnabled(false);
        slice_visible_check_->setChecked(false);
        slice_visible_check_->setEnabled(false);
        slice_contour_visible_check_->setChecked(false);
        slice_contour_visible_check_->setEnabled(false);
        slice_hatch_visible_check_->setChecked(false);
        slice_hatch_visible_check_->setEnabled(false);
        slice_direction_visible_check_->setChecked(false);
        slice_direction_visible_check_->setEnabled(false);
        slice_jump_visible_check_->setChecked(false);
        slice_jump_visible_check_->setEnabled(false);
        slice_hide_models_check_->setChecked(false);
        slice_hide_models_check_->setEnabled(false);
        slice_reslice_button_->setEnabled(false);
        slice_clear_button_->setEnabled(false);
        return;
    }

    if (!slice_had_preview_)
    {
        slice_dock_->show();
        slice_dock_->raise();
    }
    slice_had_preview_ = true;

    slice_layer_label_->setText(
        QStringLiteral("第 %1 / %2 个有效层")
            .arg(static_cast<qulonglong>(info.layer_index))
            .arg(static_cast<qulonglong>(info.layer_count)));
    slice_z_label_->setText(
        QStringLiteral("Z = %1 mm").arg(QLocale().toString(info.z_mm, 'f', 3)));
    slice_contour_label_->setText(
        QStringLiteral("本层轮廓：%1 条 | 路径长：%2 mm")
            .arg(static_cast<qulonglong>(info.contour_count))
            .arg(QLocale().toString(info.layer_contour_path_length_mm, 'f', 2)));
    if (info.hatch.enabled)
    {
        slice_hatch_label_->setText(
            QStringLiteral("本层填充：%1 条 | 路径长：%2 mm")
                .arg(static_cast<qulonglong>(info.hatch_count))
                .arg(QLocale().toString(info.layer_hatch_path_length_mm, 'f', 2)));
        slice_hatch_settings_label_->setText(
            QStringLiteral("填充：%1 mm / %2° / %3°/层")
                .arg(QLocale().toString(info.hatch.spacing_mm, 'f', 3))
                .arg(QLocale().toString(info.hatch.angle_deg, 'f', 1))
                .arg(QLocale().toString(
                    info.hatch.layer_angle_increment_deg, 'f', 1)));
    }
    else
    {
        slice_hatch_label_->setText(QStringLiteral("本层填充：未启用"));
        slice_hatch_settings_label_->setText(QStringLiteral("填充：未启用"));
    }
    slice_jump_label_->setText(
        QStringLiteral("本层跳转：%1 次 | 总长 %2 mm")
            .arg(static_cast<qulonglong>(info.jump_count))
            .arg(QLocale().toString(info.layer_jump_path_length_mm, 'f', 2)));
    slice_total_label_->setText(
        QStringLiteral("轮廓总长：%1 mm | 填充总长：%2 mm\n总路径长：%3 mm")
            .arg(QLocale().toString(info.total_contour_path_length_mm, 'f', 2))
            .arg(QLocale().toString(info.total_hatch_path_length_mm, 'f', 2))
            .arg(QLocale().toString(info.total_path_length_mm, 'f', 2)));

    const int layer_count = static_cast<int>(info.layer_count);
    const int layer_index = static_cast<int>(info.layer_index);
    {
        const QSignalBlocker slider_blocker(slice_slider_);
        const QSignalBlocker spin_blocker(slice_layer_spin_);
        const QSignalBlocker check_blocker(slice_visible_check_);
        const QSignalBlocker contour_check_blocker(slice_contour_visible_check_);
        const QSignalBlocker hatch_check_blocker(slice_hatch_visible_check_);
        const QSignalBlocker direction_check_blocker(
            slice_direction_visible_check_);
        const QSignalBlocker jump_check_blocker(slice_jump_visible_check_);
        const QSignalBlocker hide_models_blocker(slice_hide_models_check_);
        slice_slider_->setRange(1, layer_count);
        slice_layer_spin_->setRange(1, layer_count);
        slice_slider_->setValue(layer_index);
        slice_layer_spin_->setValue(layer_index);
        slice_slider_->setEnabled(layer_count > 1);
        slice_layer_spin_->setEnabled(layer_count > 1);
        slice_visible_check_->setChecked(info.visible);
        slice_visible_check_->setEnabled(true);
        slice_contour_visible_check_->setChecked(info.contours_visible);
        slice_contour_visible_check_->setEnabled(info.contour_count > 0);
        slice_hatch_visible_check_->setChecked(
            info.hatch.enabled && info.hatches_visible);
        slice_hatch_visible_check_->setEnabled(
            info.hatch.enabled && info.hatch_count > 0);
        slice_direction_visible_check_->setChecked(info.direction_visible);
        slice_direction_visible_check_->setEnabled(
            info.hatch.enabled && info.hatch_count > 0 &&
            info.hatches_visible);
        slice_jump_visible_check_->setChecked(info.jumps_visible);
        slice_jump_visible_check_->setEnabled(
            info.hatch.enabled && info.hatch_count > 0);
        slice_hide_models_check_->setChecked(info.models_hidden);
        slice_hide_models_check_->setEnabled(!viewport_.IsTransformEditing());
    }
    slice_reslice_button_->setEnabled(can_slice);
    slice_clear_button_->setEnabled(true);
}

void SceneEditorWindow::GenerateSlice()
{
    if (viewport_.ModelCount() == 0)
    {
        QMessageBox::information(this, QStringLiteral("生成切片"),
            QStringLiteral("请先导入至少一个 STL 模型。"));
        return;
    }
    if (viewport_.IsTransformEditing())
    {
        QMessageBox::information(this, QStringLiteral("生成切片"),
            QStringLiteral("请先关闭模型变换参数窗口。"));
        return;
    }

    RenderPause pause(render_timer_, viewport_);

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("sliceSettingsDialog"));
    dialog.setWindowTitle(QStringLiteral("生成切片"));
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.setFont(QFont(QStringLiteral("Microsoft YaHei"), 11));
    dialog.setMinimumWidth(460);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(14);
    QFormLayout* form = new QFormLayout;
    auto* thickness = new QDoubleSpinBox(&dialog);
    thickness->setObjectName(QStringLiteral("sliceLayerThicknessSpin"));
    thickness->setRange(kMinLayerThicknessMm, kMaxLayerThicknessMm);
    thickness->setDecimals(3);
    thickness->setSingleStep(0.01);
    thickness->setSuffix(QStringLiteral(" mm"));
    thickness->setValue(slice_layer_thickness_mm_);
    form->addRow(QStringLiteral("层厚"), thickness);
    QLabel* estimate_label = new QLabel(&dialog);
    estimate_label->setWordWrap(true);
    estimate_label->setTextFormat(Qt::PlainText);
    form->addRow(QStringLiteral("预计"), estimate_label);
    layout->addLayout(form);

    QGroupBox* hatch_group = new QGroupBox(QStringLiteral("填充"), &dialog);
    hatch_group->setObjectName(QStringLiteral("sliceHatchGroup"));
    QVBoxLayout* hatch_layout = new QVBoxLayout(hatch_group);
    QCheckBox* hatch_enabled =
        new QCheckBox(QStringLiteral("启用填充"), hatch_group);
    hatch_enabled->setObjectName(QStringLiteral("sliceHatchEnabledCheck"));
    hatch_enabled->setChecked(slice_hatch_enabled_);
    hatch_layout->addWidget(hatch_enabled);
    QFormLayout* hatch_form = new QFormLayout;
    auto* hatch_spacing = new QDoubleSpinBox(hatch_group);
    hatch_spacing->setObjectName(QStringLiteral("sliceHatchSpacingSpin"));
    hatch_spacing->setRange(kMinHatchSpacingMm, kMaxHatchSpacingMm);
    hatch_spacing->setDecimals(3);
    hatch_spacing->setSingleStep(0.01);
    hatch_spacing->setSuffix(QStringLiteral(" mm"));
    hatch_spacing->setValue(slice_hatch_spacing_mm_);
    hatch_form->addRow(QStringLiteral("扫描间距"), hatch_spacing);
    auto* hatch_angle = new QDoubleSpinBox(hatch_group);
    hatch_angle->setObjectName(QStringLiteral("sliceHatchAngleSpin"));
    hatch_angle->setRange(0.0, 180.0);
    hatch_angle->setDecimals(1);
    hatch_angle->setSingleStep(1.0);
    hatch_angle->setSuffix(QStringLiteral(" °"));
    hatch_angle->setValue(slice_hatch_angle_deg_);
    hatch_form->addRow(QStringLiteral("扫描角度"), hatch_angle);
    auto* hatch_increment = new QDoubleSpinBox(hatch_group);
    hatch_increment->setObjectName(
        QStringLiteral("sliceHatchLayerIncrementSpin"));
    hatch_increment->setRange(-180.0, 180.0);
    hatch_increment->setDecimals(1);
    hatch_increment->setSingleStep(1.0);
    hatch_increment->setSuffix(QStringLiteral(" °/层"));
    hatch_increment->setValue(slice_hatch_layer_increment_deg_);
    hatch_form->addRow(QStringLiteral("层间角度增量"), hatch_increment);
    hatch_layout->addLayout(hatch_form);
    layout->addWidget(hatch_group);
    auto update_hatch_fields = [hatch_enabled, hatch_spacing,
                                hatch_angle, hatch_increment]()
    {
        const bool enabled = hatch_enabled->isChecked();
        hatch_spacing->setEnabled(enabled);
        hatch_angle->setEnabled(enabled);
        hatch_increment->setEnabled(enabled);
    };
    connect(hatch_enabled, &QCheckBox::toggled, &dialog,
            [update_hatch_fields](bool) { update_hatch_fields(); });
    update_hatch_fields();

    QLabel* note = new QLabel(
        QStringLiteral("按平台坐标切片所有模型（包括隐藏模型）；只生成轮廓和填充预览，"
                       "不导出文件，也不进入撤销记录。模型低于 Z=0 的部分不切。"
                       "填充线会穿透模型显示。"),
        &dialog);
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);
    layout->addWidget(note);

    auto update_estimate = [&]()
    {
        const double layer_thickness = thickness->value();
        double max_z_mm = 0.0;
        for (const auto& info : viewport_.ModelInfos())
            max_z_mm = std::max(max_z_mm,
                static_cast<double>(info.world_bounds_max_mm.z));
        const std::size_t layers =
            layer_thickness > 0.0 && max_z_mm > 0.0
                ? static_cast<std::size_t>(std::ceil(max_z_mm / layer_thickness))
                : 0;
        estimate_label->setText(
            QStringLiteral("预计有效层数上限：%1")
                .arg(static_cast<qulonglong>(layers)));
    };
    connect(thickness, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            &dialog, [&](double) { update_estimate(); });
    update_estimate();

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (QPushButton* ok_button = buttons->button(QDialogButtonBox::Ok))
        ok_button->setText(QStringLiteral("生成"));
    if (QPushButton* cancel_button = buttons->button(QDialogButtonBox::Cancel))
        cancel_button->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const double layer_thickness = thickness->value();
    slice_layer_thickness_mm_ = layer_thickness;
    slice_hatch_enabled_ = hatch_enabled->isChecked();
    slice_hatch_spacing_mm_ = hatch_spacing->value();
    slice_hatch_angle_deg_ = hatch_angle->value();
    slice_hatch_layer_increment_deg_ = hatch_increment->value();

    SliceSettings settings;
    settings.layer_thickness_mm = layer_thickness;
    settings.hatch.enabled = slice_hatch_enabled_;
    settings.hatch.spacing_mm = slice_hatch_spacing_mm_;
    settings.hatch.angle_deg = slice_hatch_angle_deg_;
    settings.hatch.layer_angle_increment_deg = slice_hatch_layer_increment_deg_;
    settings.hatch.spot_compensation_mm = 0.08;

    SceneViewport::SliceEstimate estimate;
    QString error;
    if (!viewport_.PrepareSlice(settings, estimate, error))
    {
        QMessageBox::warning(this, QStringLiteral("无法生成切片"), error);
        return;
    }

    if (estimate.estimated_layers > 5000 || estimate.triangle_count > 2000000)
    {
        const auto answer = QMessageBox::question(this,
            QStringLiteral("切片耗时提示"),
            QStringLiteral("预计最多 %1 个有效层、共 %2 个三角面，切片可能耗时较长。是否继续？")
                .arg(static_cast<qulonglong>(estimate.estimated_layers))
                .arg(static_cast<qulonglong>(estimate.triangle_count)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    QProgressDialog progress(QStringLiteral("正在切片，请稍候…"),
                             QStringLiteral("取消"), 0, 100, this);
    progress.setWindowTitle(QStringLiteral("切片"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);
    progress.show();
    QCoreApplication::processEvents();

    auto progress_callback = [&progress](std::size_t completed,
                                         std::size_t total) -> bool
    {
        if (progress.wasCanceled())
            return false;
        if (total > 0)
        {
            const int maximum = static_cast<int>(
                std::min<std::size_t>(
                    total, static_cast<std::size_t>(std::numeric_limits<int>::max())));
            if (progress.maximum() != maximum)
                progress.setMaximum(maximum);
            progress.setValue(static_cast<int>(
                std::min<std::size_t>(completed,
                    static_cast<std::size_t>(maximum))));
        }
        QCoreApplication::processEvents();
        return !progress.wasCanceled();
    };

    bool cancelled = false;
    QStringList issues;
    const bool ok = viewport_.GenerateSlice(
        settings, progress_callback, cancelled, error, issues);
    progress.close();

    if (cancelled)
    {
        statusBar()->showMessage(QStringLiteral("已取消切片，预览未更新。"));
        return;
    }
    if (!ok)
    {
        QMessageBox::warning(this, QStringLiteral("无法生成切片"),
            error.isEmpty() ? QStringLiteral("没有生成任何有效切片。") : error);
        return;
    }

    last_slice_revision_ = 0;
    UpdateSlicePanel(true);
    slice_dock_->show();
    slice_dock_->raise();
    const auto info = viewport_.SliceInfo();
    statusBar()->showMessage(
        QStringLiteral("切片完成：%1 个有效层 | 轮廓 %2 mm | 填充 %3 mm | 总长 %4 mm")
            .arg(static_cast<qulonglong>(info.layer_count))
            .arg(QLocale().toString(info.total_contour_path_length_mm, 'f', 2))
            .arg(QLocale().toString(info.total_hatch_path_length_mm, 'f', 2))
            .arg(QLocale().toString(info.total_path_length_mm, 'f', 2)));
    if (!issues.isEmpty())
        QMessageBox::warning(this, QStringLiteral("部分模型未生成切片"),
            issues.join(QStringLiteral("\n")));
}

void SceneEditorWindow::ClearSlicePreview()
{
    if (!viewport_.SliceInfo().has_preview)
        return;
    RenderPause pause(render_timer_, viewport_);
    viewport_.ClearSlicePreview();
    slice_had_preview_ = false;
    last_slice_revision_ = 0;
    if (slice_dock_->isVisible())
        slice_dock_->hide();
    UpdateSlicePanel(true);
    statusBar()->showMessage(QStringLiteral("已清除切片预览。"));
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
                info.name, model_list_);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id));
            item->setToolTip(QDir::toNativeSeparators(path));
            model_items_.emplace(id, item);

            QWidget* row = new QWidget(model_list_);
            row->setProperty("modelId", QVariant::fromValue<qulonglong>(id));
            row->installEventFilter(this);
            row->setAutoFillBackground(true);
            row->setBackgroundRole(QPalette::Window);
            QHBoxLayout* row_layout = new QHBoxLayout(row);
            row_layout->setContentsMargins(4, 2, 4, 2);
            row_layout->setSpacing(4);
            QLabel* name_label = new QLabel(info.name, row);
            name_label->setObjectName(QStringLiteral("modelRowName"));
            name_label->setTextFormat(Qt::PlainText);
            name_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            name_label->setToolTip(QStringLiteral("双击修改模型名称\n%1").arg(item->toolTip()));
            name_label->setProperty("modelId", QVariant::fromValue<qulonglong>(id));
            name_label->installEventFilter(this);
            name_label->setCursor(Qt::PointingHandCursor);
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
                const QString name = current->name;
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
    const bool can_duplicate = details_id != 0 && !viewport_.IsTransformEditing();
    duplicate_action_->setEnabled(can_duplicate);
    array_action_->setEnabled(can_duplicate);
    place_on_plate_action_->setEnabled(can_duplicate);
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
        item->setText(info.name);
        auto* name_label = row->findChild<QLabel*>(QStringLiteral("modelRowName"));
        name_label->setText(info.name);
        QPalette name_palette = name_label->palette();
        name_palette.setColor(QPalette::WindowText, model_list_->palette().color(
            info.visible ? QPalette::Active : QPalette::Disabled, QPalette::Text));
        name_label->setPalette(name_palette);
        QPalette palette = row->palette();
        palette.setColor(QPalette::Window, info.selected ? QColor(214, 234, 255)
            : model_list_->palette().color(QPalette::Base));
        row->setPalette(palette);
    }
    if (force || details_id != last_details_model_id_ || (details_id && model_name_label_->text() != current->name))
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
    model_name_label_->setText(info->name);
    model_name_label_->setToolTip(info->name);
    model_path_edit_->setPlainText(QDir::toNativeSeparators(file.absoluteFilePath()));
    model_path_edit_->setToolTip(model_path_edit_->toPlainText());
    model_file_size_label_->setText(FormatFileSize(static_cast<qint64>(info->source_file_size)));
    model_triangles_label_->setText(
        QLocale().toString(static_cast<qulonglong>(info->triangle_count)));
    UpdateModelTransformDetails();
    UpdateModelTransformDialog();
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

bool SceneEditorWindow::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick &&
        static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
    {
        const QVariant id = object->property("modelId");
        if (id.isValid())
        {
            RenameModel(id.toULongLong());
            return true;
        }
    }
    return QMainWindow::eventFilter(object, event);
}

void SceneEditorWindow::RenameModel(std::uint64_t id)
{
    const auto* info = viewport_.ModelInfo(id);
    if (!info) return;
    const QString initial_name = info->name;
    RenderPause pause(render_timer_, viewport_);
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("renameModelDialog"));
    dialog.setWindowTitle(QStringLiteral("修改模型名称"));
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.setFont(QFont(QStringLiteral("Microsoft YaHei"), 11));
    dialog.setMinimumWidth(480);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(14);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(initial_name, &dialog);
    name->setObjectName(QStringLiteral("modelNameEdit"));
    name->setMaxLength(ModelNames::kMaxLength);
    name->setMinimumHeight(34);
    form->addRow(QStringLiteral("模型名称"), name);
    layout->addLayout(form);
    auto* hint = new QLabel(QStringLiteral("名称不能为空，且不能重名。"), &dialog);
    hint->setObjectName(QStringLiteral("modelNameHint"));
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* apply = buttons->button(QDialogButtonBox::Ok);
    apply->setObjectName(QStringLiteral("renameModelButton"));
    apply->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
    {
        QString error;
        if (!viewport_.RenameModel(id, name->text(), error))
        {
            hint->setText(error);
            name->setFocus();
            name->selectAll();
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    name->setFocus();
    name->selectAll();
    if (dialog.exec() != QDialog::Accepted) return;
    UpdateModelSelection(true);
    UpdateProjectState();
    statusBar()->showMessage(QStringLiteral("模型名称：%1").arg(viewport_.ModelInfo(id)->name));
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
