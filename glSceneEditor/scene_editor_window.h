#ifndef SCENE_EDITOR_WINDOW_H
#define SCENE_EDITOR_WINDOW_H

#include <QMainWindow>
#include <QTimer>

#include <array>
#include <cstdint>
#include <unordered_map>

#include "recent_projects.h"
#include "model_transform.h"

class QAction;
class QDialog;
class QDoubleSpinBox;
class QCloseEvent;
class QDockWidget;
class QGroupBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class SceneViewport;

// Owns the Qt shell: menus, toolbar, model dock, status bar and render timer.
class SceneEditorWindow : public QMainWindow
{
public:
    explicit SceneEditorWindow(SceneViewport& viewport, const QString& project_path = QString(),
                               const QString& recent_projects_file = RecentProjects::RecordFilePath());
    ~SceneEditorWindow() override;

    bool IsReady() const { return ready_; }
    void StartRendering();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void NewProject();
    void OpenProject();
    void RememberCurrentProject();
    bool SaveProject(bool save_as = false);
    bool ConfirmSaveChanges();
    void UpdateProjectState();
    void UndoOperation();
    void RedoOperation();
    void ShowOperationHistory();
    void UpdateOperationHistory();
    void OpenStl();
    void EditPlateSize();
    void EditModelTransform(TransformMode mode);
    void ApplyModelTransformDialog();
    void UpdateModelTransformDialog(bool force = false);
    void CreateModelDock();
    void RefreshModelList();
    void UpdateModelDetails();
    void UpdateModelTransformDetails();
    void UpdateModelSelection(bool force = false);

    SceneViewport& viewport_;
    QTimer render_timer_;
    QString project_path_;
    QString recent_projects_file_;
    std::uint64_t saved_history_revision_ = 0;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QDialog* transform_dialog_ = nullptr;
    std::array<QDoubleSpinBox*, 3> transform_fields_{};
    std::uint64_t transform_dialog_model_id_ = 0;
    std::uint64_t transform_dialog_revision_ = 0;
    TransformMode transform_dialog_mode_ = TransformMode::Translate;
    QDialog* history_dialog_ = nullptr;
    QListWidget* history_list_ = nullptr;
    QLabel* history_empty_label_ = nullptr;
    std::uint64_t last_history_revision_ = 0;
    QDockWidget* model_dock_ = nullptr;
    QListWidget* model_list_ = nullptr;
    QLabel* model_count_label_ = nullptr;
    QLabel* empty_models_label_ = nullptr;
    QGroupBox* model_details_ = nullptr;
    QLabel* model_name_label_ = nullptr;
    QPlainTextEdit* model_path_edit_ = nullptr;
    QLabel* model_file_size_label_ = nullptr;
    QLabel* model_triangles_label_ = nullptr;
    QLabel* model_size_x_label_ = nullptr;
    QLabel* model_size_y_label_ = nullptr;
    QLabel* model_size_z_label_ = nullptr;
    std::array<QLabel*, 3> model_position_labels_{};
    std::array<QLabel*, 3> model_bounds_min_labels_{};
    std::array<QLabel*, 3> model_bounds_max_labels_{};
    std::array<QLabel*, 3> model_translation_labels_{};
    std::array<QLabel*, 3> model_scale_labels_{};
    std::array<QLabel*, 3> model_rotation_labels_{};
    std::unordered_map<std::uint64_t, QListWidgetItem*> model_items_;
    std::uint64_t last_model_state_revision_ = 0;
    std::uint64_t last_details_model_id_ = 0;
    std::uint64_t last_transform_revision_ = 0;
    bool ready_ = false;
};

#endif
