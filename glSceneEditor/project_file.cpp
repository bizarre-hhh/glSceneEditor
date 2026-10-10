#include "project_file.h"

#include <QDataStream>
#include <QFile>
#include <QSaveFile>

#include <cmath>
#include <utility>

namespace
{
constexpr quint32 kMagic = 0x474C5052; // GLPR
constexpr quint32 kVersion = 3;
constexpr quint32 kMaxModels = 10000;
constexpr quint32 kMaxPathBytes = 1024 * 1024;
// Qt 5 QByteArray is int-sized. Bound allocations before reading file data.
constexpr quint32 kMaxModelBytes = 512 * 1024 * 1024;

void Configure(QDataStream& stream)
{
    stream.setVersion(QDataStream::Qt_5_12);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
}

bool ReadBytes(QDataStream& stream, QByteArray& bytes, quint32 limit)
{
    quint32 size = 0;
    stream >> size;
    if (stream.status() != QDataStream::Ok || size > limit ||
        size > static_cast<quint64>(stream.device()->bytesAvailable()))
        return false;
    bytes.resize(static_cast<int>(size));
    return stream.readRawData(bytes.data(), bytes.size()) == bytes.size();
}

void WriteBytes(QDataStream& stream, const QByteArray& bytes)
{
    stream << static_cast<quint32>(bytes.size());
    stream.writeRawData(bytes.constData(), static_cast<int>(bytes.size()));
}

bool ReadBool(QDataStream& stream, bool& value)
{
    quint8 byte = 0;
    stream >> byte;
    value = byte != 0;
    return stream.status() == QDataStream::Ok && byte <= 1;
}

void ReadVector(QDataStream& stream, glm::vec3& value)
{
    stream >> value.x >> value.y >> value.z;
}

void WriteVector(QDataStream& stream, const glm::vec3& value)
{
    stream << value.x << value.y << value.z;
}

bool FiniteVector(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
} // namespace

QString ProjectFile::Filter()
{
    return QStringLiteral("glSceneEditor 工程 (*.glproj)");
}

QString ProjectFile::DefaultOpenDirectory()
{
    return QStringLiteral("D:/vs_cmake_proj/glSceneEditor/glproj");
}

bool ProjectFile::Validate(const ProjectData& project, QString& error)
{
    error.clear();
    if (!PlateDimensions::IsValid(project.plate_size_mm))
    {
        error = QStringLiteral("平台长度和宽度必须在 1 至 10000 mm 之间。");
        return false;
    }
    bool valid = project.models.size() <= kMaxModels;
    for (const auto& model : project.models)
    {
        valid = valid && !model.source_path.isEmpty() &&
            model.source_path.toUtf8().size() <= kMaxPathBytes &&
            !model.stl_data.isEmpty() && model.stl_data.size() <= kMaxModelBytes &&
            FiniteVector(model.translation_mm) && FiniteVector(model.scale) &&
            FiniteVector(model.rotation_degrees) &&
            std::isfinite(model.placement_center_mm.x) && std::isfinite(model.placement_center_mm.y) &&
            model.scale.x > 0.0f && model.scale.y > 0.0f && model.scale.z > 0.0f;
    }
    if (!valid)
        error = QStringLiteral("工程包含无效的模型数据或变换参数。");
    return valid;
}

bool ProjectFile::Read(const QString& path, ProjectData& project, QString& error)
{
    error.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = QStringLiteral("无法读取工程：%1").arg(file.errorString());
        return false;
    }
    QDataStream stream(&file);
    Configure(stream);
    quint32 magic = 0, version = 0, count = 0;
    stream >> magic >> version;
    if (stream.status() != QDataStream::Ok || magic != kMagic)
    {
        error = QStringLiteral("这不是有效的 glSceneEditor 工程文件。");
        return false;
    }
    if (version != 1 && version != 2 && version != kVersion)
    {
        error = QStringLiteral("不支持工程版本 %1，请使用兼容的软件版本。").arg(version);
        return false;
    }

    // Parse into a temporary document; failures never replace the caller's data.
    ProjectData loaded;
    bool valid = true;
    if (version == 1)
    {
        // Consume the legacy camera/display fields; only geometry is restored.
        float ignored = 0.0f;
        for (int i = 0; i < 12; ++i)
            stream >> ignored;
        bool flag = false;
        valid = ReadBool(stream, flag) && ReadBool(stream, flag);
    }
    if (version >= 3)
        stream >> loaded.plate_size_mm.x >> loaded.plate_size_mm.y;
    stream >> count;
    valid = valid && stream.status() == QDataStream::Ok && count <= kMaxModels;
    for (quint32 i = 0; valid && i < count; ++i)
    {
        ProjectModel model;
        QByteArray utf8_path;
        valid = ReadBytes(stream, utf8_path, kMaxPathBytes) &&
                ReadBytes(stream, model.stl_data, kMaxModelBytes);
        if (!valid)
            break;
        model.source_path = QString::fromUtf8(utf8_path);
        ReadVector(stream, model.translation_mm);
        ReadVector(stream, model.scale);
        ReadVector(stream, model.rotation_degrees);
        if (version >= 3)
            stream >> model.placement_center_mm.x >> model.placement_center_mm.y;
        if (version == 1)
        {
            bool flag = false;
            valid = ReadBool(stream, flag) && ReadBool(stream, flag) && ReadBool(stream, flag);
            quint64 ignored_order = 0;
            stream >> ignored_order;
        }
        valid = valid && stream.status() == QDataStream::Ok;
        loaded.models.push_back(std::move(model));
    }
    if (!valid || stream.status() != QDataStream::Ok || !file.atEnd())
    {
        error = QStringLiteral("工程文件已损坏、数据不完整或超过支持的大小。");
        return false;
    }
    if (!Validate(loaded, error))
        return false;
    project = std::move(loaded);
    return true;
}

bool ProjectFile::Write(const QString& path, const ProjectData& project, QString& error)
{
    if (!Validate(project, error))
        return false;
    // Atomic replacement: a failed write leaves an existing project intact.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        error = QStringLiteral("无法保存工程：%1").arg(file.errorString());
        return false;
    }
    QDataStream stream(&file);
    Configure(stream);
    stream << kMagic << kVersion;
    stream << project.plate_size_mm.x << project.plate_size_mm.y;
    stream << static_cast<quint32>(project.models.size());
    for (const auto& model : project.models)
    {
        WriteBytes(stream, model.source_path.toUtf8());
        WriteBytes(stream, model.stl_data);
        WriteVector(stream, model.translation_mm);
        WriteVector(stream, model.scale);
        WriteVector(stream, model.rotation_degrees);
        stream << model.placement_center_mm.x << model.placement_center_mm.y;
    }
    if (stream.status() != QDataStream::Ok || !file.commit())
    {
        error = QStringLiteral("保存工程失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}
