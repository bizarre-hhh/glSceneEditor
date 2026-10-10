#ifndef PROJECT_FILE_H
#define PROJECT_FILE_H

#include <QByteArray>
#include <QString>
#include <glm/vec3.hpp>
#include "plate_dimensions.h"

#include <vector>

// Original STL bytes are embedded in the document, never external links.
struct ProjectModel
{
    QString source_path;
    QByteArray stl_data;
    glm::vec3 translation_mm{0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 rotation_degrees{0.0f};
    glm::vec2 placement_center_mm{PlateDimensions::kDefaultMm * 0.5f};
};

struct ProjectData
{
    std::vector<ProjectModel> models;
    glm::vec2 plate_size_mm{PlateDimensions::kDefaultMm};
};

namespace ProjectFile
{
QString Filter();
QString DefaultOpenDirectory();
bool Validate(const ProjectData& project, QString& error);
bool Read(const QString& path, ProjectData& project, QString& error);
bool Write(const QString& path, const ProjectData& project, QString& error);
} // namespace ProjectFile

#endif
