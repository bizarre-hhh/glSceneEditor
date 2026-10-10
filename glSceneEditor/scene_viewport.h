#ifndef SCENE_VIEWPORT_H
#define SCENE_VIEWPORT_H

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "camera_controller.h"
#include "project_file.h"
#include "model_transform.h"

class BasePlate;
class Shader;
class StlModel;
struct ImFont;
struct GLFWwindow;

// Owns the GLFW/OpenGL scene, graphics resources, STL models and camera input.
// The Qt window that wraps NativeHandle() must be destroyed before this object.
class SceneViewport
{
public:
    static constexpr int kInitialWidth = 1280;
    static constexpr int kInitialHeight = 800;

    using ModelId = std::uint64_t;

    struct StlModelInfo
    {
        std::string path;
        glm::vec3 size_mm{0.0f};
        std::size_t triangle_count = 0;
        bool visible = true;
        bool selected = false;
        glm::vec3 world_position_mm{0.0f};
        glm::vec3 world_bounds_min_mm{0.0f};
        glm::vec3 world_bounds_max_mm{0.0f};
        glm::vec3 local_translation_mm{0.0f};
        glm::vec3 local_scale{1.0f};
        glm::vec3 local_rotation_degrees{0.0f};
        std::uint64_t transform_revision = 0;
        ModelId id = 0;
        bool bounding_box_visible = false;
        std::size_t source_file_size = 0;
        glm::vec2 placement_center_mm{PlateDimensions::kDefaultMm * 0.5f};
    };

    SceneViewport();
    ~SceneViewport();
    SceneViewport(const SceneViewport&) = delete;
    SceneViewport& operator=(const SceneViewport&) = delete;

    bool Initialize();
    void ShowNativeWindow();
    void* NativeHandle() const;
    bool RenderFrame(); // false requests application shutdown
    void ReleaseCursor();
    bool LoadStl(const std::string& path, std::string& error, glm::vec3& size_mm);
    // Numeric edits are one step; Begin/End merge an entire gizmo gesture.
    bool BeginModelTransform(ModelId id, TransformMode mode);
    bool TransformModelTo(ModelId id, const ModelTransform& transform, TransformMode mode);
    void EndModelTransform();
    bool BeginModelMove(ModelId id) { return BeginModelTransform(id, TransformMode::Translate); }
    bool MoveModelTo(ModelId id, const glm::vec3& translation_mm);
    void EndModelMove() { EndModelTransform(); }
    bool BeginTransformEditing(ModelId id, TransformMode mode);
    void EndTransformEditing();
    bool IsTransformEditing() const { return transform_editing_model_id_ != 0; }
    void SetTransformMode(TransformMode mode);
    TransformMode GetTransformMode() const { return transform_mode_; }
    std::optional<glm::mat4> ModelGizmoMatrix(ModelId id) const;
    bool TransformModelFromGizmo(ModelId id, const glm::mat4& matrix, TransformMode mode);
    bool CanUndo() const;
    bool CanRedo() const;
    bool Undo(QString& error);
    bool Redo(QString& error);
    const std::vector<QString>& OperationHistory() const { return operation_history_; }
    std::uint64_t HistoryRevision() const { return history_revision_; }
    void NewProject();
    ProjectData SnapshotProject() const;
    bool ApplyProject(const ProjectData& project, QString& error);
    std::uint64_t ProjectRevision() const { return project_revision_; }
    const StlModelInfo* CurrentModelInfo() const { return ModelInfo(active_model_id_); }
    const StlModelInfo* ModelInfo(ModelId id) const;
    std::vector<StlModelInfo> ModelInfos() const;
    std::size_t ModelCount() const { return models_.size(); }
    std::uint64_t ModelStateRevision() const { return model_state_revision_; }
    bool IsModelVisible() const
    {
        const auto* info = CurrentModelInfo();
        return info && info->visible;
    }
    void SetModelVisible(bool visible) { SetModelVisible(active_model_id_, visible); }
    void SetModelVisible(ModelId id, bool visible);
    void SetModelBoundingBoxVisible(ModelId id, bool visible);
    bool IsModelSelected() const
    {
        const auto* info = CurrentModelInfo();
        return info && info->selected;
    }
    void SetModelSelected(bool selected) { SetModelSelected(active_model_id_, selected); }
    void SetModelSelected(ModelId id, bool selected);
    void ClearModelSelection();
    bool SelectModelAt(float x_pixels, float y_pixels, glm::vec3* hit_point = nullptr);
    void RemoveStl() { RemoveStl(active_model_id_); }
    void RemoveStl(ModelId id);

    glm::vec2 PlateSizeMm() const { return plate_size_mm_; }
    bool SetPlateSizeMm(const glm::vec2& size_mm, QString& error);
    void ResetView();
    const Camera& GetCamera() const { return camera_controller_.GetCamera(); }
    void SetSettingsVisible(bool visible) { show_settings_ = visible; }
    void ReleaseGraphics(); // call before the Qt window destroys its wrapper

private:
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void ScrollCallback(GLFWwindow* window, double x_offset, double y_offset);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int modifiers);
    struct ModelEntry
    {
        std::unique_ptr<StlModel> model;
        StlModelInfo info;
        std::uint64_t selection_order = 0;
        QByteArray stl_data;
    };
    struct SavedModel
    {
        StlModelInfo info;
        QByteArray stl_data;
        std::uint64_t selection_order = 0;
        std::size_t index = 0;
    };
    enum class OperationKind { Import, Delete, Transform, PlateSize };
    struct Operation
    {
        OperationKind kind = OperationKind::Import;
        SavedModel model;
        ModelTransform before_transform;
        ModelTransform after_transform;
        glm::vec2 before_plate_size{PlateDimensions::kDefaultMm};
        glm::vec2 after_plate_size{PlateDimensions::kDefaultMm};
        std::uint64_t before_revision = 0;
        std::uint64_t after_revision = 0;
        QString description;
    };
    struct PendingTransform
    {
        ModelId id = 0;
        TransformMode mode = TransformMode::Translate;
        ModelTransform before_transform;
        std::uint64_t before_revision = 0;
    };
    SavedModel CaptureModel(const ModelEntry& entry) const;
    bool RestoreModel(const SavedModel& saved, QString& error);
    void EraseModel(ModelId id);
    bool ApplyTransform(ModelEntry& entry, const ModelTransform& transform);
    bool DrawModelGizmo(const glm::mat4& view, const glm::mat4& projection, bool over_cube);
    void RecordOperation(Operation operation);
    void AppendHistory(const QString& description);
    void ClearHistory();
    void AdvanceProjectRevision();
    ModelEntry* FindModel(ModelId id);
    const ModelEntry* FindModel(ModelId id) const;
    void ChooseActiveModel();
    void ResetProjectDisplay();
    void ApplyPlateSizeMm(const glm::vec2& size_mm);
    void RefreshSceneBounds();
    struct MouseRay
    {
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
    };
    struct ModelDrag
    {
        ModelId id = 0;
        glm::vec3 anchor{0.0f};
        glm::vec3 plane_normal{0.0f, 0.0f, 1.0f};
        glm::vec3 initial_translation_mm{0.0f};
    };
    std::optional<MouseRay> MouseRayAt(const glm::vec2& position,
                                     bool require_inside = true) const;
    ModelEntry* ClosestModel(const MouseRay& ray, glm::vec3* hit_point);
    void BeginModelDrag(ModelId id, const glm::vec2& position, const glm::vec3& hit_point);
    void UpdateModelDrag(const glm::vec2& position);
    void RefreshModelTransformInfo(ModelEntry& entry);
    glm::mat4 ProjectionMatrix(float aspect) const;
    void DrawSettings();
    void DrawModelBounds(const glm::mat4& view, const glm::mat4& projection);

    GLFWwindow* window_ = nullptr;
    void* previous_ime_context_ = nullptr;
    bool glfw_initialized_ = false;
    bool glad_ready_ = false;
    bool ime_detached_ = false;
    bool imgui_context_ready_ = false;
    bool imgui_glfw_ready_ = false;
    bool imgui_opengl_ready_ = false;
    bool graphics_released_ = false;
    bool initial_view_fitted_ = false;

    std::unique_ptr<Shader> stl_shader_;
    std::unique_ptr<Shader> plate_shader_;
    std::unique_ptr<BasePlate> base_plate_;
    glm::vec2 plate_size_mm_{PlateDimensions::kDefaultMm};
    unsigned int bounds_vao_ = 0;
    unsigned int bounds_vbo_ = 0;
    std::vector<ModelEntry> models_;
    ModelId next_model_id_ = 1;
    ModelId active_model_id_ = 0;
    std::uint64_t model_state_revision_ = 0;
    std::uint64_t next_selection_order_ = 1;
    std::uint64_t project_revision_ = 0;
    std::uint64_t next_project_revision_ = 1;
    std::vector<Operation> operations_;
    std::size_t operation_cursor_ = 0;
    std::vector<QString> operation_history_;
    std::uint64_t history_revision_ = 0;
    std::optional<PendingTransform> pending_transform_;
    ModelId transform_editing_model_id_ = 0;
    TransformMode transform_mode_ = TransformMode::Translate;
    bool gizmo_dragging_ = false;
    std::optional<glm::vec2> pending_pick_;
    std::optional<ModelDrag> model_drag_;
    ImFont* axis_label_font_ = nullptr;
    CameraController camera_controller_;

    bool show_demo_window_ = false;
    bool show_settings_ = false;
    bool wireframe_ = false;
    float clear_color_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

#endif
