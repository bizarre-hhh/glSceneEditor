#include "scene_editor_window.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
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

#include "scene_viewport.h"

namespace
{
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
        UpdateModelSelection();
    });

    QMenu* file_menu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    QAction* open_stl_action =
        file_menu->addAction(QStringLiteral("打开 STL 模型(&O)..."));
    open_stl_action->setObjectName(QStringLiteral("openStlAction"));
    open_stl_action->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    open_stl_action->setIconText(QStringLiteral("打开 STL"));
    open_stl_action->setToolTip(QStringLiteral("打开 STL 模型，可多选 (Ctrl+O)"));
    open_stl_action->setShortcut(QKeySequence::Open);
    connect(open_stl_action, &QAction::triggered,
            this, &SceneEditorWindow::OpenStl);
    file_menu->addSeparator();
    QAction* quit_action = file_menu->addAction(QStringLiteral("退出(&X)"));
    connect(quit_action, &QAction::triggered, qApp, &QApplication::quit);

    QMenu* view_menu = menuBar()->addMenu(QStringLiteral("视图(&V)"));

    QAction* reset_action = view_menu->addAction(QStringLiteral("重置视角"));
    reset_action->setObjectName(QStringLiteral("resetViewAction"));
    reset_action->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    reset_action->setToolTip(QStringLiteral("恢复初始视角（视口快捷键 Home）"));
    connect(reset_action, &QAction::triggered,
            this, [this]() { viewport_.ResetView(); });
    view_menu->addSeparator();

    QAction* settings_action = view_menu->addAction(QStringLiteral("场景设置"));
    settings_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    settings_action->setCheckable(true);
    connect(settings_action, &QAction::toggled, this,
            [this](bool checked) { viewport_.SetSettingsVisible(checked); });

    QToolBar* toolbar = new QToolBar(QStringLiteral("主工具栏"), this);
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setAllowedAreas(Qt::TopToolBarArea);
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addToolBar(Qt::TopToolBarArea, toolbar);
    toolbar->addAction(open_stl_action);
    toolbar->addSeparator();
    toolbar->addAction(reset_action);
    toolbar->addSeparator();
    toolbar->addAction(settings_action);

    CreateModelDock();
    view_menu->addSeparator();
    view_menu->addAction(model_dock_->toggleViewAction());
    view_menu->addAction(toolbar->toggleViewAction());

    statusBar()->showMessage(QStringLiteral(
        "就绪：仅显示基板 | 右键旋转，中键平移，滚轮缩放，Home 重置"));
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
                header->fontMetrics().horizontalAdvance(QStringLiteral("00.00")) + 4);
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
    auto add_note = [](QGroupBox* group, const QString& text)
    {
        auto* grid = static_cast<QGridLayout*>(group->layout());
        QLabel* note = new QLabel(text, group);
        note->setWordWrap(true);
        note->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        grid->addWidget(note, grid->rowCount(), 0, 1, 4);
    };
    add_note(position_group, QStringLiteral("场景世界坐标"));
    add_note(transform_group, QStringLiteral("相对加载后的初始姿态"));
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
            const QString path = QString::fromLocal8Bit(info.path.c_str());
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
                    QString::fromLocal8Bit(current->path.c_str())).fileName();
                viewport_.RemoveStl(id);
                RefreshModelList();
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
        selection->setToolTip(info.selected ? QStringLiteral("取消选中该模型") : QStringLiteral("选中该模型"));
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
        return;
    }

    const QString path = QString::fromLocal8Bit(info->path.c_str());
    const QFileInfo file(path);
    model_name_label_->setText(file.fileName());
    model_name_label_->setToolTip(file.fileName());
    model_path_edit_->setPlainText(QDir::toNativeSeparators(file.absoluteFilePath()));
    model_path_edit_->setToolTip(model_path_edit_->toPlainText());
    model_file_size_label_->setText(file.exists() ? FormatFileSize(file.size())
                                                 : QStringLiteral("文件已不存在"));
    model_triangles_label_->setText(
        QLocale().toString(static_cast<qulonglong>(info->triangle_count)));
    auto format_dimension = [](float value)
    {
        return QStringLiteral("%1 mm").arg(QLocale().toString(value, 'f', 2));
    };
    model_size_x_label_->setText(format_dimension(info->size_mm.x));
    model_size_y_label_->setText(format_dimension(info->size_mm.y));
    model_size_z_label_->setText(format_dimension(info->size_mm.z));
    UpdateModelTransformDetails();
}

void SceneEditorWindow::UpdateModelTransformDetails()
{
    const auto& info = viewport_.CurrentModelInfo();
    if (!info || !info->selected)
        return;
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
        const QByteArray local_path = QDir::fromNativeSeparators(path).toLocal8Bit();
        if (viewport_.LoadStl(local_path.constData(), error, size_mm))
            ++loaded_count;
        else
            failures.append(QStringLiteral("%1：%2").arg(
                QFileInfo(path).fileName(), QString::fromLocal8Bit(error.c_str())));
    }
    if (loaded_count > 0)
    {
        RefreshModelList();
        statusBar()->showMessage(QStringLiteral("已添加 %1 个模型 | 当前共 %2 个模型")
            .arg(loaded_count).arg(static_cast<qulonglong>(viewport_.ModelCount())));
    }
    if (!failures.isEmpty())
        QMessageBox::warning(this, QStringLiteral("部分 STL 无法打开"), failures.join(QStringLiteral("\n")));
    render_timer_.start();
}
