#include "slice_engine.h"

#include <pyslm/core.hpp>
#include <pyslm/hatching.hpp>
#include <pyslm/types.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace
{
constexpr double kLayerPlaneEpsilon = 1.0e-9;
constexpr double kPathLengthTolerance = 1.0e-12;

struct Bounds3
{
    double min[3]{std::numeric_limits<double>::max(),
                  std::numeric_limits<double>::max(),
                  std::numeric_limits<double>::max()};
    double max[3]{std::numeric_limits<double>::lowest(),
                  std::numeric_limits<double>::lowest(),
                  std::numeric_limits<double>::lowest()};
};

bool ComputeBounds(const SliceModelInput& model, Bounds3& bounds)
{
    bool has_vertex = false;
    for (const SliceTriangle& triangle : model.triangles)
        for (const auto& vertex : triangle.vertices)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(vertex[axis]))
                    return false;
                bounds.min[axis] = std::min(bounds.min[axis], vertex[axis]);
                bounds.max[axis] = std::max(bounds.max[axis], vertex[axis]);
            }
            has_vertex = true;
        }
    return has_vertex;
}

int FirstLayerIndex(double min_z_mm, double layer_thickness_mm)
{
    const double start = std::max(0.0, min_z_mm);
    const double value =
        std::ceil((start - layer_thickness_mm * 0.5) / layer_thickness_mm +
                  kLayerPlaneEpsilon);
    if (value <= 0.0)
        return 0;
    if (value >= static_cast<double>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(value);
}

int LastLayerIndex(double max_z_mm, double layer_thickness_mm)
{
    const double value =
        std::floor((max_z_mm - layer_thickness_mm * 0.5) / layer_thickness_mm -
                   kLayerPlaneEpsilon);
    if (value < 0.0)
        return -1;
    if (value >= static_cast<double>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(value);
}

double PathLength(const SlicePath& path)
{
    double length = 0.0;
    for (std::size_t i = 1; i < path.points.size(); ++i)
    {
        const double dx = path.points[i][0] - path.points[i - 1][0];
        const double dy = path.points[i][1] - path.points[i - 1][1];
        length += std::hypot(dx, dy);
    }
    return length;
}

double HatchLength(const SliceHatchLine& line)
{
    return std::hypot(line.end[0] - line.start[0],
                      line.end[1] - line.start[1]);
}

bool SamePoint(const std::array<double, 2>& a, const std::array<double, 2>& b)
{
    return std::abs(a[0] - b[0]) <= kPathLengthTolerance &&
           std::abs(a[1] - b[1]) <= kPathLengthTolerance;
}

SlicePath ConvertPath(const pyslm::Path& source)
{
    SlicePath path;
    path.points.reserve(source.size());
    for (const pyslm::Vec2& point : source)
        path.points.push_back({point.x, point.y});
    if (path.points.size() >= 2 && !SamePoint(path.points.front(), path.points.back()))
        path.points.push_back(path.points.front());
    return path;
}

void AppendPolygons(const pyslm::Polygons& polygons, std::vector<SlicePath>& paths)
{
    for (const pyslm::Polygon& polygon : polygons)
    {
        if (!polygon.exterior.empty())
        {
            SlicePath path = ConvertPath(polygon.exterior);
            if (path.points.size() >= 2)
                paths.push_back(std::move(path));
        }
        for (const pyslm::Path& interior : polygon.interiors)
        {
            SlicePath path = ConvertPath(interior);
            if (path.points.size() >= 2)
                paths.push_back(std::move(path));
        }
    }
}

struct GeometryModel
{
    const SliceModelInput* input = nullptr;
    Bounds3 bounds;
};

struct PreparedModel
{
    GeometryModel geometry;
    int first_layer = 0;
    int last_layer = 0;
    std::unique_ptr<pyslm::Part> part;
    std::unique_ptr<pyslm::hatching::Hatcher> hatcher;
};

std::string ModelLabel(const SliceModelInput& model)
{
    return model.name.empty() ? std::string("未命名模型") : model.name;
}
} // namespace

SliceResult SliceBuild(const std::vector<SliceModelInput>& models,
                       const SliceSettings& settings,
                       const SliceProgressCallback& progress)
{
    SliceResult result;
    result.model_count = models.size();

    const double thickness = settings.layer_thickness_mm;
    if (!std::isfinite(thickness) || thickness < kMinLayerThicknessMm ||
        thickness > kMaxLayerThicknessMm)
    {
        result.error = "层厚必须在 0.001 mm 到 10 mm 之间。";
        return result;
    }

    if (settings.hatch.enabled)
    {
        if (!std::isfinite(settings.hatch.spacing_mm) ||
            settings.hatch.spacing_mm < kMinHatchSpacingMm ||
            settings.hatch.spacing_mm > kMaxHatchSpacingMm)
        {
            result.error = "扫描间距必须在 0.001 mm 到 10 mm 之间。";
            return result;
        }
        if (!std::isfinite(settings.hatch.angle_deg) ||
            !std::isfinite(settings.hatch.layer_angle_increment_deg))
        {
            result.error = "扫描角度和层间角度增量必须是有限数值。";
            return result;
        }
        if (!std::isfinite(settings.hatch.spot_compensation_mm) ||
            settings.hatch.spot_compensation_mm < 0.0)
        {
            result.error = "光斑补偿必须是非负有限数值。";
            return result;
        }
    }

    if (models.empty())
    {
        result.error = "没有可切片的模型。";
        return result;
    }

    std::vector<GeometryModel> geometry_models;
    geometry_models.reserve(models.size());
    double global_max_z = std::numeric_limits<double>::lowest();
    for (const SliceModelInput& model : models)
    {
        GeometryModel geometry;
        geometry.input = &model;
        if (model.triangles.empty() || !ComputeBounds(model, geometry.bounds))
        {
            result.issues.push_back({model.id, ModelLabel(model),
                "模型没有有效的三角面片。"});
            continue;
        }
        global_max_z = std::max(global_max_z, geometry.bounds.max[2]);
        geometry_models.push_back(geometry);
    }

    if (geometry_models.empty())
    {
        result.error = "没有可切片的有效几何。";
        return result;
    }
    if (global_max_z <= 0.0)
    {
        result.error = "所有模型都在平台下方，没有可切片的 Z 范围。";
        return result;
    }
    result.max_z_mm = global_max_z;

    const int global_last_layer = LastLayerIndex(global_max_z, thickness);
    if (global_last_layer < 0)
    {
        result.error = "模型顶部低于第一层切平面，没有生成切片。";
        return result;
    }
    if (static_cast<std::size_t>(global_last_layer) + 1 > kMaxSliceLayers)
    {
        result.error = "预计层数超过 20000，请增大层厚后再切片。";
        return result;
    }

    std::shared_ptr<pyslm::hatching::AlternateSort> hatch_sort;
    if (settings.hatch.enabled)
        hatch_sort = std::make_shared<pyslm::hatching::AlternateSort>();

    std::vector<PreparedModel> prepared;
    prepared.reserve(geometry_models.size());
    for (const GeometryModel& geometry : geometry_models)
    {
        PreparedModel model;
        model.geometry = geometry;
        model.first_layer = FirstLayerIndex(geometry.bounds.min[2], thickness);
        model.last_layer = LastLayerIndex(geometry.bounds.max[2], thickness);
        model.last_layer = std::min(model.last_layer, global_last_layer);

        if (model.first_layer > model.last_layer)
        {
            result.issues.push_back({geometry.input->id,
                ModelLabel(*geometry.input),
                "模型高度小于层厚，未跨越任何切平面。"});
            continue;
        }

        try
        {
            pyslm::Mesh mesh;
            mesh.vertices.reserve(geometry.input->triangles.size() * 3);
            mesh.faces.reserve(geometry.input->triangles.size());
            for (const SliceTriangle& triangle : geometry.input->triangles)
            {
                const std::uint32_t base =
                    static_cast<std::uint32_t>(mesh.vertices.size());
                for (const auto& vertex : triangle.vertices)
                    mesh.vertices.push_back({vertex[0], vertex[1], vertex[2]});
                mesh.faces.push_back({base, base + 1, base + 2});
            }

            model.part = std::make_unique<pyslm::Part>(ModelLabel(*geometry.input));
            model.part->setGeometry(mesh, true, true);
            if (!model.part->hasGeometry())
            {
                result.issues.push_back({geometry.input->id,
                    ModelLabel(*geometry.input),
                    "库没有接受该模型的网格。"});
                continue;
            }

            if (settings.hatch.enabled)
            {
                model.hatcher = std::make_unique<pyslm::hatching::Hatcher>();
                pyslm::hatching::Hatcher& hatcher = *model.hatcher;
                hatcher.hatchingEnabled = true;
                // Keep the exact slice contour in the preview and only inset
                // the hatch region by the spot compensation.
                hatcher.numOuterContours = 0;
                hatcher.numInnerContours = 0;
                hatcher.spotCompensation = settings.hatch.spot_compensation_mm;
                hatcher.contourOffset = 0.0;
                hatcher.volumeOffsetHatch = 0.0;
                hatcher.hatchDistance = settings.hatch.spacing_mm;
                hatcher.hatchAngle = settings.hatch.angle_deg;
                // The angle is set per layer below; letting the hatcher add
                // layerAngleIncrement would apply it on every call instead.
                hatcher.layerAngleIncrement = 0.0;
                hatcher.hatchSortMethod = hatch_sort;
            }
        }
        catch (const std::exception& error)
        {
            result.issues.push_back({geometry.input->id,
                ModelLabel(*geometry.input), error.what()});
            continue;
        }

        prepared.push_back(std::move(model));
    }

    if (prepared.empty())
    {
        result.error = "所有模型都无法生成切片，请检查模型几何。";
        return result;
    }

    std::size_t total_calls = 0;
    for (const PreparedModel& model : prepared)
        total_calls += static_cast<std::size_t>(model.last_layer - model.first_layer + 1);
    result.slice_call_count = total_calls;

    std::vector<std::vector<SlicePath>> layer_contours(
        static_cast<std::size_t>(global_last_layer) + 1);
    std::vector<std::vector<SliceHatchLine>> layer_hatches(
        static_cast<std::size_t>(global_last_layer) + 1);
    std::size_t completed_calls = 0;
    std::size_t total_hatch_segments = 0;

    pyslm::SliceOptions slice_options;
    slice_options.fixPolygons = true;
    slice_options.simplificationFactor.reset();

    for (PreparedModel& model : prepared)
    {
        std::vector<std::vector<SlicePath>> model_contours(
            static_cast<std::size_t>(global_last_layer) + 1);
        std::vector<std::vector<SliceHatchLine>> model_hatches(
            static_cast<std::size_t>(global_last_layer) + 1);
        bool produced_contours = false;
        bool model_failed = false;
        int last_attempted_layer = model.first_layer - 1;
        for (int layer = model.first_layer; layer <= model.last_layer; ++layer)
        {
            last_attempted_layer = layer;
            if (progress && !progress(completed_calls, total_calls))
            {
                result.cancelled = true;
                result.error = "已取消";
                return result;
            }

            const double z_mm =
                thickness * 0.5 + static_cast<double>(layer) * thickness;
            std::vector<SlicePath> paths;
            std::vector<SliceHatchLine> hatch_lines;
            try
            {
                const pyslm::Polygons polygons =
                    model.part->getVectorSlicePolygons(z_mm, slice_options);
                AppendPolygons(polygons, paths);

                if (settings.hatch.enabled && model.hatcher && !polygons.empty())
                {
                    model.hatcher->hatchAngle =
                        settings.hatch.angle_deg +
                        static_cast<double>(layer) *
                            settings.hatch.layer_angle_increment_deg;
                    const pyslm::Paths boundary =
                        pyslm::Part::path2DToPathList(polygons);
                    const auto hatch_layer = model.hatcher->hatch(boundary);
                    if (hatch_layer)
                    {
                        for (const pyslm::LayerGeometry& geometry :
                             hatch_layer->geometry)
                        {
                            if (geometry.kind != pyslm::LayerGeometryType::Hatch)
                                continue;
                            if (geometry.coords.size() % 2 != 0)
                                throw std::runtime_error(
                                    "Hatch coordinates are not paired");
                            for (std::size_t i = 0;
                                 i < geometry.coords.size(); i += 2)
                            {
                                if (total_hatch_segments >=
                                    settings.max_hatch_segments)
                                {
                                    result.error =
                                        "填充扫描线超过上限（" +
                                        std::to_string(
                                            settings.max_hatch_segments) +
                                        " 条），请增大扫描间距或层厚后再切片。";
                                    return result;
                                }
                                SliceHatchLine line;
                                line.model_id = model.geometry.input->id;
                                line.start = {geometry.coords[i].x,
                                              geometry.coords[i].y};
                                line.end = {geometry.coords[i + 1].x,
                                            geometry.coords[i + 1].y};
                                hatch_lines.push_back(line);
                                ++total_hatch_segments;
                            }
                        }
                    }
                }
            }
            catch (const std::exception& error)
            {
                result.issues.push_back({model.geometry.input->id,
                    ModelLabel(*model.geometry.input), error.what()});
                model_failed = true;
            }

            if (model_failed)
                break;

            if (!paths.empty())
            {
                produced_contours = true;
                auto& target = model_contours[static_cast<std::size_t>(layer)];
                target.insert(target.end(),
                              std::make_move_iterator(paths.begin()),
                              std::make_move_iterator(paths.end()));
            }
            if (!hatch_lines.empty())
            {
                auto& target = model_hatches[static_cast<std::size_t>(layer)];
                target.insert(target.end(),
                              std::make_move_iterator(hatch_lines.begin()),
                              std::make_move_iterator(hatch_lines.end()));
            }

            ++completed_calls;
            if (progress && !progress(completed_calls, total_calls))
            {
                result.cancelled = true;
                result.error = "已取消";
                return result;
            }
        }

        if (model_failed)
        {
            completed_calls += static_cast<std::size_t>(
                model.last_layer - last_attempted_layer + 1);
            continue;
        }

        if (!produced_contours)
        {
            result.issues.push_back({model.geometry.input->id,
                ModelLabel(*model.geometry.input),
                "没有生成任何轮廓，可能整体在平台下方或高度小于层厚。"});
            continue;
        }

        for (std::size_t layer = 0; layer < model_contours.size(); ++layer)
        {
            auto& contour_target = layer_contours[layer];
            contour_target.insert(
                contour_target.end(),
                std::make_move_iterator(model_contours[layer].begin()),
                std::make_move_iterator(model_contours[layer].end()));
            auto& hatch_target = layer_hatches[layer];
            hatch_target.insert(
                hatch_target.end(),
                std::make_move_iterator(model_hatches[layer].begin()),
                std::make_move_iterator(model_hatches[layer].end()));
        }
    }

    for (std::size_t layer = 0; layer < layer_contours.size(); ++layer)
    {
        if (layer_contours[layer].empty())
            continue;

        SliceLayer slice_layer;
        slice_layer.index = result.layers.size() + 1;
        slice_layer.grid_index = static_cast<std::int64_t>(layer);
        slice_layer.z_mm =
            thickness * 0.5 + static_cast<double>(layer) * thickness;
        slice_layer.contours = std::move(layer_contours[layer]);
        slice_layer.hatches = std::move(layer_hatches[layer]);
        for (const SlicePath& path : slice_layer.contours)
            slice_layer.contour_path_length_mm += PathLength(path);
        for (const SliceHatchLine& line : slice_layer.hatches)
            slice_layer.hatch_path_length_mm += HatchLength(line);
        slice_layer.path_length_mm =
            slice_layer.contour_path_length_mm +
            slice_layer.hatch_path_length_mm;
        result.total_contour_path_length_mm +=
            slice_layer.contour_path_length_mm;
        result.total_hatch_path_length_mm +=
            slice_layer.hatch_path_length_mm;
        result.total_path_length_mm += slice_layer.path_length_mm;
        result.layers.push_back(std::move(slice_layer));
    }

    if (result.layers.empty())
    {
        result.error = "没有生成任何有效切片，请检查模型位置和层厚。";
        return result;
    }

    result.ok = true;
    return result;
}
