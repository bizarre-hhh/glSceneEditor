#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QKeyEvent>
#include <QImage>
#include <QDockWidget>
#include <QMouseEvent>
#include <QListWidget>
#include <QLocale>
#include <QToolBar>
#include <QToolButton>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "ImGuizmo.h"
#include "project_file.h"
#include "recent_projects.h"
#include "scene_editor_window.h"
#include "scene_viewport.h"
#include "welcome_dialog.h"

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QByteArray ReadBytes(const QString& path)
{
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "read fixture");
    return file.readAll();
}

void WriteBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    Require(file.open(QIODevice::WriteOnly), "write fixture");
    Require(file.write(bytes) == bytes.size(), "write complete fixture");
}

bool SameModel(const ProjectModel& a, const ProjectModel& b)
{
    return a.source_path == b.source_path && a.stl_data == b.stl_data &&
        a.translation_mm == b.translation_mm && a.scale == b.scale &&
        a.rotation_degrees == b.rotation_degrees && a.placement_center_mm == b.placement_center_mm && a.name == b.name;
}

bool SameProject(const ProjectData& a, const ProjectData& b)
{
    if (a.models.size() != b.models.size() || a.plate_size_mm != b.plate_size_mm)
        return false;
    for (std::size_t i = 0; i < a.models.size(); ++i)
        if (!SameModel(a.models[i], b.models[i]))
            return false;
    return true;
}

// Fixture for the original format, including non-default presentation settings.
QByteArray LegacyProject(const ProjectData& project, quint32 version = 1)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_12);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << quint32(0x474C5052) << version;
    if (version == 1)
    {
        for (float value : {2.5f, -3.75f, 4.25f, 48.5f, 136.75f, 64.5f,
                           42.5f, 22.0f, 0.25f, 0.5f, 0.75f, 1.0f})
            stream << value;
        stream << quint8(1) << quint8(1);
    }
    if (version >= 3)
        stream << project.plate_size_mm.x << project.plate_size_mm.y;
    stream << quint32(project.models.size());
    for (const auto& model : project.models)
    {
        for (const QByteArray data : {model.source_path.toUtf8(), model.stl_data})
        {
            stream << quint32(data.size());
            stream.writeRawData(data.constData(), data.size());
        }
        for (const auto& vector : {model.translation_mm, model.scale, model.rotation_degrees})
            stream << vector.x << vector.y << vector.z;
        if (version >= 3)
            stream << model.placement_center_mm.x << model.placement_center_mm.y;
        if (version == 1)
            stream << quint8(0) << quint8(1) << quint8(1) << quint64(99);
    }
    return bytes;
}

bool SameCamera(const Camera& a, const Camera& b)
{
    return a.Target() == b.Target() && a.Distance() == b.Distance() &&
        a.Yaw() == b.Yaw() && a.Pitch() == b.Pitch() && a.Zoom() == b.Zoom() &&
        a.SceneRadius() == b.SceneRadius() && a.IsOrthographic() == b.IsOrthographic();
}

void ScrollViewport()
{
    auto* window = glfwGetCurrentContext();
    auto callback = glfwSetScrollCallback(window, nullptr);
    glfwSetScrollCallback(window, callback);
    Require(callback != nullptr, "scroll callback missing");
    callback(window, 0, 3);
}

int failures = 0;
void Run(const char* name, const std::function<void()>& test)
{
    try
    {
        test();
        std::cout << "[PASS] " << name << '\n';
    }
    catch (const std::exception& error)
    {
        ++failures;
        std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
    }
}

// UI tests use the real Windows foreign-window wrapper, with all Qt dialogs hidden.
class HideTestWindows : public QObject
{
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::Polish)
            if (auto* widget = qobject_cast<QWidget*>(object); widget && widget->isWindow())
                widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};

void WithDialog(const std::function<void(QDialog*)>& answer,
                const std::function<void()>& operation)
{
    bool answered = false;
    QTimer responder;
    responder.setInterval(10);
    QObject::connect(&responder, &QTimer::timeout, [&]()
    {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
        {
            responder.stop();
            answered = true;
            answer(dialog);
        }
    });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, []()
    {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    watchdog.start(5000);
    responder.start();
    operation();
    Require(answered, "expected modal dialog was not presented");
}

void WithFile(const QString& path, const std::function<void()>& operation)
{
    WithDialog([&](QDialog* dialog)
    {
        auto* file = qobject_cast<QFileDialog*>(dialog);
        Require(file != nullptr, "expected a file dialog");
        file->setDirectory(QFileInfo(path).absolutePath());
        file->selectFile(QFileInfo(path).fileName());
        QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
    }, operation);
}

void WithAnswer(QMessageBox::StandardButton answer, const std::function<void()>& operation)
{
    WithDialog([&](QDialog* dialog)
    {
        auto* box = qobject_cast<QMessageBox*>(dialog);
        Require(box && box->button(answer), "expected an unsaved-changes dialog");
        box->button(answer)->click();
    }, operation);
}

void Trigger(SceneEditorWindow& window, const char* name)
{
    auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
    Require(action != nullptr, "missing project action");
    action->trigger();
}

void FileTests(const QString& directory)
{
    QString error;
    const QString file = directory + QStringLiteral("/往返测试.glproj");
    ProjectData original;
    const QByteArray stl = ReadBytes(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl"));
    ProjectModel first;
    first.source_path = QStringLiteral("D:/已删除的文件夹/模型.stl");
    first.stl_data = stl;
    first.name = QStringLiteral("模型");
    first.translation_mm = {-123.4567f, 78.125f, 9.25f};
    first.scale = {1.2f, 0.8f, 2.0f};
    first.rotation_degrees = {13.25f, -35.75f, 82.5f};
    original.models.push_back(first);
    ProjectModel duplicate = first;
    duplicate.translation_mm = {13.5f, 72.5f, -9.5f};
    duplicate.name = QStringLiteral("模型 (2)");
    original.models.push_back(duplicate);
    Run("single-file roundtrip: duplicates, UTF-8 paths and transforms", [&]()
    {
        Require(ProjectFile::Write(file, original, error), qPrintable(error));
        ProjectData restored;
        Require(ProjectFile::Read(file, restored, error), qPrintable(error));
        Require(SameProject(original, restored), "saved document differs");
        Require(restored.models[0].stl_data == stl, "embedded STL changed");
    });
    Run("version 4 adds independent UTF-8 names without presentation fields", [&]()
    {
        const QByteArray bytes = ReadBytes(file);
        const QByteArray version3 = LegacyProject(original, 3);
        int name_bytes = 0;
        for (const auto& model : original.models) name_bytes += 4 + model.name.toUtf8().size();
        Require(static_cast<unsigned char>(bytes[4]) == 4, "new format version missing");
        Require(bytes.size() - version3.size() == name_bytes, "unexpected serialized fields");
    });
    Run("version 3 preserves placement and derives distinct names for repeated paths", [&]()
    {
        ProjectData sized = original;
        sized.plate_size_mm = {225, 175};
        sized.models[1].placement_center_mm = {112.5f, 87.5f};
        const QString path = directory + "/version3.glproj";
        WriteBytes(path, LegacyProject(sized, 3));
        ProjectData restored;
        Require(ProjectFile::Read(path, restored, error), qPrintable(error));
        Require(SameProject(sized, restored), "version 3 lost geometry or name migration");
    });
    Run("version 2 compatibility defaults platform to 100 mm and preserves model positions", [&]()
    {
        const QString path = directory + "/version2.glproj";
        WriteBytes(path, LegacyProject(original, 2));
        ProjectData restored;
        Require(ProjectFile::Read(path, restored, error), qPrintable(error));
        Require(SameProject(original, restored), "version 2 compatibility lost geometry or default platform");
    });
    Run("platform dimensions and independent model placement survive file roundtrip", [&]()
    {
        ProjectData sized = original;
        sized.plate_size_mm = {250.75f, 160.5f};
        sized.models[1].placement_center_mm = {125.375f, 80.25f};
        const QString path = directory + "/sized.glproj";
        Require(ProjectFile::Write(path, sized, error), qPrintable(error));
        ProjectData restored;
        Require(ProjectFile::Read(path, restored, error), qPrintable(error));
        Require(SameProject(sized, restored), "platform or placement lost in roundtrip");
    });
    Run("legacy version 1 preserves geometry and ignores presentation", [&]()
    {
        const QString legacy = directory + "/legacy.glproj";
        WriteBytes(legacy, LegacyProject(original));
        ProjectData restored;
        Require(ProjectFile::Read(legacy, restored, error), qPrintable(error));
        Require(SameProject(original, restored), "legacy geometry not preserved");
    });
    Run("empty project roundtrip", [&]()
    {
        ProjectData empty, restored;
        const QString path = directory + QStringLiteral("/empty.glproj");
        Require(ProjectFile::Write(path, empty, error), qPrintable(error));
        Require(ProjectFile::Read(path, restored, error), qPrintable(error));
        Require(SameProject(empty, restored), "empty document differs");
    });
    Run("malformed, truncated and oversized files preserve caller document", [&]()
    {
        const QByteArray bytes = ReadBytes(file);
        for (int size : {0, 4, 8, 30, bytes.size() - 1})
        {
            WriteBytes(directory + "/bad.glproj", bytes.left(size));
            ProjectData previous = original;
            Require(!ProjectFile::Read(directory + "/bad.glproj", previous, error), "truncation accepted");
            Require(SameProject(previous, original), "failed read replaced document");
        }
        QByteArray invalid = bytes;
        invalid[4] = 5; // Unsupported format version.
        WriteBytes(directory + "/bad.glproj", invalid);
        ProjectData previous = original;
        Require(!ProjectFile::Read(directory + "/bad.glproj", previous, error), "newer version accepted");
        invalid = bytes;
        for (int i = 20; i < 24; ++i) // First path's byte length after dimensions and model count.
            invalid[i] = static_cast<char>(0xff);
        WriteBytes(directory + "/bad.glproj", invalid);
        Require(!ProjectFile::Read(directory + "/bad.glproj", previous, error), "oversized length accepted");
        WriteBytes(directory + "/bad.glproj", bytes + "extra");
        Require(!ProjectFile::Read(directory + "/bad.glproj", previous, error), "trailing corruption accepted");
    });
    Run("invalid numeric data and write failures preserve existing project", [&]()
    {
        const QByteArray before = ReadBytes(file);
        ProjectData invalid = original;
        invalid.models[0].translation_mm.x = std::numeric_limits<float>::quiet_NaN();
        Require(!ProjectFile::Write(file, invalid, error), "NaN accepted");
        Require(ReadBytes(file) == before, "failed write changed existing file");
        invalid = original;
        invalid.plate_size_mm.x = 0;
        Require(!ProjectFile::Write(file, invalid, error), "invalid platform accepted");
        Require(ReadBytes(file) == before, "invalid platform overwrote existing project");
        invalid = original;
        invalid.models[0].placement_center_mm.y = std::numeric_limits<float>::quiet_NaN();
        Require(!ProjectFile::Write(file, invalid, error), "invalid placement accepted");
        invalid = original;
        invalid.models[0].scale.y = 0;
        Require(!ProjectFile::Write(file, invalid, error), "singular transform accepted");
        Require(!ProjectFile::Write(directory + "/missing/path.glproj", original, error), "invalid destination accepted");
        Require(ReadBytes(file) == before, "original file lost");
    });
}

void SceneTests(const QString& directory)
{
    SceneViewport viewport;
    Require(viewport.Initialize(), "initialize hidden OpenGL viewport");
    QString error;
    std::string load_error;
    glm::vec3 size;
    const QString source = directory + QStringLiteral("/中文模型.stl");
    WriteBytes(source, ReadBytes(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl")));
    ProjectData saved;
    std::vector<SceneViewport::StlModelInfo> before;
    Run("import Unicode STL and keep two instances independent", [&]()
    {
        Require(viewport.LoadStl(source.toUtf8().toStdString(), load_error, size), load_error.c_str());
        Require(viewport.LoadStl(source.toUtf8().toStdString(), load_error, size), load_error.c_str());
        Require(viewport.ModelCount() == 2, "duplicate STL collapsed");
        saved = viewport.SnapshotProject();
        saved.models[0].translation_mm = {-123.4567f, 68.1234f, 7.5f};
        saved.models[0].scale = {1.25f, 0.75f, 1.5f};
        saved.models[0].rotation_degrees = {15.5f, -23.75f, 45.125f};
        saved.models[1].translation_mm = {123.4567f, -68.1234f, 0};
        Require(viewport.ApplyProject(saved, error), qPrintable(error));
        Require(SameProject(saved, viewport.SnapshotProject()), "application changed project state");
        before = viewport.ModelInfos();
        Require(before[0].id != before[1].id, "model IDs are not distinct");
        const auto revision = viewport.ProjectRevision();
        viewport.SetModelVisible(before[0].id, false);
        viewport.SetModelBoundingBoxVisible(before[1].id, true);
        viewport.SetModelSelected(before[0].id, true);
        Require(viewport.RenderFrame(), "render fixture");
        ScrollViewport();
        Require(viewport.ProjectRevision() == revision, "display or camera dirtied document");
        Require(SameProject(saved, viewport.SnapshotProject()), "presentation entered document");
    });
    Run("save, delete original STL, reopen and preserve exact world positions", [&]()
    {
        const QString path = directory + QStringLiteral("/独立工程.glproj");
        Require(ProjectFile::Write(path, viewport.SnapshotProject(), error), qPrintable(error));
        Require(QFile::remove(source), "remove source STL");
        viewport.NewProject();
        Require(viewport.ModelCount() == 0, "new project retained models");
        ProjectData loaded;
        Require(ProjectFile::Read(path, loaded, error), qPrintable(error));
        Require(viewport.ApplyProject(loaded, error), qPrintable(error));
        Require(SameProject(saved, viewport.SnapshotProject()), "scene did not roundtrip");
        const auto restored = viewport.ModelInfos();
        for (std::size_t i = 0; i < restored.size(); ++i)
        {
            Require(restored[i].world_position_mm == before[i].world_position_mm, "world position changed");
            Require(restored[i].world_bounds_min_mm == before[i].world_bounds_min_mm, "minimum bounds changed");
            Require(restored[i].world_bounds_max_mm == before[i].world_bounds_max_mm, "maximum bounds changed");
            Require(restored[i].source_file_size == before[i].source_file_size, "source size lost");
        }
        for (const auto& model : restored)
            Require(model.visible && !model.bounding_box_visible && !model.selected,
                    "presentation not reset on open");
        Require(viewport.RenderFrame(), "first restored frame failed");
        const Camera opened = viewport.GetCamera();
        viewport.ResetView();
        Require(SameCamera(opened, viewport.GetCamera()), "open camera differs from Home");
        Require(opened.Yaw() == -90.0f && opened.Pitch() == 35.0f && !opened.IsOrthographic(),
                "open did not use default Home direction");
    });
    Run("bad embedded STL preserves current scene, model IDs and dirty revision", [&]()
    {
        const auto current = viewport.SnapshotProject();
        const auto infos = viewport.ModelInfos();
        const auto revision = viewport.ProjectRevision();
        ProjectData corrupt = current;
        corrupt.models[1].stl_data = "broken model data";
        Require(!viewport.ApplyProject(corrupt, error), "corrupt embedded STL accepted");
        Require(SameProject(current, viewport.SnapshotProject()), "failed restore destroyed scene");
        Require(viewport.ModelInfos()[0].id == infos[0].id, "failed restore changed IDs");
        Require(viewport.ProjectRevision() == revision, "failed restore marked scene modified");
    });
    Run("selection, visibility, bounds and camera changes do not dirty project", [&]()
    {
        const auto infos = viewport.ModelInfos();
        const auto revision = viewport.ProjectRevision();
        viewport.SetModelSelected(infos[0].id, true);
        viewport.SetModelSelected(infos[1].id, true);
        viewport.SetModelVisible(infos[0].id, false);
        viewport.SetModelBoundingBoxVisible(infos[1].id, true);
        ScrollViewport();
        Require(viewport.CurrentModelInfo()->id == infos[1].id, "selection rank not advanced");
        Require(viewport.ProjectRevision() == revision, "presentation marked document modified");
    });
    Run("legacy project opens with visible models, no boxes and Home camera", [&]()
    {
        const QString path = directory + "/legacy-scene.glproj";
        WriteBytes(path, LegacyProject(saved));
        ProjectData legacy;
        Require(ProjectFile::Read(path, legacy, error), qPrintable(error));
        Require(viewport.ApplyProject(legacy, error), qPrintable(error));
        Require(SameProject(saved, viewport.SnapshotProject()), "legacy transforms changed");
        for (const auto& info : viewport.ModelInfos())
            Require(info.visible && !info.bounding_box_visible && !info.selected,
                    "legacy display settings restored");
        Require(viewport.RenderFrame(), "legacy first frame");
        const Camera opened = viewport.GetCamera();
        viewport.ResetView();
        Require(SameCamera(opened, viewport.GetCamera()), "legacy camera differs from Home");
    });
    Run("new project clears models, display settings and selection", [&]()
    {
        viewport.NewProject();
        const auto project = viewport.SnapshotProject();
        Require(project.models.empty() && !viewport.GetCamera().IsOrthographic(),
                "new project retains models or projection");
        Require(!viewport.CurrentModelInfo(), "new project retains active model");
        Require(viewport.GetCamera().Yaw() == -90.0f && viewport.GetCamera().Pitch() == 35.0f,
                "new project not using Home");
    });
}


void TransformTests(const QString& directory)
{
    SceneViewport viewport;
    Require(viewport.Initialize(), "initialize transform viewport");
    QString error;
    std::string load_error;
    glm::vec3 size;
    const auto load = [&]()
    {
        viewport.NewProject();
        Require(viewport.LoadStl(TEST_ASSET_DIR "/cube_30mm.stl", load_error, size), load_error.c_str());
        return viewport.ModelInfos()[0].id;
    };
    const auto vectors_close = [](const glm::vec3& a, const glm::vec3& b)
    { return glm::length(a - b) < 0.005f; };
    const auto transform_of = [](const SceneViewport::StlModelInfo& info)
    { return ModelTransform{info.local_translation_mm, info.local_scale, info.local_rotation_degrees}; };
    Run("hidden bounds follow numeric and gizmo scaling before rendering and through undo redo", [&]()
    {
        const auto id = load();
        const auto original = *viewport.ModelInfo(id);
        Require(!original.bounding_box_visible, "bounds fixture must start hidden");
        ModelTransform transform;
        transform.scale = {2, 0.5f, 1.5f};
        Require(viewport.TransformModelTo(id, transform, TransformMode::Scale), "scale with hidden bounds");
        const auto scaled = *viewport.ModelInfo(id);
        Require(vectors_close(scaled.world_bounds_min_mm, {20, 42.5f, -7.5f}) &&
                vectors_close(scaled.world_bounds_max_mm, {80, 57.5f, 37.5f}), "hidden bounds retained original size");
        const auto history = viewport.HistoryRevision();
        viewport.SetModelBoundingBoxVisible(id, true);
        Require(viewport.RenderFrame(), "render bounds after hidden scaling");
        Require(viewport.ModelInfo(id)->world_bounds_min_mm == scaled.world_bounds_min_mm &&
                viewport.ModelInfo(id)->world_bounds_max_mm == scaled.world_bounds_max_mm &&
                viewport.HistoryRevision() == history, "showing bounds changed geometry or history");
        viewport.SetModelBoundingBoxVisible(id, false);
        viewport.SetModelVisible(id, false);
        auto matrix = *viewport.ModelGizmoMatrix(id);
        matrix = glm::translate(glm::mat4(1), glm::vec3(matrix[3])) * glm::scale(glm::mat4(1), {0.5f, 3, 2});
        Require(viewport.TransformModelFromGizmo(id, matrix, TransformMode::Scale), "scale hidden model from gizmo");
        const auto gizmo_scaled = *viewport.ModelInfo(id);
        Require(vectors_close(gizmo_scaled.world_bounds_min_mm, {42.5f, 5, -15}) &&
                vectors_close(gizmo_scaled.world_bounds_max_mm, {57.5f, 95, 45}), "hidden gizmo bounds not refreshed");
        Require(viewport.Undo(error), qPrintable(error));
        Require(viewport.ModelInfo(id)->world_bounds_min_mm == scaled.world_bounds_min_mm &&
                viewport.ModelInfo(id)->world_bounds_max_mm == scaled.world_bounds_max_mm, "hidden bounds not restored by undo");
        Require(viewport.Redo(error), qPrintable(error));
        Require(viewport.ModelInfo(id)->world_bounds_min_mm == gizmo_scaled.world_bounds_min_mm &&
                viewport.ModelInfo(id)->world_bounds_max_mm == gizmo_scaled.world_bounds_max_mm, "hidden bounds not restored by redo");
        viewport.SetModelVisible(id, true);
        viewport.SetModelBoundingBoxVisible(id, true);
        Require(viewport.RenderFrame(), "render restored scaled bounds");
        Require(viewport.ModelInfo(id)->world_bounds_min_mm == gizmo_scaled.world_bounds_min_mm &&
                viewport.ModelInfo(id)->world_bounds_max_mm == gizmo_scaled.world_bounds_max_mm, "reshown bounds are stale");
    });
    Run("gizmo centers match existing placement for rotated and scaled legacy transforms", [&]()
    {
        const auto id = load();
        for (const glm::vec3 angles : {glm::vec3(20, -35, 57), glm::vec3(10, 90, 45), glm::vec3(10, -90, -45)})
        {
            ModelTransform transform{{12, -7, 3}, {1.2f, 0.7f, 2}, angles};
            Require(viewport.TransformModelTo(id, transform, TransformMode::Rotate), "apply transform fixture");
            const auto matrix = viewport.ModelGizmoMatrix(id);
            Require(matrix && vectors_close(glm::vec3((*matrix)[3]) * 10.0f, viewport.ModelInfo(id)->world_position_mm),
                    "gizmo origin disagrees with model center");
            auto restored = transform;
            Require(ModelTransformMath::FromGizmo(*matrix, {5, 5, 1.5f}, TransformMode::Rotate, restored), "decompose rotation");
            const auto before = ModelTransformMath::Rotation(angles);
            const auto after = ModelTransformMath::Rotation(restored.rotation_degrees);
            for (int axis = 0; axis < 3; ++axis)
                Require(vectors_close(glm::vec3(before[axis]), glm::vec3(after[axis])), "Euler conversion changed orientation");
        }
    });
    Run("all three gizmo transformations preserve unrelated parameters and undo full pose", [&]()
    {
        const auto id = load();
        ModelTransform fixture{{12, -7, 3}, {1.2f, 0.7f, 2}, {20, -35, 57}};
        Require(viewport.TransformModelTo(id, fixture, TransformMode::Translate), "apply transform fixture");
        for (auto mode : {TransformMode::Translate, TransformMode::Rotate, TransformMode::Scale})
        {
            const auto before = *viewport.ModelInfo(id);
            const auto history = viewport.OperationHistory().size();
            auto matrix = *viewport.ModelGizmoMatrix(id);
            if (mode == TransformMode::Translate)
                matrix[3] += glm::vec4(2, -1, 3, 0);
            else if (mode == TransformMode::Rotate)
                matrix = glm::translate(glm::mat4(1), glm::vec3(matrix[3])) *
                    ModelTransformMath::Rotation({-15, 67, 112}) * glm::scale(glm::mat4(1), before.local_scale);
            else
                matrix = glm::translate(glm::mat4(1), glm::vec3(matrix[3])) *
                    ModelTransformMath::Rotation(before.local_rotation_degrees) * glm::scale(glm::mat4(1), {2, 3, 0.5f});
            Require(viewport.TransformModelFromGizmo(id, matrix, mode), "apply gizmo matrix");
            const auto after = *viewport.ModelInfo(id);
            Require(viewport.OperationHistory().size() == history + 1, "gizmo step history missing");
            if (mode == TransformMode::Translate)
                Require(vectors_close(after.world_position_mm, before.world_position_mm + glm::vec3(20, -10, 30)) &&
                        after.local_scale == before.local_scale && after.local_rotation_degrees == before.local_rotation_degrees,
                        "gizmo translation changed other components");
            else
                Require(vectors_close(after.world_position_mm, before.world_position_mm), "rotate/scale moved model center");
            Require(viewport.Undo(error), qPrintable(error));
            Require(transform_of(*viewport.ModelInfo(id)) == transform_of(before), "undo did not restore full transform");
            Require(viewport.Redo(error), qPrintable(error));
            Require(transform_of(*viewport.ModelInfo(id)) == transform_of(after), "redo did not restore full transform");
        }
        const auto saved = viewport.SnapshotProject();
        const auto expected = *viewport.ModelInfo(id);
        const QString path = directory + "/gizmo-transforms.glproj";
        Require(ProjectFile::Write(path, saved, error), qPrintable(error));
        ProjectData loaded;
        Require(ProjectFile::Read(path, loaded, error), qPrintable(error));
        viewport.NewProject();
        Require(viewport.ApplyProject(loaded, error), qPrintable(error));
        Require(SameProject(saved, viewport.SnapshotProject()) &&
                viewport.ModelInfos()[0].world_bounds_min_mm == expected.world_bounds_min_mm &&
                viewport.ModelInfos()[0].world_bounds_max_mm == expected.world_bounds_max_mm, "transformed project changed on reopen");
    });
    Run("multi-frame rotate and scale gestures merge and no-op gestures make no records", [&]()
    {
        const auto id = load();
        for (auto mode : {TransformMode::Rotate, TransformMode::Scale})
        {
            const auto before = transform_of(*viewport.ModelInfo(id));
            const auto history = viewport.OperationHistory().size();
            Require(viewport.BeginModelTransform(id, mode), "begin transform gesture");
            auto next = before;
            for (int step = 1; step <= 8; ++step)
            {
                if (mode == TransformMode::Rotate) next.rotation_degrees = {0, 0, step * 10.0f};
                else next.scale = glm::vec3(1 + step * 0.1f);
                Require(viewport.TransformModelTo(id, next, mode), "gesture update failed");
            }
            Require(viewport.OperationHistory().size() == history, "intermediate frame recorded");
            viewport.EndModelTransform();
            Require(viewport.OperationHistory().size() == history + 1, "gesture not merged");
            const auto unchanged = viewport.HistoryRevision();
            Require(viewport.BeginModelTransform(id, mode), "begin no-op gesture");
            viewport.EndModelTransform();
            Require(viewport.HistoryRevision() == unchanged, "stationary gesture dirty");
            Require(viewport.BeginModelTransform(id, mode), "begin returning gesture");
            Require(viewport.TransformModelTo(id, before, mode) && viewport.TransformModelTo(id, next, mode), "returning gesture");
            viewport.EndModelTransform();
            Require(viewport.HistoryRevision() == unchanged, "returning gesture dirty");
            Require(viewport.Undo(error) && transform_of(*viewport.ModelInfo(id)) == before, "gesture undo failed");
        }
    });
    Run("invalid transforms leave geometry and history unchanged", [&]()
    {
        const auto id = load();
        const auto saved = viewport.SnapshotProject();
        const auto history = viewport.HistoryRevision();
        for (float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        {
            ModelTransform transform;
            transform.scale.y = invalid;
            Require(!viewport.TransformModelTo(id, transform, TransformMode::Scale), "invalid scale accepted");
        }
        ModelTransform transform;
        transform.rotation_degrees.x = std::numeric_limits<float>::infinity();
        Require(!viewport.TransformModelTo(id, transform, TransformMode::Rotate), "invalid rotation accepted");
        Require(viewport.HistoryRevision() == history && SameProject(saved, viewport.SnapshotProject()), "invalid transform changed scene");
    });
    Run("editing session freezes selection of every model and unlocks without changing history", [&]()
    {
        const auto id = load();
        Require(viewport.LoadStl(TEST_ASSET_DIR "/sphere_30mm.stl", load_error, size), "load selected peer");
        const auto peer = viewport.ModelInfos()[1].id;
        Require(viewport.LoadStl(TEST_ASSET_DIR "/torus_30mm.stl", load_error, size), "load unselected peer");
        const auto unselected = viewport.ModelInfos()[2].id;
        viewport.SetModelSelected(peer, true);
        viewport.SetModelSelected(id, true);
        const auto history = viewport.HistoryRevision();
        Require(!viewport.IsTransformEditing(), "ordinary selection enabled gizmo");
        Require(viewport.BeginTransformEditing(id, TransformMode::Translate), "open edit session");
        const auto state = viewport.ModelStateRevision();
        viewport.SetModelSelected(id, false);
        viewport.SetModelSelected(peer, false);
        viewport.SetModelSelected(unselected, true);
        viewport.ClearModelSelection();
        Require(!viewport.SelectModelAt(1, 1), "locked viewport accepted picking");
        Require(viewport.ModelInfo(id)->selected && viewport.ModelInfo(peer)->selected &&
                !viewport.ModelInfo(unselected)->selected && viewport.CurrentModelInfo()->id == id &&
                viewport.ModelStateRevision() == state, "locked selection changed");
        Require(!viewport.BeginTransformEditing(peer, TransformMode::Rotate), "locked edit changed target");
        Require(viewport.BeginTransformEditing(id, TransformMode::Rotate) &&
                viewport.GetTransformMode() == TransformMode::Rotate, "mode switch failed while locked");
        viewport.EndTransformEditing();
        Require(!viewport.IsTransformEditing() && viewport.HistoryRevision() == history, "opening/closing dirtied project");
        viewport.SetModelSelected(unselected, true);
        viewport.SetModelSelected(peer, false);
        Require(viewport.ModelInfo(unselected)->selected && !viewport.ModelInfo(peer)->selected,
                "closing did not restore selection changes");
        viewport.ClearModelSelection();
        for (const auto& info : viewport.ModelInfos()) Require(!info.selected, "clear selection stayed locked");
        viewport.SetModelSelected(id, true);
        Require(viewport.BeginTransformEditing(id, TransformMode::Scale), "reopen edit session");
        viewport.RemoveStl(id);
        Require(!viewport.IsTransformEditing(), "deleted target left editing locked");
        viewport.SetModelSelected(peer, true);
        Require(viewport.ModelInfo(peer)->selected, "target deletion left selection locked");
    });
    Run("real gizmo mouse drag moves the selected model and records a single operation", [&]()
    {
        const auto id = load();
        viewport.SetModelSelected(id, true);
        Require(viewport.BeginTransformEditing(id, TransformMode::Translate), "open gizmo session");
        auto& io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        const auto frame = [&](const ImVec2& point, bool down)
        {
            io.AddFocusEvent(true);
            io.AddMousePosEvent(point.x, point.y);
            io.AddMouseButtonEvent(0, down);
            Require(viewport.RenderFrame(), "synthetic mouse frame failed");
        };
        frame({-100, -100}, false);
        const Camera camera = viewport.GetCamera();
        const auto frame_matrix = *viewport.ModelGizmoMatrix(id);
        const auto vp = camera.GetProjectionMatrix(io.DisplaySize.x / io.DisplaySize.y) * camera.GetViewMatrix();
        const auto project = [&](const glm::vec3& world)
        {
            const auto clip = vp * glm::vec4(world, 1);
            return ImVec2((clip.x / clip.w * 0.5f + 0.5f) * io.DisplaySize.x,
                          (0.5f - clip.y / clip.w * 0.5f) * io.DisplaySize.y);
        };
        ImVec2 handle;
        bool found = false;
        for (float distance = 0.4f; distance < 8 && !found; distance += 0.15f)
        {
            handle = project(glm::vec3(frame_matrix[3]) + glm::vec3(distance, 0, 0));
            frame(handle, false);
            found = ImGuizmo::GetHoveredHandleType() == ImGuizmo::MT_MOVE_X;
        }
        Require(found, "could not hover X gizmo handle");
        const auto before = *viewport.ModelInfo(id);
        const auto history = viewport.HistoryRevision();
        frame(handle, true);
        Require(ImGuizmo::IsUsingAny(), "gizmo did not capture click");
        frame({handle.x + 25, handle.y}, true);
        frame({handle.x + 55, handle.y}, true);
        frame({handle.x + 55, handle.y}, false);
        Require(!ImGuizmo::IsUsingAny() && viewport.HistoryRevision() == history + 1, "gizmo gesture not committed once");
        Require(viewport.ModelInfo(id)->world_position_mm.x > before.world_position_mm.x + 1 &&
                vectors_close(glm::vec3(0, viewport.ModelInfo(id)->world_position_mm.y, viewport.ModelInfo(id)->world_position_mm.z),
                     glm::vec3(0, before.world_position_mm.y, before.world_position_mm.z)), "axis drag changed wrong coordinates");
        Require(SameCamera(camera, viewport.GetCamera()), "gizmo drag moved camera");
        Require(viewport.Undo(error) && viewport.ModelInfo(id)->world_position_mm == before.world_position_mm,
                "mouse gizmo undo failed");
        frame({-100, -100}, false);
        viewport.EndTransformEditing();
        Require(viewport.LoadStl(TEST_ASSET_DIR "/cube_30mm.stl", load_error, size), "import second drag target");
        const auto other = viewport.ModelInfos()[1].id;
        viewport.SetModelSelected(other, true);
        viewport.SetModelSelected(id, true);
        const auto other_before = transform_of(*viewport.ModelInfo(other));
        Require(viewport.BeginTransformEditing(id, TransformMode::Translate), "reopen gizmo session");
        frame(handle, false);
        frame(handle, true);
        Require(ImGuizmo::IsUsingAny(), "begin interrupted drag");
        frame({handle.x + 20, handle.y}, true);
        const auto interruption_history = viewport.HistoryRevision();
        viewport.SetModelSelected(id, false);
        Require(viewport.ModelInfo(id)->selected && ImGuizmo::IsUsingAny() &&
                viewport.HistoryRevision() == interruption_history, "locked selection interrupted gizmo drag");
        viewport.EndTransformEditing();
        Require(!ImGuizmo::IsUsingAny() && viewport.HistoryRevision() == interruption_history + 1,
                "closing edit session did not finish active drag");
        frame({handle.x + 45, handle.y}, true);
        frame({handle.x + 45, handle.y}, false);
        Require(transform_of(*viewport.ModelInfo(other)) == other_before, "ongoing drag leaked to another selected model");
        frame(handle, false);
        frame(handle, true);
        Require(!ImGuizmo::IsUsingAny() && ImGuizmo::GetHoveredHandleType() == ImGuizmo::MT_NONE,
                "gizmo was usable after parameter session closed");
        frame({-100, -100}, false);
    });
}

void PlatePlacementTests(const QString& directory)
{
    SceneViewport viewport;
    Require(viewport.Initialize(), "initialize plate placement viewport");
    QString error;
    const auto load = [&](const char* name)
    {
        std::string load_error;
        glm::vec3 size;
        const QString path = QStringLiteral(TEST_ASSET_DIR) + '/' + QString::fromLatin1(name);
        Require(viewport.LoadStl(path.toUtf8().toStdString(), load_error, size), load_error.c_str());
        return viewport.ModelInfos().back().id;
    };
    Run("place selected models on plate independently with world XY and pose preserved", [&]()
    {
        viewport.NewProject();
        const auto cone = load("cone_30mm.stl");
        const auto cube = load("cube_30mm.stl");
        const auto torus = load("torus_30mm.stl");
        const auto untouched = load("sphere_30mm.stl");
        Require(viewport.TransformModelTo(cone, {{0, 0, 0}, {1, 1, 1}, {0, 135, 0}}, TransformMode::Rotate), "tilt asymmetric cone");
        const float expected_tip_z = 15.0f * (1.0f + std::cos(glm::radians(135.0f)));
        Require(std::abs(viewport.ModelInfo(cone)->world_bounds_min_mm.z - expected_tip_z) < 0.001f,
                "rotated bounds use empty box corners instead of actual cone tip");
        Require(viewport.TransformModelTo(cube, {{12, -8, 45}, {2, 0.5f, 1.25f}, {20, 30, 60}}, TransformMode::Rotate), "place cube above plate");
        Require(viewport.TransformModelTo(torus, {{-20, 10, -55}, {0.8f, 1.3f, 1.5f}, {35, -40, 15}}, TransformMode::Rotate), "place torus below plate");
        for (const auto id : {cone, cube, torus}) viewport.SetModelSelected(id, true);
        viewport.SetModelVisible(torus, false);
        viewport.SetModelBoundingBoxVisible(cube, true);
        const auto before = viewport.SnapshotProject();
        const auto infos = viewport.ModelInfos();
        const auto history = viewport.HistoryRevision();
        const auto revision = viewport.ProjectRevision();
        const Camera camera = viewport.GetCamera();
        Require(viewport.PlaceSelectedModelsOnPlate(error), qPrintable(error));
        const auto grounded = viewport.ModelInfos();
        for (std::size_t i = 0; i < 3; ++i)
        {
            Require(grounded[i].world_bounds_min_mm.z == 0 &&
                    glm::length(glm::vec2(grounded[i].world_bounds_min_mm - infos[i].world_bounds_min_mm)) < 0.001f &&
                    glm::length(glm::vec2(grounded[i].world_bounds_max_mm - infos[i].world_bounds_max_mm)) < 0.001f &&
                    glm::length(glm::vec2(grounded[i].world_position_mm - infos[i].world_position_mm)) < 0.001f,
                    "plate placement did not zero minimum Z or changed world XY");
            Require(grounded[i].local_scale == infos[i].local_scale &&
                    grounded[i].local_rotation_degrees == infos[i].local_rotation_degrees &&
                    grounded[i].selected == infos[i].selected && grounded[i].visible == infos[i].visible &&
                    grounded[i].bounding_box_visible == infos[i].bounding_box_visible, "plate placement changed pose/presentation");
        }
        // The cone's real tip is its lowest vertex at this angle; it must touch the plate.
        const glm::vec3 tip = glm::vec3(*viewport.ModelGizmoMatrix(cone) * glm::vec4(0, 0, 1.5f, 1)) * 10.0f;
        Require(std::abs(tip.z) < 0.001f, "real cone tip still floats above the plate");
        Require(SameModel(before.models[3], viewport.SnapshotProject().models[3]) &&
                viewport.ModelInfo(untouched)->world_position_mm == infos[3].world_position_mm, "unselected model moved");
        Require(viewport.HistoryRevision() == history + 1 && viewport.OperationHistory().back().startsWith(QStringLiteral("贴合基板")) &&
                viewport.CurrentModelInfo()->id == torus, "placement not one history step or changed active selection");
        Require(viewport.GetCamera().Target() == camera.Target() && viewport.GetCamera().Distance() == camera.Distance(), "placement moved camera");
        const auto expected = viewport.SnapshotProject();
        Require(viewport.Undo(error) && SameProject(before, viewport.SnapshotProject()) && viewport.ProjectRevision() == revision,
                "batch placement undo did not restore original poses");
        Require(viewport.Redo(error) && SameProject(expected, viewport.SnapshotProject()), "batch placement redo not exact");
        const auto settled_history = viewport.HistoryRevision();
        Require(viewport.PlaceSelectedModelsOnPlate(error) && viewport.HistoryRevision() == settled_history,
                "already grounded models added history");
        const QString project_path = directory + QStringLiteral("/贴合基板.glproj");
        ProjectData project;
        Require(ProjectFile::Write(project_path, expected, error) && ProjectFile::Read(project_path, project, error) &&
                viewport.ApplyProject(project, error) && SameProject(expected, viewport.SnapshotProject()), "grounded project roundtrip");
        for (std::size_t i = 0; i < 3; ++i)
            Require(viewport.ModelInfos()[i].world_bounds_min_mm.z == 0, "reopened model no longer on plate");
    });
    Run("empty or locked selection and already grounded no-op preserve history and redo", [&]()
    {
        viewport.NewProject();
        const auto id = load("cube_30mm.stl");
        const auto history = viewport.HistoryRevision();
        Require(!viewport.PlaceSelectedModelsOnPlate(error) && !error.isEmpty() && viewport.HistoryRevision() == history,
                "empty placement changed history");
        viewport.SetModelSelected(id, true);
        Require(viewport.PlaceSelectedModelsOnPlate(error) && viewport.HistoryRevision() == history, "grounded import moved");
        Require(viewport.MoveModelTo(id, {10, -5, 20}) && viewport.Undo(error), "prepare redo for placement no-op");
        const auto undo_history = viewport.HistoryRevision();
        Require(viewport.PlaceSelectedModelsOnPlate(error) && viewport.CanRedo() && viewport.HistoryRevision() == undo_history,
                "placement no-op cleared redo");
        Require(viewport.BeginTransformEditing(id, TransformMode::Translate), "begin locked placement fixture");
        Require(!viewport.PlaceSelectedModelsOnPlate(error) && viewport.HistoryRevision() == undo_history, "placement changed locked model");
        viewport.EndTransformEditing();
    });
    Run("array copies can be grounded and the whole placement undone separately", [&]()
    {
        viewport.NewProject();
        const auto id = load("cube_30mm.stl");
        Require(viewport.MoveModelTo(id, {4, 8, 30}), "raise array source");
        viewport.SetModelSelected(id, true);
        Require(viewport.ArraySelectedModels(2, 2, {5, 5}, error), qPrintable(error));
        const auto elevated = viewport.SnapshotProject();
        Require(viewport.PlaceSelectedModelsOnPlate(error), qPrintable(error));
        const auto infos = viewport.ModelInfos();
        Require(infos[0].world_bounds_min_mm.z == 30, "grounding copies moved unselected original");
        for (std::size_t i = 1; i < infos.size(); ++i) Require(infos[i].world_bounds_min_mm.z == 0, "array copy not grounded");
        Require(viewport.Undo(error) && SameProject(elevated, viewport.SnapshotProject()), "placement undo also removed array");
        Require(viewport.Undo(error) && viewport.ModelCount() == 1, "array undo after placement failed");
        Require(viewport.Redo(error) && viewport.Redo(error) && viewport.ModelInfos().back().world_bounds_min_mm.z == 0,
                "array and placement redo sequence failed");
    });
}

void DuplicationTests(const QString& directory)
{
    SceneViewport viewport;
    Require(viewport.Initialize(), "initialize duplication viewport");
    QString error;
    const auto load = [&](const QString& source)
    {
        std::string load_error;
        glm::vec3 size;
        Require(viewport.LoadStl(source.toUtf8().toStdString(), load_error, size), load_error.c_str());
        return viewport.ModelInfos().back().id;
    };
    Run("duplicate preserves pose, offsets along world X and restores selection in one undo", [&]()
    {
        viewport.NewProject();
        const auto id = load(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl"));
        Require(viewport.TransformModelTo(id, {{12, -8, 4}, {2, 0.5f, 1.25f}, {0, 0, 90}}, TransformMode::Rotate), "prepare rotated duplicate");
        viewport.SetModelSelected(id, true);
        viewport.SetModelVisible(id, false);
        viewport.SetModelBoundingBoxVisible(id, true);
        const auto source = *viewport.ModelInfo(id);
        const auto before = viewport.SnapshotProject();
        const auto revision = viewport.ProjectRevision();
        const auto history = viewport.HistoryRevision();
        const auto camera = viewport.GetCamera();
        Require(viewport.DuplicateSelectedModels(error), qPrintable(error));
        const auto infos = viewport.ModelInfos();
        Require(infos.size() == 2 && infos[1].id != id && !infos[0].selected && infos[1].selected &&
                infos[1].visible && infos[1].bounding_box_visible, "duplicate identity/selection/presentation");
        const auto& copy = infos[1];
        const glm::vec3 expected(source.world_bounds_max_mm.x - source.world_bounds_min_mm.x + 5, 0, 0);
        Require(glm::length(copy.world_position_mm - source.world_position_mm - expected) < 0.001f &&
                copy.local_rotation_degrees == source.local_rotation_degrees && copy.local_scale == source.local_scale,
                "duplicate changed pose or used local instead of world X");
        Require(viewport.HistoryRevision() == history + 1 && SameCamera(camera, viewport.GetCamera()), "duplicate history/camera");
        const auto duplicated = viewport.SnapshotProject();
        Require(viewport.Undo(error) && SameProject(before, viewport.SnapshotProject()) &&
                viewport.ProjectRevision() == revision && viewport.ModelInfo(id)->selected &&
                viewport.CurrentModelInfo()->id == id, "duplicate undo did not restore source selection/geometry");
        Require(viewport.Redo(error) && SameProject(duplicated, viewport.SnapshotProject()) &&
                viewport.ModelInfos()[1].id == copy.id && !viewport.ModelInfo(id)->selected,
                "duplicate redo did not preserve model identity");
        Require(viewport.MoveModelTo(id, {100, 100, 4}), "move original after duplication");
        Require(viewport.ModelInfo(copy.id)->world_position_mm == copy.world_position_mm, "copy shares editable transform with original");
    });
    Run("multi-model array repeats the group in world XY and preserves unselected models", [&]()
    {
        viewport.NewProject();
        const auto first = load(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl"));
        const auto second = load(QStringLiteral(TEST_ASSET_DIR "/torus_30mm.stl"));
        load(QStringLiteral(TEST_ASSET_DIR "/sphere_30mm.stl"));
        Require(viewport.TransformModelTo(first, {{-20, 8, 5}, {1.2f, 0.7f, 1.1f}, {15, 25, 90}}, TransformMode::Rotate), "prepare first array object");
        Require(viewport.TransformModelTo(second, {{25, -10, 3}, {0.5f, 1.5f, 0.8f}, {0, 20, -30}}, TransformMode::Rotate), "prepare second array object");
        viewport.SetModelSelected(first, true);
        viewport.SetModelSelected(second, true);
        const auto sources = viewport.ModelInfos();
        const auto before = viewport.SnapshotProject();
        const auto history = viewport.HistoryRevision();
        const glm::vec2 minimum = glm::min(glm::vec2(sources[0].world_bounds_min_mm), glm::vec2(sources[1].world_bounds_min_mm));
        const glm::vec2 maximum = glm::max(glm::vec2(sources[0].world_bounds_max_mm), glm::vec2(sources[1].world_bounds_max_mm));
        const glm::vec2 step = maximum - minimum + glm::vec2(7.5f, 12.25f);
        Require(viewport.ArraySelectedModels(2, 3, {7.5f, 12.25f}, error), qPrintable(error));
        const auto copies = viewport.ModelInfos();
        Require(copies.size() == 13 && viewport.HistoryRevision() == history + 1, "array count or batch history wrong");
        const auto after = viewport.SnapshotProject();
        for (int i = 0; i < 3; ++i) Require(SameModel(before.models[i], after.models[i]), "array moved an original");
        std::size_t index = 3;
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 3; ++column)
            {
                if (row == 0 && column == 0) continue;
                for (int source = 0; source < 2; ++source, ++index)
                {
                    const auto& copy = copies[index];
                    const glm::vec3 offset(column * step.x, row * step.y, 0);
                    Require(glm::length(copy.world_position_mm - sources[source].world_position_mm - offset) < 0.002f &&
                            copy.local_scale == sources[source].local_scale &&
                            copy.local_rotation_degrees == sources[source].local_rotation_degrees && copy.selected,
                            "array lost group spacing, orientation, Z or selection");
                }
            }
        Require(!copies[2].selected && copies[2].world_position_mm == sources[2].world_position_mm, "unselected model affected");
        Require(viewport.Undo(error) && SameProject(before, viewport.SnapshotProject()) &&
                viewport.ModelInfo(first)->selected && viewport.CurrentModelInfo()->id == second,
                "array not undone as one step with original active selection");
        Require(viewport.Redo(error) && SameProject(after, viewport.SnapshotProject()), "array redo changed data");
        for (std::size_t i = 3; i < copies.size(); ++i)
            Require(viewport.ModelInfos()[i].id == copies[i].id, "array redo changed IDs");
    });
    Run("array uses embedded STL after source deletion and survives project roundtrip", [&]()
    {
        viewport.NewProject();
        const QString source = directory + QStringLiteral("/阵列源模型.stl");
        WriteBytes(source, ReadBytes(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl")));
        const auto id = load(source);
        viewport.SetModelSelected(id, true);
        Require(QFile::remove(source), "remove array source");
        Require(viewport.ArraySelectedModels(3, 1, {0, 0}, error), qPrintable(error));
        const auto expected = viewport.SnapshotProject();
        Require(viewport.Undo(error) && viewport.Redo(error), "array redo depends on source file");
        const QString project_path = directory + QStringLiteral("/阵列工程.glproj");
        ProjectData project;
        Require(ProjectFile::Write(project_path, expected, error) && ProjectFile::Read(project_path, project, error) &&
                viewport.ApplyProject(project, error) && SameProject(expected, viewport.SnapshotProject()),
                "array project did not roundtrip independently of source");
    });
    Run("invalid and excessive arrays do not alter models, history or selection", [&]()
    {
        viewport.NewProject();
        Require(!viewport.DuplicateSelectedModels(error) && !error.isEmpty(), "empty selection duplicated");
        const auto id = load(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl"));
        viewport.SetModelSelected(id, true);
        const auto before = viewport.SnapshotProject();
        const auto history = viewport.HistoryRevision();
        const auto revision = viewport.ProjectRevision();
        Require(!viewport.ArraySelectedModels(1, 1, {5, 5}, error), "1x1 accepted");
        Require(!viewport.ArraySelectedModels(0, 2, {5, 5}, error), "zero rows accepted");
        Require(!viewport.ArraySelectedModels(2, std::numeric_limits<int>::max(), {5, 5}, error), "row overflow accepted");
        Require(!viewport.ArraySelectedModels(2, 2, {-1, 5}, error), "negative gap accepted");
        Require(!viewport.ArraySelectedModels(2, 2, {std::numeric_limits<float>::infinity(), 5}, error), "nonfinite gap accepted");
        Require(!viewport.ArraySelectedModels(100, 100, {5, 5}, error), "excessive array accepted");
        Require(viewport.BeginTransformEditing(id, TransformMode::Translate), "begin transform editing");
        Require(!viewport.DuplicateSelectedModels(error), "duplicated while selection locked");
        viewport.EndTransformEditing();
        Require(SameProject(before, viewport.SnapshotProject()) && viewport.HistoryRevision() == history &&
                viewport.ProjectRevision() == revision && viewport.ModelInfo(id)->selected,
                "rejected arrays modified state");
    });
    Run("new duplicate after undo replaces the redo branch with fresh identities", [&]()
    {
        viewport.NewProject();
        const auto id = load(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl"));
        viewport.SetModelSelected(id, true);
        Require(viewport.DuplicateSelectedModels(error), qPrintable(error));
        const auto previous_id = viewport.ModelInfos().back().id;
        Require(viewport.Undo(error) && viewport.CanRedo(), "prepare duplicate branch");
        Require(viewport.ArraySelectedModels(2, 1, {5, 8}, error), qPrintable(error));
        Require(!viewport.CanRedo() && viewport.ModelInfos().back().id != previous_id, "redo branch or model ID reused");
    });
    Run("array placement overflow rolls back all staged copies", [&]()
    {
        viewport.NewProject();
        const auto id = load(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl"));
        Require(viewport.TransformModelTo(id, {{0, 0, 0}, {1.0e36f, 1, 1}, {0, 0, 0}}, TransformMode::Scale), "prepare very large model");
        viewport.SetModelSelected(id, true);
        const auto before = viewport.SnapshotProject();
        const auto history = viewport.HistoryRevision();
        Require(!viewport.ArraySelectedModels(1, 20, {0, 0}, error) && !error.isEmpty(), "overflowed array accepted");
        Require(SameProject(before, viewport.SnapshotProject()) && viewport.HistoryRevision() == history &&
                viewport.ModelInfo(id)->selected, "staging failure left partial copies");
    });
}

void NamingTests(const QString& directory)
{
    SceneViewport viewport;
    Require(viewport.Initialize(), "initialize naming viewport");
    QString error;
    auto load = [&](const QString& path)
    {
        std::string load_error;
        glm::vec3 size;
        Require(viewport.LoadStl(path.toUtf8().toStdString(), load_error, size), load_error.c_str());
        return viewport.ModelInfos().back().id;
    };
    auto unique_names = [&]()
    {
        QSet<QString> names;
        for (const auto& info : viewport.ModelInfos())
        {
            Require(!info.name.isEmpty() && !info.name.endsWith(".stl", Qt::CaseInsensitive), "invalid display name");
            Require(!names.contains(info.name.toCaseFolded()), "duplicate model name");
            names.insert(info.name.toCaseFolded());
        }
    };
    Run("imports strip only the STL suffix and number repeated names", [&]()
    {
        const QString source = directory + QStringLiteral("/打印.零件.STL");
        WriteBytes(source, ReadBytes(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl")));
        const auto first = load(source), second = load(source);
        Require(viewport.ModelInfo(first)->name == QStringLiteral("打印.零件") &&
                viewport.ModelInfo(second)->name == QStringLiteral("打印.零件 (2)"), "import names not derived/numbered");
        Require(QString::fromUtf8(viewport.ModelInfo(first)->path.c_str()) == source, "source path renamed");
        unique_names();
    });
    Run("rename is one undo step, preserves geometry and presentation, and survives saving", [&]()
    {
        viewport.NewProject();
        const auto id = load(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl"));
        viewport.SetModelSelected(id, true);
        viewport.SetModelVisible(id, false);
        viewport.SetModelBoundingBoxVisible(id, true);
        const auto info = *viewport.ModelInfo(id);
        const auto before = viewport.SnapshotProject();
        const auto revision = viewport.ProjectRevision(), history = viewport.HistoryRevision();
        Require(viewport.RenameModel(id, QStringLiteral("  打印件 A.stl  "), error), qPrintable(error));
        auto expected = before;
        expected.models[0].name = QStringLiteral("打印件 A");
        Require(SameProject(expected, viewport.SnapshotProject()) &&
                viewport.ModelInfo(id)->selected && !viewport.ModelInfo(id)->visible &&
                viewport.ModelInfo(id)->bounding_box_visible && viewport.ModelInfo(id)->transform_revision == info.transform_revision,
                "rename changed geometry/presentation or lost normalization");
        Require(viewport.HistoryRevision() == history + 1 && viewport.ProjectRevision() != revision, "rename history/dirty state");
        Require(viewport.Undo(error) && SameProject(before, viewport.SnapshotProject()) && viewport.ProjectRevision() == revision,
                "rename undo not exact");
        Require(viewport.Redo(error) && SameProject(expected, viewport.SnapshotProject()), "rename redo not exact");
        viewport.RemoveStl(id);
        Require(viewport.Undo(error) && viewport.ModelInfo(id)->name == QStringLiteral("打印件 A"), "deleted name not restored");
        const QString file = directory + "/names.glproj";
        ProjectData read;
        Require(ProjectFile::Write(file, expected, error) && ProjectFile::Read(file, read, error) &&
                SameProject(read, expected) && viewport.ApplyProject(read, error) &&
                SameProject(viewport.SnapshotProject(), expected), "name lost in project roundtrip");
    });
    Run("empty, duplicate, oversized and invalid names preserve history; no-op preserves redo", [&]()
    {
        viewport.NewProject();
        const auto first = load(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl"));
        const auto second = load(QStringLiteral(TEST_ASSET_DIR "/torus_30mm.stl"));
        const auto before = viewport.SnapshotProject();
        const auto history = viewport.HistoryRevision(), revision = viewport.ProjectRevision();
        for (const QString& invalid : {QString(), QStringLiteral("  "), QStringLiteral(".STL"),
             QStringLiteral("TORUS_30MM"), QStringLiteral("torus_30mm.stl"), QString(257, QChar('a')), QStringLiteral("a\nb")})
            Require(!viewport.RenameModel(first, invalid, error) && !error.isEmpty(), "invalid name accepted");
        Require(!viewport.RenameModel(0, "missing", error), "missing rename target accepted");
        Require(SameProject(before, viewport.SnapshotProject()) && viewport.HistoryRevision() == history &&
                viewport.ProjectRevision() == revision, "invalid rename mutated project");
        Require(viewport.RenameModel(first, "renamed", error) && viewport.Undo(error) && viewport.CanRedo(), "prepare rename redo");
        const auto undo_history = viewport.HistoryRevision();
        Require(viewport.RenameModel(first, " cube_30mm.STL ", error) && viewport.CanRedo() &&
                viewport.HistoryRevision() == undo_history && SameProject(before, viewport.SnapshotProject()), "no-op rename cleared redo");
        Require(viewport.RenameModel(second, "branch", error) && !viewport.CanRedo(), "new rename kept abandoned redo");
        ProjectData invalid = viewport.SnapshotProject();
        invalid.models[1].name = invalid.models[0].name;
        const auto current = viewport.SnapshotProject();
        Require(!viewport.ApplyProject(invalid, error) && SameProject(current, viewport.SnapshotProject()), "duplicate project names accepted");
    });
    Run("numbering long Unicode names stays valid and survives UTF-8 persistence", [&]()
    {
        viewport.NewProject();
        const auto id = load(QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl"));
        const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
        QString long_name;
        for (int i = 0; i < 128; ++i) long_name += emoji;
        Require(viewport.RenameModel(id, long_name, error), qPrintable(error));
        viewport.SetModelSelected(id, true);
        Require(viewport.DuplicateSelectedModels(error) && viewport.ArraySelectedModels(1, 3, {5, 5}, error),
                "long name duplication failed");
        unique_names();
        const auto expected = viewport.SnapshotProject();
        const QString file = directory + "/unicode-names.glproj";
        ProjectData read;
        Require(ProjectFile::Write(file, expected, error) && ProjectFile::Read(file, read, error) &&
                SameProject(read, expected), "Unicode copy names changed in persistence");
    });
    Run("copies and multi-model arrays avoid reserved names and retain names through undo/redo", [&]()
    {
        viewport.NewProject();
        const QString source = QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl");
        const auto original = load(source), peer = load(source);
        Require(viewport.RenameModel(peer, "cube_30mm (3)", error), "reserve numbered name");
        viewport.SetModelSelected(original, true);
        const auto before_copy = viewport.SnapshotProject();
        Require(viewport.DuplicateSelectedModels(error), qPrintable(error));
        Require(viewport.ModelInfos().back().name == QStringLiteral("cube_30mm (2)"), "copy failed to avoid reserved name");
        const auto copy = viewport.SnapshotProject();
        unique_names();
        Require(viewport.Undo(error) && SameProject(before_copy, viewport.SnapshotProject()) &&
                viewport.Redo(error) && SameProject(copy, viewport.SnapshotProject()), "copy name undo/redo");
        viewport.ClearModelSelection();
        viewport.SetModelSelected(original, true);
        viewport.SetModelSelected(peer, true);
        const auto before_array = viewport.SnapshotProject();
        Require(viewport.ArraySelectedModels(2, 3, {5, 5}, error), qPrintable(error));
        unique_names();
        const auto array = viewport.SnapshotProject();
        Require(viewport.Undo(error) && SameProject(before_array, viewport.SnapshotProject()) &&
                viewport.Redo(error) && SameProject(array, viewport.SnapshotProject()), "array name undo/redo");
        ProjectData read;
        const QString file = directory + "/named-array.glproj";
        Require(ProjectFile::Write(file, array, error) && ProjectFile::Read(file, read, error) &&
                viewport.ApplyProject(read, error) && SameProject(array, viewport.SnapshotProject()), "array names not persisted");
        load(source);
        unique_names();
    });
}

void HistoryTests(const QString& directory)
{
    SceneViewport viewport;
    Require(viewport.Initialize(), "initialize history viewport");
    const QString cube = QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl");
    const QString torus = QStringLiteral(TEST_ASSET_DIR "/torus_30mm.stl");
    QString error;
    const auto load = [&](const QString& source)
    {
        std::string load_error;
        glm::vec3 size;
        Require(viewport.LoadStl(source.toUtf8().toStdString(), load_error, size), load_error.c_str());
        return viewport.ModelInfos().back().id;
    };

    Run("undo import and redo from embedded bytes after source removal", [&]()
    {
        viewport.NewProject();
        const auto clean = viewport.ProjectRevision();
        const QString source = directory + QStringLiteral("/撤销模型.stl");
        WriteBytes(source, ReadBytes(cube));
        const auto id = load(source);
        const auto imported = viewport.SnapshotProject();
        const Camera camera = viewport.GetCamera();
        Require(QFile::remove(source), "remove imported source");
        Require(viewport.CanUndo() && !viewport.CanRedo(), "import history state");
        Require(viewport.Undo(error), qPrintable(error));
        Require(viewport.ModelCount() == 0 && viewport.ProjectRevision() == clean, "undo import state");
        Require(!viewport.CanUndo() && viewport.CanRedo(), "undo end state");
        Require(viewport.Redo(error), qPrintable(error));
        Require(SameProject(imported, viewport.SnapshotProject()), "redo import content changed");
        Require(viewport.ModelInfos()[0].id == id, "redo import changed model identity");
        Require(SameCamera(camera, viewport.GetCamera()), "history changed camera");
        const auto& history = viewport.OperationHistory();
        Require(history.size() == 3 && history[0].startsWith(QStringLiteral("打开 STL")) &&
                history[1].startsWith(QStringLiteral("撤销")) && history[2].startsWith(QStringLiteral("回撤")),
                "history omitted import/undo/redo");
    });
    Run("undo deletion restores mesh, order, ID, transforms and presentation", [&]()
    {
        viewport.NewProject();
        const auto id = load(cube);
        const auto other = load(torus);
        Require(viewport.MoveModelTo(id, {32.25f, -71.5f, 8.25f}), "move fixture");
        viewport.SetModelSelected(id, true);
        viewport.SetModelVisible(id, false);
        viewport.SetModelBoundingBoxVisible(id, true);
        const auto before = viewport.SnapshotProject();
        const auto revision = viewport.ProjectRevision();
        const auto info = *viewport.ModelInfo(id);
        viewport.RemoveStl(id);
        Require(!viewport.ModelInfo(id) && viewport.ModelInfos()[0].id == other, "delete wrong instance");
        Require(viewport.Undo(error), qPrintable(error));
        Require(SameProject(before, viewport.SnapshotProject()), "undo deletion geometry changed");
        Require(viewport.ProjectRevision() == revision, "undo deletion did not restore saved-state identity");
        Require(viewport.ModelInfos()[0].id == id && viewport.ModelInfos()[1].id == other, "model order lost");
        const auto* restored = viewport.ModelInfo(id);
        Require(restored && restored->selected && !restored->visible && restored->bounding_box_visible,
                "undo deletion lost presentation");
        Require(restored->world_position_mm == info.world_position_mm, "undo deletion moved model");
        Require(viewport.Redo(error) && !viewport.ModelInfo(id), "redo deletion failed");
    });
    Run("one multi-frame movement is one exact undo/redo operation", [&]()
    {
        viewport.NewProject();
        const auto id = load(cube);
        const auto initial = *viewport.ModelInfo(id);
        const auto clean = viewport.ProjectRevision();
        const auto count = viewport.OperationHistory().size();
        Require(viewport.BeginModelMove(id), "begin drag");
        for (int i = 1; i <= 50; ++i)
            Require(viewport.MoveModelTo(id, {i * 0.12345f, -i * 0.23456f, 0}), "update drag");
        Require(viewport.OperationHistory().size() == count, "per-frame drag records created");
        viewport.EndModelMove();
        const auto moved = *viewport.ModelInfo(id);
        const auto final_revision = viewport.ProjectRevision();
        Require(viewport.OperationHistory().size() == count + 1, "drag not grouped into one step");
        Require(viewport.OperationHistory().back().contains(QStringLiteral("移动 STL")), "move description missing");
        Require(viewport.Undo(error), qPrintable(error));
        Require(viewport.ModelInfo(id)->local_translation_mm == initial.local_translation_mm &&
                viewport.ModelInfo(id)->world_position_mm == initial.world_position_mm, "undo move not exact");
        Require(viewport.ProjectRevision() == clean, "undo move did not restore clean state");
        Require(viewport.Redo(error), qPrintable(error));
        Require(viewport.ModelInfo(id)->local_translation_mm == moved.local_translation_mm &&
                viewport.ModelInfo(id)->world_position_mm == moved.world_position_mm, "redo move not exact");
        Require(viewport.ProjectRevision() == final_revision, "redo move state identity changed");
    });
    Run("stationary drag, return to start and failed movement add no steps", [&]()
    {
        viewport.NewProject();
        const auto id = load(cube);
        const auto count = viewport.OperationHistory().size();
        const auto clean = viewport.ProjectRevision();
        Require(viewport.BeginModelMove(id), "begin stationary drag");
        viewport.EndModelMove();
        Require(viewport.BeginModelMove(id), "begin returning drag");
        Require(viewport.MoveModelTo(id, {30, -40, 0}), "outward drag");
        Require(viewport.MoveModelTo(id, {0, 0, 0}), "returning drag");
        viewport.EndModelMove();
        Require(!viewport.MoveModelTo(id, {std::numeric_limits<float>::quiet_NaN(), 0, 0}), "NaN move accepted");
        Require(viewport.OperationHistory().size() == count && viewport.ProjectRevision() == clean,
                "no-op gesture changed history or dirty state");
    });
    Run("focus release commits pending drag and undo removes only that step", [&]()
    {
        viewport.NewProject();
        const auto id = load(cube);
        Require(viewport.BeginModelMove(id), "begin unfinished drag");
        Require(viewport.MoveModelTo(id, {21.5f, 12.25f, 0}), "unfinished drag update");
        viewport.ReleaseCursor();
        Require(viewport.OperationHistory().size() == 2, "focus release lost movement");
        Require(viewport.Undo(error) && viewport.ModelCount() == 1 &&
                viewport.ModelInfo(id)->local_translation_mm == glm::vec3(0), "undo pending drag removed model");
    });
    Run("history supports complete mixed import/move/delete undo and redo chain", [&]()
    {
        viewport.NewProject();
        const auto empty = viewport.ProjectRevision();
        const auto id = load(cube);
        const auto duplicate = load(cube);
        Require(viewport.MoveModelTo(id, {12.75f, 24.5f, 0}), "chain movement");
        viewport.RemoveStl(duplicate);
        const auto finished = viewport.SnapshotProject();
        for (int i = 0; i < 4; ++i)
            Require(viewport.Undo(error), qPrintable(error));
        Require(viewport.ModelCount() == 0 && viewport.ProjectRevision() == empty && !viewport.CanUndo(),
                "mixed undo chain incomplete");
        for (int i = 0; i < 4; ++i)
            Require(viewport.Redo(error), qPrintable(error));
        Require(SameProject(finished, viewport.SnapshotProject()) && viewport.ModelInfo(id) && !viewport.CanRedo(),
                "mixed redo chain incorrect");
    });
    Run("new branch invalidates redo and does not reuse saved-state identities", [&]()
    {
        viewport.NewProject();
        const auto id = load(cube);
        Require(viewport.MoveModelTo(id, {10, 20, 0}), "old branch move");
        const auto abandoned_saved_revision = viewport.ProjectRevision();
        Require(viewport.Undo(error), qPrintable(error));
        Require(viewport.MoveModelTo(id, {-10, -20, 0}), "new branch move");
        Require(!viewport.CanRedo() && viewport.ProjectRevision() != abandoned_saved_revision,
                "new branch can redo or falsely appears saved");
        Require(viewport.Undo(error) && viewport.ModelInfo(id)->local_translation_mm == glm::vec3(0),
                "new branch undo incorrect");
        Require(viewport.Redo(error) && viewport.ModelInfo(id)->local_translation_mm == glm::vec3(-10, -20, 0),
                "new branch redo used abandoned step");
    });
    Run("failed load/restore and temporary display changes preserve history", [&]()
    {
        viewport.NewProject();
        const auto id = load(cube);
        const auto history = viewport.OperationHistory();
        const auto revision = viewport.ProjectRevision();
        viewport.SetModelSelected(id, true);
        viewport.SetModelVisible(id, false);
        viewport.SetModelBoundingBoxVisible(id, true);
        ProjectData corrupt = viewport.SnapshotProject();
        corrupt.models[0].stl_data = "broken mesh";
        Require(!viewport.ApplyProject(corrupt, error), "corrupt scene accepted");
        std::string load_error;
        glm::vec3 size;
        Require(!viewport.LoadStl((directory + "/absent.stl").toStdString(), load_error, size), "missing import succeeded");
        Require(viewport.OperationHistory() == history && viewport.ProjectRevision() == revision && viewport.CanUndo(),
                "failed operation or display toggle entered history");
    });
    Run("opening and creating projects reset undo/redo and operation log", [&]()
    {
        const ProjectData saved = viewport.SnapshotProject();
        Require(viewport.ApplyProject(saved, error), qPrintable(error));
        Require(!viewport.CanUndo() && !viewport.CanRedo() && viewport.OperationHistory().empty(),
                "Open kept previous project history");
        Require(viewport.MoveModelTo(viewport.ModelInfos()[0].id, {1, 2, 0}), "new edit after Open");
        viewport.NewProject();
        Require(!viewport.CanUndo() && !viewport.CanRedo() && viewport.OperationHistory().empty(),
                "New kept previous project history");
    });
}

void PlateTests(const QString& directory)
{
    SceneViewport viewport;
    Require(viewport.Initialize(), "initialize platform viewport");
    QString error;
    std::string load_error;
    glm::vec3 size;
    ProjectData saved;
    std::vector<SceneViewport::StlModelInfo> positions;
    SceneViewport::ModelId original_id = 0;
    Run("resizing rectangular platform preserves existing models and centers later imports", [&]()
    {
        Require(viewport.LoadStl(TEST_ASSET_DIR "/cube_30mm.stl", load_error, size), load_error.c_str());
        const auto original = viewport.ModelInfos()[0];
        original_id = original.id;
        Require(viewport.RenderFrame(), "render default platform");
        const float before_distance = viewport.GetCamera().Distance();
        Require(viewport.SetPlateSizeMm({245.5f, 125.25f}, error), qPrintable(error));
        Require(viewport.PlateSizeMm() == glm::vec2(245.5f, 125.25f), "rectangle dimensions incorrect");
        const auto* same = viewport.ModelInfo(original_id);
        Require(same->world_bounds_min_mm == original.world_bounds_min_mm &&
                same->world_bounds_max_mm == original.world_bounds_max_mm &&
                same->local_translation_mm == original.local_translation_mm, "resizing moved an existing model");
        Require(viewport.GetCamera().Distance() > before_distance, "Home did not fit resized platform");
        Require(viewport.LoadStl(TEST_ASSET_DIR "/sphere_30mm.stl", load_error, size), load_error.c_str());
        const auto imported = viewport.ModelInfos().back();
        Require(std::abs(imported.world_position_mm.x - 122.75f) < 0.001f &&
                std::abs(imported.world_position_mm.y - 62.625f) < 0.001f &&
                imported.local_translation_mm == glm::vec3(0), "new import not centered on resized platform");
        Require(viewport.RenderFrame(), "render rectangular platform");
        saved = viewport.SnapshotProject();
        positions = viewport.ModelInfos();
    });
    Run("platform changes coexist with import Undo and Redo", [&]()
    {
        Require(viewport.Undo(error) && viewport.ModelCount() == 1, "undo later import");
        Require(viewport.Undo(error) && viewport.PlateSizeMm() == glm::vec2(100), "undo platform resize");
        Require(viewport.ModelInfo(original_id)->world_bounds_min_mm == positions[0].world_bounds_min_mm,
                "platform Undo moved model");
        Require(viewport.Redo(error) && viewport.PlateSizeMm() == saved.plate_size_mm, "redo platform resize");
        Require(viewport.Redo(error) && SameProject(saved, viewport.SnapshotProject()), "redo import lost placement");
    });
    Run("reopen sized project restores exact world geometry; New returns to default platform", [&]()
    {
        const QString path = directory + "/platform_scene.glproj";
        Require(ProjectFile::Write(path, saved, error), qPrintable(error));
        viewport.NewProject();
        Require(viewport.PlateSizeMm() == glm::vec2(100) && !viewport.CanUndo(), "New kept platform or history");
        ProjectData loaded;
        Require(ProjectFile::Read(path, loaded, error) && viewport.ApplyProject(loaded, error), qPrintable(error));
        Require(SameProject(saved, viewport.SnapshotProject()), "sized scene not restored");
        const auto restored = viewport.ModelInfos();
        for (std::size_t i = 0; i < restored.size(); ++i)
            Require(restored[i].world_bounds_min_mm == positions[i].world_bounds_min_mm &&
                    restored[i].world_bounds_max_mm == positions[i].world_bounds_max_mm,
                    "reopened model moved after platform resize");
        Require(viewport.RenderFrame(), "render reopened platform");
        const Camera opened = viewport.GetCamera();
        viewport.ResetView();
        Require(SameCamera(opened, viewport.GetCamera()), "reopened sized project not in Home view");
    });
    Run("invalid and unchanged platform dimensions add no operation", [&]()
    {
        const auto before = viewport.SnapshotProject();
        const auto history = viewport.HistoryRevision();
        for (const glm::vec2 invalid : {glm::vec2(0, 100), glm::vec2(-1, 100),
             glm::vec2(10001, 100), glm::vec2(std::numeric_limits<float>::quiet_NaN(), 100)})
            Require(!viewport.SetPlateSizeMm(invalid, error), "invalid platform dimensions accepted");
        Require(viewport.SetPlateSizeMm(before.plate_size_mm, error), "unchanged platform failed");
        Require(SameProject(before, viewport.SnapshotProject()) && viewport.HistoryRevision() == history,
                "invalid or unchanged dimensions altered scene/history");
        ProjectData corrupt = before;
        corrupt.plate_size_mm = {300, 180};
        corrupt.models[0].stl_data = "broken STL";
        Require(!viewport.ApplyProject(corrupt, error), "bad resized project accepted");
        Require(SameProject(before, viewport.SnapshotProject()), "failed Open changed current platform");
    });
    Run("minimum and maximum platform sizes rebuild and render repeatedly", [&]()
    {
        for (const glm::vec2 dimensions : {glm::vec2(10000, 300), glm::vec2(1, 1), glm::vec2(120, 80)})
        {
            Require(viewport.SetPlateSizeMm(dimensions, error), qPrintable(error));
            Require(viewport.RenderFrame(), "render size boundary");
        }
    });
}

void RecentProjectTests(const QString& directory)
{
    const QString record = directory + QStringLiteral("/recent/最近工程.json");
    QStringList projects;
    QString error;
    Run("recent projects persist Unicode paths, keep five, deduplicate and move reopened project first", [&]()
    {
        Require(RecentProjects::RecordFilePath() == QStringLiteral("D:/vs_cmake_proj/glSceneEditor/asset/recent_projects.json"),
                "recent record outside configured asset directory");
        Require(RecentProjects::Read(record).isEmpty(), "missing recent record not empty");
        for (int i = 0; i < 7; ++i)
        {
            const QString path = directory + QStringLiteral("/最近工程 %1.glproj").arg(i);
            Require(ProjectFile::Write(path, ProjectData{}, error), qPrintable(error));
            projects.append(path);
            Require(RecentProjects::Remember(path, error, record), qPrintable(error));
        }
        const QStringList recent = RecentProjects::Read(record);
        Require(recent.size() == 5 && recent.front() == projects[6] && recent.back() == projects[2],
                "recent order or five-entry limit incorrect");
        Require(RecentProjects::Remember(QDir::toNativeSeparators(projects[3]), error, record), qPrintable(error));
        const QStringList reopened = RecentProjects::Read(record);
        Require(reopened.size() == 5 && reopened.front() == projects[3] && reopened[1] == projects[6],
                "reopened project not deduplicated and moved first");
#ifdef Q_OS_WIN
        Require(RecentProjects::Remember(projects[3].toUpper(), error, record), qPrintable(error));
        Require(RecentProjects::Read(record).size() == 5, "case variation duplicated Windows path");
#endif
    });
    Run("corrupt recent records fall back to an empty list and recover on next successful open", [&]()
    {
        WriteBytes(record, "{broken JSON");
        Require(RecentProjects::Read(record).isEmpty(), "corrupt recent record not ignored");
        Require(RecentProjects::Remember(projects.front(), error, record), qPrintable(error));
        const auto recovered = RecentProjects::Read(record);
        Require(recovered.size() == 1 && recovered.front() == projects.front(), "recent record did not recover");
    });
    Run("recent record write failures return an error", [&]()
    {
        const QString blocked = directory + QStringLiteral("/record_is_a_directory.json");
        Require(QDir().mkpath(blocked), "create blocked record fixture");
        Require(!RecentProjects::Remember(projects.front(), error, blocked) && !error.isEmpty(),
                "recent record write failure went undetected");
    });
}

void UiTests(const QString& directory)
{
    HideTestWindows filter;
    qApp->installEventFilter(&filter);
    SceneViewport viewport;
    Require(viewport.Initialize(), "initialize UI viewport");
    const QString recent_file = directory + QStringLiteral("/ui_recent_projects.json");
    SceneEditorWindow window(viewport, QString(), recent_file);
    Require(window.IsReady(), "wrap Windows native viewport");
    QString error;
    std::string load_error;
    glm::vec3 size;
    const QString first = directory + QStringLiteral("/首次保存.glproj");
    const QString second = directory + QStringLiteral("/另存为.glproj");
    const QString model = QStringLiteral(TEST_ASSET_DIR "/cube_30mm.stl");
    Run("project menu actions and shortcuts", [&]()
    {
        Require(window.findChild<QAction*>("newProjectAction")->shortcut() == QKeySequence::New, "New shortcut");
        Require(window.findChild<QAction*>("openProjectAction")->shortcut() == QKeySequence::Open, "Open shortcut");
        Require(window.findChild<QAction*>("saveProjectAction")->shortcut() == QKeySequence::Save, "Save shortcut");
        Require(window.findChild<QAction*>("saveProjectAsAction")->shortcut() == QKeySequence::SaveAs, "Save As shortcut");
        Require(window.findChild<QAction*>("openStlAction")->shortcut() == QKeySequence("Ctrl+I"), "Import shortcut");
    });
    Run("platform toolbar dialog reads current dimensions and Cancel changes nothing", [&]()
    {
        auto* toolbar = window.findChild<QToolBar*>("mainToolBar");
        auto* action = window.findChild<QAction*>("plateSizeAction");
        Require(action && toolbar->actions().contains(action) && !action->icon().isNull(), "platform toolbar action missing");
        const auto history = viewport.HistoryRevision();
        bool fields_correct = false;
        WithDialog([&](QDialog* dialog)
        {
            auto* length = dialog->findChild<QDoubleSpinBox*>("plateLengthSpinBox");
            auto* width = dialog->findChild<QDoubleSpinBox*>("plateWidthSpinBox");
            fields_correct = length && width && length->value() == 100 && width->value() == 100;
            if (length && width) { length->setValue(200); width->setValue(150); }
            dialog->reject();
        }, [&]() { Trigger(window, "plateSizeAction"); });
        Require(fields_correct && viewport.PlateSizeMm() == glm::vec2(100) && viewport.HistoryRevision() == history,
                "platform Cancel changed dimensions/history");
    });
    Run("first Save asks destination and writes current scene", [&]()
    {
        Require(viewport.LoadStl(model.toUtf8().toStdString(), load_error, size), load_error.c_str());
        WithFile(first, [&]() { Trigger(window, "saveProjectAction"); });
        Require(QFileInfo::exists(first), "first Save did not create file");
        Require(!window.isWindowModified(), "successful Save remains dirty");
        ProjectData restored;
        Require(ProjectFile::Read(first, restored, error), qPrintable(error));
        Require(SameProject(restored, viewport.SnapshotProject()), "Save wrote wrong scene");
    });
    Run("platform dialog applies fractional rectangle, requires Save and supports Undo/Redo", [&]()
    {
        const auto history = viewport.HistoryRevision();
        bool fields_found = false;
        WithDialog([&](QDialog* dialog)
        {
            auto* length = dialog->findChild<QDoubleSpinBox*>("plateLengthSpinBox");
            auto* width = dialog->findChild<QDoubleSpinBox*>("plateWidthSpinBox");
            fields_found = length && width;
            if (fields_found) { length->setValue(150.75); width->setValue(90.25); }
            dialog->accept();
        }, [&]() { Trigger(window, "plateSizeAction"); });
        Require(fields_found && viewport.PlateSizeMm() == glm::vec2(150.75f, 90.25f) &&
                window.isWindowModified() && viewport.HistoryRevision() == history + 1, "platform edit not applied/recorded");
        Trigger(window, "saveProjectAction");
        ProjectData stored;
        Require(ProjectFile::Read(first, stored, error) && stored.plate_size_mm == viewport.PlateSizeMm() &&
                !window.isWindowModified(), "Save did not acknowledge and store dimensions");
        Trigger(window, "undoAction");
        Require(viewport.PlateSizeMm() == glm::vec2(100) && window.isWindowModified(), "platform Undo did not require Save");
        Trigger(window, "redoAction");
        Require(viewport.PlateSizeMm() == stored.plate_size_mm, "platform Redo not restored");
        Trigger(window, "saveProjectAction");
        const auto unchanged = viewport.HistoryRevision();
        WithDialog([](QDialog* dialog) { dialog->accept(); }, [&]() { Trigger(window, "plateSizeAction"); });
        Require(viewport.HistoryRevision() == unchanged && !window.isWindowModified(), "unchanged dimensions dirtied project");
    });
    Run("Save without new records skips writing, including presentation and no-op movement", [&]()
    {
        QFile file(first);
        Require(file.open(QIODevice::ReadWrite), "open saved project to mark modification time");
        Require(file.setFileTime(QDateTime(QDate(2000, 1, 1), QTime(0, 0), Qt::UTC),
                                 QFileDevice::FileModificationTime), "mark saved file modification time");
        file.close();
        const QDateTime before_time = QFileInfo(first).lastModified();
        const QByteArray before_bytes = ReadBytes(first);
        const auto history_revision = viewport.HistoryRevision();
        const auto id = viewport.ModelInfos()[0].id;
        viewport.SetModelSelected(id, true);
        viewport.SetModelVisible(id, false);
        viewport.SetModelBoundingBoxVisible(id, true);
        viewport.ResetView();
        Require(viewport.BeginModelMove(id), "begin no-op movement");
        Require(viewport.MoveModelTo(id, {12, -8, 0}), "move away during gesture");
        Require(viewport.MoveModelTo(id, {0, 0, 0}), "return to original position");
        // Save finishes the pending gesture before checking its history.
        Trigger(window, "saveProjectAction");
        Trigger(window, "saveProjectAction");
        Require(viewport.HistoryRevision() == history_revision, "no-op or presentation added records");
        Require(!window.isWindowModified(), "no new record marked project modified");
        Require(QFileInfo(first).lastModified() == before_time && ReadBytes(first) == before_bytes,
                "Save rewrote project without new records");
        viewport.SetModelSelected(id, false);
        viewport.SetModelVisible(id, true);
        viewport.SetModelBoundingBoxVisible(id, false);
    });
    Run("Save As changes active path and subsequent Save uses it", [&]()
    {
        const QByteArray first_bytes = ReadBytes(first);
        WithFile(second, [&]() { Trigger(window, "saveProjectAsAction"); });
        Require(viewport.LoadStl(model.toUtf8().toStdString(), load_error, size), load_error.c_str());
        Trigger(window, "saveProjectAction");
        Require(ReadBytes(first) == first_bytes, "Save As overwrote old project");
        ProjectData loaded;
        Require(ProjectFile::Read(second, loaded, error), qPrintable(error));
        Require(loaded.models.size() == 2, "Save used old destination");
    });
    Run("cancel New and cancel close retain unsaved scene", [&]()
    {
        Require(viewport.LoadStl(model.toUtf8().toStdString(), load_error, size), load_error.c_str());
        WithAnswer(QMessageBox::Cancel, [&]() { Trigger(window, "newProjectAction"); });
        Require(viewport.ModelCount() == 3 && window.isWindowModified(), "cancel New lost scene");
        WithAnswer(QMessageBox::Cancel, [&]() { Require(!window.close(), "cancel close accepted"); });
        Require(viewport.ModelCount() == 3, "cancel close lost model");
    });
    Run("discard New clears scene and active project path", [&]()
    {
        WithAnswer(QMessageBox::Discard, [&]() { Trigger(window, "newProjectAction"); });
        Require(viewport.ModelCount() == 0 && !window.isWindowModified(), "new project not clean");
        Require(window.windowFilePath().isEmpty(), "new project retained saved path");
    });
    Run("Open action restores saved scene and updates model details", [&]()
    {
        WithFile(first, [&]() { Trigger(window, "openProjectAction"); });
        Require(viewport.ModelCount() == 1 && !window.isWindowModified(), "open not restored and clean");
        Require(window.windowFilePath() == first, "open did not update path");
        const auto recent = RecentProjects::Read(recent_file);
        Require(recent.size() == 1 && recent.front() == first, "editor Open did not update recent record");
    });
    Run("reopening current project after Save uses newly saved content", [&]()
    {
        Require(viewport.LoadStl(model.toUtf8().toStdString(), load_error, size), load_error.c_str());
        // One timer handles both successive modal dialogs in the action's event loops.
        bool chose_file = false, chose_save = false;
        QTimer responder;
        responder.setInterval(10);
        QObject::connect(&responder, &QTimer::timeout, [&]()
        {
            if (!chose_file)
            {
                if (auto* file = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()))
                {
                    chose_file = true;
                    file->selectFile(first);
                    QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
                }
            }
            else if (!chose_save)
            {
                if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                {
                    chose_save = true;
                    box->button(QMessageBox::Save)->click();
                    responder.stop();
                }
            }
        });
        responder.start();
        Trigger(window, "openProjectAction");
        Require(chose_file && chose_save, "Open and Save prompts did not appear");
        Require(viewport.ModelCount() == 2, "reopen used stale pre-Save document");
        Require(!window.isWindowModified(), "reopened project dirty");
    });
    Run("welcome Open accepts a complete saved project", [&]()
    {
        WelcomeDialog welcome(recent_file);
        WithFile(first, [&]()
        { welcome.findChild<QPushButton*>("openProjectButton")->click(); });
        Require(welcome.result() == QDialog::Accepted && welcome.ProjectPath() == first,
                "welcome Open did not select project");
        ProjectData loaded;
        Require(ProjectFile::Read(first, loaded, error), qPrintable(error));
        Require(SameProject(welcome.TakeInitialProject(), loaded), "welcome returned wrong project");
    });
    Run("welcome displays five recent projects and opens a selected entry with one click or Enter", [&]()
    {
        const QString record = directory + QStringLiteral("/welcome_recent.json");
        QStringList fixtures;
        for (int i = 0; i < 7; ++i)
        {
            const QString path = directory + QStringLiteral("/欢迎工程 %1.glproj").arg(i);
            Require(ProjectFile::Write(path, ProjectData{}, error), qPrintable(error));
            Require(RecentProjects::Remember(path, error, record), qPrintable(error));
            fixtures.prepend(path);
        }
        WelcomeDialog welcome(record);
        auto* list = welcome.findChild<QListWidget*>("recentProjectList");
        Require(list && list->count() == 5 && list->item(0)->data(Qt::UserRole).toString() == fixtures[0],
                "welcome recent count or newest entry incorrect");
        Require(list->item(4)->data(Qt::UserRole).toString() == fixtures[4], "welcome recent order incorrect");
        welcome.show();
        qApp->processEvents();
        const QPoint position = list->visualItemRect(list->item(2)).center();
        QMouseEvent press(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &press);
        QApplication::sendEvent(list->viewport(), &release);
        Require(welcome.result() == QDialog::Accepted && welcome.ProjectPath() == fixtures[2],
                "single click did not open the selected recent project");
        Require(welcome.TakeInitialProject().models.empty(), "recent click loaded wrong project");
        WelcomeDialog keyboard(record);
        auto* keyboard_list = keyboard.findChild<QListWidget*>("recentProjectList");
        keyboard.show();
        qApp->processEvents();
        keyboard_list->setCurrentRow(0);
        keyboard_list->setFocus();
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(keyboard_list, &enter);
        Require(keyboard.result() == QDialog::Accepted && keyboard.ProjectPath() == fixtures[0],
                "keyboard activation did not open recent project");
    });
    Run("missing and corrupt recent projects stay on welcome and preserve recent order", [&]()
    {
        const QString record = directory + QStringLiteral("/bad_recent.json");
        const QString missing = directory + QStringLiteral("/已移动的工程.glproj");
        const QString corrupt = directory + QStringLiteral("/损坏工程.glproj");
        Require(RecentProjects::Remember(missing, error, record), qPrintable(error));
        WriteBytes(corrupt, "not a project file");
        Require(RecentProjects::Remember(corrupt, error, record), qPrintable(error));
        const QByteArray before = ReadBytes(record);
        WelcomeDialog welcome(record);
        auto* list = welcome.findChild<QListWidget*>("recentProjectList");
        Require(list && list->count() == 2, "bad recent fixture not listed");
        for (int i = 0; i < 2; ++i)
        {
            WithDialog([](QDialog* dialog) { dialog->reject(); }, [&]()
            { list->itemClicked(list->item(i)); });
            Require(welcome.result() != QDialog::Accepted && welcome.ProjectPath().isEmpty(),
                    "bad recent project entered editor");
            Require(ReadBytes(record) == before, "failed recent open changed record");
        }
    });
    Run("cancel welcome Open stays on welcome", [&]()
    {
        WelcomeDialog welcome(recent_file);
        WithDialog([](QDialog* dialog) { dialog->reject(); }, [&]()
        { welcome.findChild<QPushButton*>("openProjectButton")->click(); });
        Require(welcome.result() != QDialog::Accepted && welcome.ProjectPath().isEmpty(),
                "cancel welcome Open entered editor");
    });
    Run("welcome and editor Open start in configured glproj directory", [&]()
    {
        const QByteArray before_recent = ReadBytes(recent_file);
        bool correct = false;
        auto reject = [&](QDialog* dialog)
        {
            auto* picker = qobject_cast<QFileDialog*>(dialog);
            correct = picker && QDir::cleanPath(picker->directory().absolutePath()) ==
                                QDir::cleanPath(ProjectFile::DefaultOpenDirectory());
            dialog->reject();
        };
        WithDialog(reject, [&]() { Trigger(window, "openProjectAction"); });
        Require(correct, "editor Open starts outside glproj directory");
        correct = false;
        WelcomeDialog welcome(recent_file);
        WithDialog(reject, [&]()
        { welcome.findChild<QPushButton*>("openProjectButton")->click(); });
        Require(correct, "welcome Open starts outside glproj directory");
        Require(ReadBytes(recent_file) == before_recent, "cancel Open changed recent record");
    });
    Run("cancel Save As keeps current path and unsaved changes", [&]()
    {
        Require(viewport.LoadStl(model.toUtf8().toStdString(), load_error, size), load_error.c_str());
        const QByteArray before = ReadBytes(first);
        WithDialog([](QDialog* dialog) { dialog->reject(); }, [&]()
        { Trigger(window, "saveProjectAsAction"); });
        Require(window.windowFilePath() == first && window.isWindowModified(),
                "cancel Save As changed current document");
        Require(ReadBytes(first) == before, "cancel Save As wrote the project");
    });
    Run("toolbar history controls and undo/redo shortcuts", [&]()
    {
        auto* toolbar = window.findChild<QToolBar*>("mainToolBar");
        Require(toolbar != nullptr, "main toolbar missing");
        for (const char* name : {"undoAction", "redoAction", "operationHistoryAction"})
        {
            auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
            Require(action && toolbar->actions().contains(action), "history action missing from toolbar");
        }
        Require(window.findChild<QAction*>("undoAction")->shortcut() == QKeySequence::Undo, "Undo shortcut missing");
        Require(window.findChild<QAction*>("redoAction")->shortcuts().contains(QKeySequence::Redo), "Redo shortcut missing");
    });
    Run("new Undo and Redo records require Save even at a previously saved scene", [&]()
    {
        Trigger(window, "saveProjectAction");
        const auto count = viewport.ModelCount();
        const auto id = viewport.ModelInfos()[0].id;
        const auto saved_scene = viewport.SnapshotProject();
        Require(viewport.BeginModelMove(id), "begin UI movement");
        Require(viewport.MoveModelTo(id, {42.25f, -31.75f, 0}), "UI movement");
        WithDialog([](QDialog* dialog) { dialog->reject(); }, [&]()
        { Trigger(window, "saveProjectAsAction"); });
        Require(window.isWindowModified(), "move did not mark title modified");
        Trigger(window, "undoAction");
        Require(window.isWindowModified() && SameProject(saved_scene, viewport.SnapshotProject()),
                "new Undo record did not require Save at the previously saved scene");
        const auto history_revision = viewport.HistoryRevision();
        Trigger(window, "saveProjectAction");
        Require(!window.isWindowModified() && viewport.HistoryRevision() == history_revision,
                "Save did not acknowledge records or changed history");
        Trigger(window, "redoAction");
        Require(window.isWindowModified() && viewport.ModelInfo(id)->local_translation_mm == glm::vec3(42.25f, -31.75f, 0),
                "Redo did not restore move");
        viewport.RemoveStl(id);
        WithDialog([](QDialog* dialog) { dialog->reject(); }, [&]()
        { Trigger(window, "saveProjectAsAction"); });
        Trigger(window, "undoAction");
        auto* models = window.findChild<QListWidget*>("stlModelList");
        Require(viewport.ModelCount() == count && models->count() == int(count), "Undo deletion did not update model list");
    });
    Run("operation record window is reusable and latest operations are first", [&]()
    {
        Trigger(window, "operationHistoryAction");
        auto* dialog = window.findChild<QDialog*>("operationHistoryDialog");
        auto* list = window.findChild<QListWidget*>("operationHistoryList");
        Require(dialog && dialog->isVisible() && list, "record window did not open");
        const auto& history = viewport.OperationHistory();
        Require(list->count() == int(history.size()) && list->item(0)->text().endsWith(history.back()),
                "latest operation not at top");
        Require(list->item(list->count() - 1)->text().endsWith(history.front()), "oldest record not at bottom");
        dialog->reject();
        Trigger(window, "operationHistoryAction");
        Require(window.findChildren<QDialog*>("operationHistoryDialog").size() == 1, "record window duplicated");
        dialog->reject();
        WithAnswer(QMessageBox::Discard, [&]() { Trigger(window, "newProjectAction"); });
        Require(list->count() == 0 && window.findChild<QLabel*>("emptyOperationHistory")->isVisibleTo(dialog),
                "new project did not clear record window");
        Require(!window.findChild<QAction*>("undoAction")->isEnabled() &&
                !window.findChild<QAction*>("redoAction")->isEnabled(), "empty history controls not disabled");
    });

    Run("model transform buttons open reusable nonmodal XYZ parameter windows", [&]()
    {
        WithFile(model, [&]() { Trigger(window, "openStlAction"); });
        WithFile(model, [&]() { Trigger(window, "openStlAction"); });
        const auto id = viewport.ModelInfos()[0].id;
        auto* model_list = window.findChild<QListWidget*>("stlModelList");
        model_list->itemWidget(model_list->item(0))->findChild<QToolButton*>("selectModelButton")->click();
        const QString transform_project = directory + QStringLiteral("/参数编辑.glproj");
        WithFile(transform_project, [&]() { Trigger(window, "saveProjectAction"); });
        auto* move = window.findChild<QPushButton*>("modelTranslateButton");
        auto* rotate = window.findChild<QPushButton*>("modelRotateButton");
        auto* scale = window.findChild<QPushButton*>("modelScaleButton");
        Require(move && rotate && scale, "transform buttons missing");
        move->click();
        auto* dialog = window.findChild<QDialog*>("modelTransformDialog");
        Require(dialog && dialog->isVisible() && !dialog->isModal() &&
                viewport.IsTransformEditing() && viewport.GetTransformMode() == TransformMode::Translate,
                "translation window did not open gizmo/lock session");
        const auto selection_buttons = window.findChildren<QToolButton*>("selectModelButton");
        Require(selection_buttons.size() == 2, "selection-lock fixture missing second model");
        for (auto* selection : selection_buttons)
        {
            Require(!selection->isEnabled(), "model selection button not disabled");
            selection->click();
        }
        viewport.SetModelSelected(id, false);
        viewport.SetModelSelected(viewport.ModelInfos()[1].id, true);
        viewport.ClearModelSelection();
        Require(viewport.ModelInfo(id)->selected && !viewport.ModelInfos()[1].selected,
                "open window permitted selection changes");
        auto* x = dialog->findChild<QDoubleSpinBox*>("modelTransformX");
        auto* y = dialog->findChild<QDoubleSpinBox*>("modelTransformY");
        auto* z = dialog->findChild<QDoubleSpinBox*>("modelTransformZ");
        auto* apply = dialog->findChild<QPushButton*>("applyModelTransformButton");
        Require(x && y && z && apply && x->value() == 0 && y->value() == 0 && z->value() == 0, "XYZ fields missing");
        const auto before_close = viewport.HistoryRevision();
        x->setValue(125);
        dialog->reject();
        Require(viewport.HistoryRevision() == before_close && viewport.ModelInfo(id)->local_translation_mm == glm::vec3(0),
                "Close applied uncommitted values");
        Require(!viewport.IsTransformEditing(), "reject left gizmo/selection lock active");
        for (auto* selection : selection_buttons) Require(selection->isEnabled(), "reject did not enable selection");
        move->click();
        x->setValue(12.125); y->setValue(-8.25); z->setValue(4.5);
        apply->click();
        Require(viewport.ModelInfo(id)->local_translation_mm == glm::vec3(12.125f, -8.25f, 4.5f) &&
                window.isWindowModified() && viewport.HistoryRevision() == before_close + 1, "numeric translation not recorded");
        const auto translated = *viewport.ModelInfo(id);
        rotate->click();
        Require(viewport.GetTransformMode() == TransformMode::Rotate && window.findChildren<QDialog*>("modelTransformDialog").size() == 1,
                "rotation did not switch/reuse dialog");
        z->setValue(90); apply->click();
        const auto rotated = *viewport.ModelInfo(id);
        Require(rotated.local_rotation_degrees == glm::vec3(0, 0, 90) &&
                glm::length(rotated.world_position_mm - translated.world_position_mm) < 0.001f &&
                viewport.OperationHistory().back().contains(QStringLiteral("旋转 STL")), "numeric rotation center/history wrong");
        scale->click();
        Require(viewport.GetTransformMode() == TransformMode::Scale && x->minimum() > 0, "scale window range/mode wrong");
        x->setValue(2); y->setValue(0.5); z->setValue(1.25); apply->click();
        const auto scaled = *viewport.ModelInfo(id);
        Require(scaled.local_scale == glm::vec3(2, 0.5f, 1.25f) &&
                glm::length(scaled.world_position_mm - rotated.world_position_mm) < 0.001f, "numeric scale moved center");
        const auto check_bounds_details = [&]()
        {
            const auto* info = viewport.ModelInfo(id);
            const glm::vec3 dimensions = info->world_bounds_max_mm - info->world_bounds_min_mm;
            const QString axes[] = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
            for (int axis = 0; axis < 3; ++axis)
            {
                auto* size_label = window.findChild<QLabel*>(QStringLiteral("modelSize") + axes[axis]);
                Require(size_label && size_label->text() == QStringLiteral("%1 mm").arg(
                    QLocale().toString(dimensions[axis], 'f', 2)), "dimensions retained original STL size after scale");
                auto* minimum = window.findChild<QLabel*>(QStringLiteral("modelBoundsMin") + axes[axis]);
                auto* maximum = window.findChild<QLabel*>(QStringLiteral("modelBoundsMax") + axes[axis]);
                Require(minimum && maximum && minimum->text() == QLocale().toString(info->world_bounds_min_mm[axis], 'f', 2) &&
                        maximum->text() == QLocale().toString(info->world_bounds_max_mm[axis], 'f', 2),
                        "hidden bounds labels did not update");
            }
        };
        Require(!scaled.bounding_box_visible, "UI scale fixture must have hidden bounds");
        check_bounds_details();
        const auto history = viewport.HistoryRevision();
        apply->click();
        Require(viewport.HistoryRevision() == history, "unchanged Apply created history");
        Trigger(window, "undoAction");
        Require(viewport.ModelInfo(id)->local_scale == glm::vec3(1) && x->value() == 1, "Undo failed to synchronize fields");
        check_bounds_details();
        Trigger(window, "redoAction");
        Require(x->value() == 2 && y->value() == 0.5, "Redo failed to synchronize fields");
        check_bounds_details();
        auto* bounds_button = window.findChild<QToolButton*>(QStringLiteral("toggleModelBoundingBoxButton"));
        bounds_button->click();
        check_bounds_details();
        bounds_button->click();
        check_bounds_details();
        Trigger(window, "saveProjectAction");
        ProjectData saved;
        Require(!window.isWindowModified() && ProjectFile::Read(transform_project, saved, error) &&
                saved.models[0].scale == scaled.local_scale && saved.models[0].rotation_degrees == scaled.local_rotation_degrees,
                "transforms not saved");
        viewport.SetModelSelected(id, false);
        Require(dialog->isVisible() && viewport.ModelInfo(id)->selected && viewport.IsTransformEditing(),
                "selection changed while parameter window remained open");
        const auto closed_history = viewport.HistoryRevision();
        dialog->close();
        Require(!dialog->isVisible() && !viewport.IsTransformEditing() && viewport.HistoryRevision() == closed_history,
                "window close did not stop gizmo without adding history");
        for (auto* selection : selection_buttons) Require(selection->isEnabled(), "window close did not unlock selection");
        selection_buttons[0]->click();
        Require(!viewport.ModelInfo(id)->selected, "selection button remained locked after close");
    });
    Run("copy and array actions, dialog validation, cancel, undo and save", [&]()
    {
        viewport.NewProject();
        WithFile(model, [&]() { Trigger(window, "openStlAction"); });
        auto* duplicate = window.findChild<QAction*>("duplicateModelsAction");
        auto* array = window.findChild<QAction*>("modelArrayAction");
        auto* toolbar = window.findChild<QToolBar*>("mainToolBar");
        Require(duplicate && array && !duplicate->isEnabled() && !array->isEnabled() &&
                toolbar->actions().contains(duplicate) && toolbar->actions().contains(array) &&
                !duplicate->icon().isNull() && !array->icon().isNull(), "duplication toolbar state/icons");
        Require(duplicate->shortcut() == QKeySequence("Ctrl+D") && array->shortcut() == QKeySequence("Ctrl+Shift+D"), "duplication shortcuts");
        auto* model_list = window.findChild<QListWidget*>("stlModelList");
        model_list->itemWidget(model_list->item(0))->findChild<QToolButton*>("selectModelButton")->click();
        Require(duplicate->isEnabled() && array->isEnabled(), "selection did not enable duplication");
        const auto original = viewport.ModelInfos()[0].id;
        Trigger(window, "duplicateModelsAction");
        Require(viewport.ModelCount() == 2 && window.isWindowModified() &&
                window.findChild<QListWidget*>("stlModelList")->count() == 2, "UI duplicate list/title");
        Trigger(window, "undoAction");
        Require(viewport.ModelCount() == 1 && viewport.ModelInfo(original)->selected, "UI duplicate undo");
        const auto history = viewport.HistoryRevision();
        WithDialog([&](QDialog* dialog)
        {
            Require(dialog->objectName() == QStringLiteral("modelArrayDialog"), "wrong array dialog");
            auto* rows = dialog->findChild<QSpinBox*>("arrayRowsSpinBox");
            auto* columns = dialog->findChild<QSpinBox*>("arrayColumnsSpinBox");
            auto* x = dialog->findChild<QDoubleSpinBox*>("arrayGapXSpinBox");
            auto* y = dialog->findChild<QDoubleSpinBox*>("arrayGapYSpinBox");
            auto* generate = dialog->findChild<QPushButton*>("generateModelArrayButton");
            Require(rows && columns && x && y && generate && rows->value() == 2 && columns->value() == 2 &&
                    x->value() == 5 && y->value() == 5, "array defaults/fields");
            rows->setValue(1); columns->setValue(1);
            Require(!generate->isEnabled(), "1x1 generation enabled");
            rows->setValue(100); columns->setValue(100);
            Require(!generate->isEnabled(), "excessive generation enabled");
            dialog->reject();
        }, [&]() { Trigger(window, "modelArrayAction"); });
        Require(viewport.ModelCount() == 1 && viewport.HistoryRevision() == history && viewport.CanRedo(), "cancel altered array or redo history");
        WithDialog([&](QDialog* dialog)
        {
            dialog->findChild<QSpinBox*>("arrayRowsSpinBox")->setValue(2);
            dialog->findChild<QSpinBox*>("arrayColumnsSpinBox")->setValue(3);
            dialog->findChild<QDoubleSpinBox*>("arrayGapXSpinBox")->setValue(7.5);
            dialog->findChild<QDoubleSpinBox*>("arrayGapYSpinBox")->setValue(12.25);
            Require(dialog->findChild<QLabel*>("arraySummaryLabel")->text().contains(QStringLiteral("新增 5 个")), "array count summary");
            dialog->findChild<QPushButton*>("generateModelArrayButton")->click();
        }, [&]() { Trigger(window, "modelArrayAction"); });
        const auto expected = viewport.SnapshotProject();
        Require(viewport.ModelCount() == 6 && viewport.HistoryRevision() == history + 1 && !viewport.CanRedo(), "UI array generation/history");
        Trigger(window, "undoAction");
        Require(viewport.ModelCount() == 1 && window.findChild<QListWidget*>("stlModelList")->count() == 1, "UI array undo list");
        Trigger(window, "redoAction");
        Require(SameProject(expected, viewport.SnapshotProject()), "UI array redo");
        const QString array_project = directory + QStringLiteral("/界面阵列.glproj");
        WithFile(array_project, [&]() { Trigger(window, "saveProjectAsAction"); });
        ProjectData saved;
        Require(!window.isWindowModified() && ProjectFile::Read(array_project, saved, error) &&
                SameProject(expected, saved), "UI array save");
        window.findChild<QPushButton*>("modelRotateButton")->click();
        Require(!duplicate->isEnabled() && !array->isEnabled(), "duplication enabled in transform session");
        window.findChild<QDialog*>("modelTransformDialog")->close();
        Require(duplicate->isEnabled() && array->isEnabled(), "duplication stayed disabled after transform close");
    });
    Run("place-on-plate toolbar updates bounds, supports undo redo and persists in project", [&]()
    {
        viewport.NewProject();
        WithFile(model, [&]() { Trigger(window, "openStlAction"); });
        auto* action = window.findChild<QAction*>("placeOnPlateAction");
        auto* toolbar = window.findChild<QToolBar*>("mainToolBar");
        Require(action && toolbar->actions().contains(action) && !action->isEnabled() &&
                !action->icon().isNull() && action->shortcut() == QKeySequence("Ctrl+L"), "plate placement action/shortcut state");
        const auto id = viewport.ModelInfos()[0].id;
        Require(viewport.TransformModelTo(id, {{12, -8, 45}, {1.2f, 0.7f, 1.5f}, {35, 40, 25}}, TransformMode::Rotate), "raise UI placement model");
        auto* list = window.findChild<QListWidget*>("stlModelList");
        list->itemWidget(list->item(0))->findChild<QToolButton*>("selectModelButton")->click();
        Require(action->isEnabled(), "selection did not enable placement");
        const QString path = directory + QStringLiteral("/界面贴合基板.glproj");
        WithFile(path, [&]() { Trigger(window, "saveProjectAsAction"); });
        const auto before = viewport.SnapshotProject();
        const auto position = viewport.ModelInfo(id)->world_position_mm;
        Trigger(window, "placeOnPlateAction");
        Require(window.isWindowModified() && viewport.ModelInfo(id)->world_bounds_min_mm.z == 0 &&
                glm::length(glm::vec2(viewport.ModelInfo(id)->world_position_mm - position)) < 0.001f,
                "UI placement bounds/title/XY wrong");
        Require(window.findChild<QLabel*>("modelBoundsMinZ")->text() == QLocale().toString(0.0, 'f', 2), "Z label not zero");
        const auto placed = viewport.SnapshotProject();
        Trigger(window, "undoAction");
        Require(SameProject(before, viewport.SnapshotProject()), "UI placement undo");
        Trigger(window, "redoAction");
        Require(SameProject(placed, viewport.SnapshotProject()), "UI placement redo");
        Trigger(window, "saveProjectAction");
        ProjectData saved;
        Require(!window.isWindowModified() && ProjectFile::Read(path, saved, error) && SameProject(placed, saved), "UI placement save");
        const auto history = viewport.HistoryRevision();
        Trigger(window, "placeOnPlateAction");
        Require(!window.isWindowModified() && viewport.HistoryRevision() == history, "UI no-op placement dirtied project");
        window.findChild<QPushButton*>("modelRotateButton")->click();
        Require(!action->isEnabled(), "placement enabled while transform window open");
        window.findChild<QDialog*>("modelTransformDialog")->close();
        Require(action->isEnabled(), "placement stayed disabled after transform close");
    });
    Run("model list double-click renames, validates, cancels, updates details and persists names", [&]()
    {
        viewport.NewProject();
        WithFile(model, [&]() { Trigger(window, "openStlAction"); });
        WithFile(model, [&]() { Trigger(window, "openStlAction"); });
        auto* list = window.findChild<QListWidget*>("stlModelList");
        const auto original = viewport.ModelInfos()[0].id;
        auto row = [&](int index) { return list->itemWidget(list->item(index)); };
        auto label = [&](int index) { return row(index)->findChild<QLabel*>("modelRowName"); };
        Require(list->item(0)->text() == QStringLiteral("cube_30mm") &&
                label(1)->text() == QStringLiteral("cube_30mm (2)"), "UI import names not numbered/stripped");
        const QString path = directory + "/ui-names.glproj";
        WithFile(path, [&]() { Trigger(window, "saveProjectAsAction"); });
        auto double_click = [&](QWidget* target)
        {
            QMouseEvent event(QEvent::MouseButtonDblClick, QPointF(8, 8),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &event);
        };
        auto history = viewport.HistoryRevision();
        WithDialog([&](QDialog* dialog)
        {
            auto* field = dialog->findChild<QLineEdit*>("modelNameEdit");
            Require(dialog->objectName() == QStringLiteral("renameModelDialog") && field &&
                    field->text() == QStringLiteral("cube_30mm") && field->selectedText() == field->text(),
                    "double-click did not open prefilled name editor");
            field->setText(QStringLiteral("取消的名称"));
            dialog->reject();
        }, [&]() { double_click(label(0)); });
        Require(!window.isWindowModified() && viewport.HistoryRevision() == history &&
                viewport.ModelInfo(original)->name == QStringLiteral("cube_30mm") &&
                !viewport.ModelInfo(original)->selected, "cancel changed name or selection");

        WithDialog([&](QDialog* dialog)
        {
            auto* field = dialog->findChild<QLineEdit*>("modelNameEdit");
            auto* apply = dialog->findChild<QPushButton*>("renameModelButton");
            Require(field && apply, "missing rename controls");
            field->clear();
            apply->click();
            Require(dialog->isVisible() && viewport.HistoryRevision() == history, "empty rename accepted");
            field->setText(QStringLiteral("CUBE_30MM (2)"));
            apply->click();
            Require(dialog->isVisible() && viewport.HistoryRevision() == history &&
                    !dialog->findChild<QLabel*>("modelNameHint")->text().isEmpty(), "duplicate rename accepted");
            field->setText(QStringLiteral("  打印零件.stl  "));
            apply->click();
        }, [&]() { double_click(label(0)); });
        Require(viewport.ModelInfo(original)->name == QStringLiteral("打印零件") &&
                list->item(0)->text() == QStringLiteral("打印零件") && label(0)->text() == QStringLiteral("打印零件") &&
                window.isWindowModified() && viewport.HistoryRevision() == history + 1 &&
                !viewport.ModelInfo(original)->selected, "UI rename did not update list/history");
        row(0)->findChild<QToolButton*>("selectModelButton")->click();
        Require(window.findChild<QLabel*>("modelName")->text() == QStringLiteral("打印零件"), "renamed details missing");
        Trigger(window, "undoAction");
        Require(list->item(0)->text() == QStringLiteral("cube_30mm") && label(0)->text() == QStringLiteral("cube_30mm") &&
                window.findChild<QLabel*>("modelName")->text() == QStringLiteral("cube_30mm") && window.isWindowModified(),
                "UI rename undo not synchronized");
        history = viewport.HistoryRevision();
        WithDialog([](QDialog* dialog) { dialog->reject(); }, [&]() { double_click(row(0)); });
        Require(viewport.CanRedo() && viewport.HistoryRevision() == history, "cancelled row rename cleared redo");
        Trigger(window, "redoAction");
        Require(label(0)->text() == QStringLiteral("打印零件") &&
                window.findChild<QLabel*>("modelName")->text() == QStringLiteral("打印零件"), "UI rename redo not synchronized");
        row(0)->findChild<QToolButton*>("toggleModelVisibilityButton")->click();
        WithDialog([](QDialog* dialog)
        {
            dialog->findChild<QLineEdit*>("modelNameEdit")->setText(QStringLiteral("隐藏打印件"));
            dialog->findChild<QPushButton*>("renameModelButton")->click();
        }, [&]() { double_click(label(0)); });
        Require(!viewport.ModelInfo(original)->visible && viewport.ModelInfo(original)->selected &&
                label(0)->text() == QStringLiteral("隐藏打印件"), "hidden model rename failed");
        Trigger(window, "saveProjectAction");
        ProjectData saved;
        Require(!window.isWindowModified() && ProjectFile::Read(path, saved, error) &&
                saved.models[0].name == QStringLiteral("隐藏打印件") &&
                saved.models[1].name == QStringLiteral("cube_30mm (2)"), "UI names not saved");
        WithFile(path, [&]() { Trigger(window, "openProjectAction"); });
        Require(list->item(0)->text() == QStringLiteral("隐藏打印件") &&
                label(1)->text() == QStringLiteral("cube_30mm (2)"), "UI reopen lost names");
        row(0)->findChild<QToolButton*>("selectModelButton")->click();
        Trigger(window, "duplicateModelsAction");
        Require(label(2)->text() == QStringLiteral("隐藏打印件 (2)"), "UI copy shares source name");
    });
    qApp->removeEventFilter(&filter);
}
} // namespace

int main(int argc, char* argv[])
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir directory;
    Require(directory.isValid(), "create test directory");
    if (argc == 3 && (QString::fromLocal8Bit(argv[1]) == QStringLiteral("--capture-welcome") ||
                     QString::fromLocal8Bit(argv[1]) == QStringLiteral("--capture-welcome-recent")))
    {
        const QString record = directory.path() + QStringLiteral("/preview_recent.json");
        if (QString::fromLocal8Bit(argv[1]) == QStringLiteral("--capture-welcome-recent"))
        {
            QString error;
            for (const QString& name : {QStringLiteral("产品展示"), QStringLiteral("装配测试"),
                                       QStringLiteral("底座模型"), QStringLiteral("模型排布"), QStringLiteral("机械支架")})
            {
                const QString path = directory.path() + QStringLiteral("/") + name + QStringLiteral(".glproj");
                Require(ProjectFile::Write(path, ProjectData{}, error), qPrintable(error));
                Require(RecentProjects::Remember(path, error, record), qPrintable(error));
            }
        }
        WelcomeDialog welcome(record);
        Require(welcome.grab().save(QString::fromLocal8Bit(argv[2])), "capture welcome");
        return 0;
    }
    const QString capture_mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (argc == 3 && (capture_mode == QStringLiteral("--capture-history") ||
                     capture_mode == QStringLiteral("--capture-toolbar") ||
                     capture_mode == QStringLiteral("--capture-platform") ||
                     capture_mode == QStringLiteral("--capture-rename") ||
                     capture_mode == QStringLiteral("--capture-array-dialog") ||
                     capture_mode == QStringLiteral("--capture-array-view") ||
                     capture_mode == QStringLiteral("--capture-model-transform") ||
                     capture_mode.startsWith(QStringLiteral("--capture-gizmo-"))))
    {
        HideTestWindows filter;
        app.installEventFilter(&filter);
        SceneViewport viewport;
        Require(viewport.Initialize(), "initialize preview viewport");
        std::string load_error;
        QString error;
        glm::vec3 size;
        Require(viewport.LoadStl(TEST_ASSET_DIR "/cube_30mm.stl", load_error, size), "preview import cube");
        Require(viewport.LoadStl(TEST_ASSET_DIR "/sphere_30mm.stl", load_error, size), "preview import sphere");
        const auto infos = viewport.ModelInfos();
        Require(viewport.BeginModelMove(infos[0].id), "preview begin drag");
        Require(viewport.MoveModelTo(infos[0].id, {32.5f, -16.25f, 0}), "preview move");
        viewport.EndModelMove();
        viewport.RemoveStl(infos[1].id);
        Require(viewport.Undo(error), "preview undo");
        if (capture_mode == QStringLiteral("--capture-array-view"))
        {
            viewport.NewProject();
            Require(viewport.LoadStl(TEST_ASSET_DIR "/cube_30mm.stl", load_error, size), "array preview import");
            viewport.SetModelSelected(viewport.ModelInfos()[0].id, true);
            Require(viewport.SetPlateSizeMm({160, 130}, error) && viewport.ArraySelectedModels(2, 3, {5, 8}, error), "array preview generate");
            viewport.ResetView();
            Require(viewport.RenderFrame() && viewport.RenderFrame(), "array preview render");
            const auto& io = ImGui::GetIO();
            const int width = static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x);
            const int height = static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y);
            QImage pixels(width, height, QImage::Format_RGBA8888);
            glReadBuffer(GL_FRONT);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.bits());
            Require(pixels.mirrored().save(QString::fromLocal8Bit(argv[2])), "capture array view");
            return 0;
        }
        if (capture_mode.startsWith(QStringLiteral("--capture-gizmo-")))
        {
            const auto id = infos[0].id;
            viewport.SetModelSelected(id, true);
            Require(viewport.BeginTransformEditing(id, capture_mode.endsWith("rotate") ? TransformMode::Rotate :
                capture_mode.endsWith("scale") ? TransformMode::Scale : TransformMode::Translate), "preview gizmo session");
            Require(viewport.RenderFrame() && viewport.RenderFrame(), "gizmo preview render");
            const auto& io = ImGui::GetIO();
            const int width = static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x);
            const int height = static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y);
            QImage pixels(width, height, QImage::Format_RGBA8888);
            glReadBuffer(GL_FRONT);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.bits());
            Require(pixels.mirrored().save(QString::fromLocal8Bit(argv[2])), "capture gizmo view");
            return 0;
        }
        viewport.SetModelSelected(infos[0].id, true);
        SceneEditorWindow window(viewport);
        Require(window.IsReady(), "preview native wrapper");
        if (capture_mode == QStringLiteral("--capture-toolbar"))
        {
            window.show();
            app.processEvents();
            auto* toolbar = window.findChild<QToolBar*>("mainToolBar");
            Require(toolbar && toolbar->grab().save(QString::fromLocal8Bit(argv[2])), "capture toolbar");
        }
        else if (capture_mode == QStringLiteral("--capture-model-transform"))
        {
            window.show();
            app.processEvents();
            window.findChild<QPushButton*>("modelRotateButton")->click();
            auto* dialog = window.findChild<QDialog*>("modelTransformDialog");
            Require(dialog && dialog->grab().save(QString::fromLocal8Bit(argv[2])), "capture transform window");
            auto* dock = window.findChild<QDockWidget*>("stlModelDock");
            Require(dock && dock->grab().save(QString::fromLocal8Bit(argv[2]) + ".dock.png"), "capture transform buttons");
            dialog->reject();
        }
        else if (capture_mode == QStringLiteral("--capture-rename"))
        {
            Require(viewport.DuplicateSelectedModels(error), qPrintable(error));
            // The window refreshes its list after undo/redo.
            Trigger(window, "undoAction");
            Trigger(window, "redoAction");
            auto* dock = window.findChild<QDockWidget*>("stlModelDock");
            Require(dock->grab().save(QString::fromLocal8Bit(argv[2]) + ".dock.png"), "capture model names");
            auto* list = window.findChild<QListWidget*>("stlModelList");
            auto* name = list->itemWidget(list->item(0))->findChild<QLabel*>("modelRowName");
            WithDialog([&](QDialog* dialog)
            {
                Require(dialog->grab().save(QString::fromLocal8Bit(argv[2])), "capture rename dialog");
                dialog->reject();
            }, [&]()
            {
                QMouseEvent event(QEvent::MouseButtonDblClick, QPointF(8, 8),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(name, &event);
            });
        }
        else if (capture_mode == QStringLiteral("--capture-array-dialog"))
        {
            WithDialog([&](QDialog* dialog)
            {
                dialog->findChild<QSpinBox*>("arrayColumnsSpinBox")->setValue(3);
                Require(dialog->grab().save(QString::fromLocal8Bit(argv[2])), "capture array dialog");
                dialog->reject();
            }, [&]() { Trigger(window, "modelArrayAction"); });
        }
        else if (capture_mode == QStringLiteral("--capture-platform"))
        {
            Require(viewport.SetPlateSizeMm({240, 160}, error), qPrintable(error));
            WithDialog([&](QDialog* dialog)
            {
                Require(dialog->grab().save(QString::fromLocal8Bit(argv[2])), "capture platform size dialog");
                dialog->reject();
            }, [&]() { Trigger(window, "plateSizeAction"); });
        }
        else
        {
            Trigger(window, "operationHistoryAction");
            auto* dialog = window.findChild<QDialog*>("operationHistoryDialog");
            Require(dialog && dialog->grab().save(QString::fromLocal8Bit(argv[2])), "capture history");
        }
        app.removeEventFilter(&filter);
        return 0;
    }
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--ui"))
        UiTests(directory.path());
    else
    {
        FileTests(directory.path());
        RecentProjectTests(directory.path());
        SceneTests(directory.path());
        PlateTests(directory.path());
        TransformTests(directory.path());
        HistoryTests(directory.path());
        DuplicationTests(directory.path());
        PlatePlacementTests(directory.path());
        NamingTests(directory.path());
        Run("welcome New button enters an unnamed project", [&]()
        {
            WelcomeDialog welcome(directory.path() + QStringLiteral("/empty_recent.json"));
            auto* button = welcome.findChild<QPushButton*>("newProjectButton");
            Require(button != nullptr, "welcome New button absent");
            button->click();
            Require(welcome.result() == QDialog::Accepted && welcome.ProjectPath().isEmpty(),
                    "welcome New did not accept");
        });
    }
    return failures ? 1 : 0;
}
