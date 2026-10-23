#include "slice_engine.h"

#include <pyslm/types.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace
{
constexpr double kPi = 3.14159265358979323846;

double LineLength(const SliceHatchLine& line)
{
    return std::hypot(line.end[0] - line.start[0],
                      line.end[1] - line.start[1]);
}

double LineAngleDegrees(const SliceHatchLine& line)
{
    double angle = std::atan2(line.end[1] - line.start[1],
                              line.end[0] - line.start[0]) *
                   180.0 / kPi;
    angle = std::fmod(angle, 180.0);
    if (angle < 0.0)
        angle += 180.0;
    return angle;
}

double LongestHatchAngle(const SliceLayer& layer)
{
    const SliceHatchLine* longest = nullptr;
    double longest_length = -1.0;
    for (const SliceHatchLine& line : layer.hatches)
    {
        const double length = LineLength(line);
        if (length > longest_length)
        {
            longest_length = length;
            longest = &line;
        }
    }
    return longest ? LineAngleDegrees(*longest) : -1.0;
}

double AcuteLineAngleDifference(double a, double b)
{
    double difference = std::fmod(std::abs(a - b), 180.0);
    if (difference > 90.0)
        difference = 180.0 - difference;
    return difference;
}
SliceModelInput MakeCube(const std::string& name, double x_offset_mm,
                         std::uint64_t id = 1)
{
    SliceModelInput model;
    model.id = id;
    model.name = name;
    const std::array<std::array<double, 3>, 8> vertices{{
        {{x_offset_mm + 0.0, 0.0, 0.0}},
        {{x_offset_mm + 10.0, 0.0, 0.0}},
        {{x_offset_mm + 10.0, 10.0, 0.0}},
        {{x_offset_mm + 0.0, 10.0, 0.0}},
        {{x_offset_mm + 0.0, 0.0, 10.0}},
        {{x_offset_mm + 10.0, 0.0, 10.0}},
        {{x_offset_mm + 10.0, 10.0, 10.0}},
        {{x_offset_mm + 0.0, 10.0, 10.0}}
    }};
    const std::array<std::array<int, 3>, 12> faces{{
        {{0, 2, 1}}, {{0, 3, 2}},
        {{4, 5, 6}}, {{4, 6, 7}},
        {{0, 1, 5}}, {{0, 5, 4}},
        {{1, 2, 6}}, {{1, 6, 5}},
        {{2, 3, 7}}, {{2, 7, 6}},
        {{3, 0, 4}}, {{3, 4, 7}}
    }};
    model.triangles.reserve(faces.size());
    for (const auto& face : faces)
    {
        SliceTriangle triangle;
        for (int corner = 0; corner < 3; ++corner)
            triangle.vertices[corner] = vertices[face[corner]];
        model.triangles.push_back(triangle);
    }
    return model;
}

bool CheckLayerLengths(const SliceResult& result, std::size_t expected_contours)
{
    for (const SliceLayer& layer : result.layers)
    {
        if (layer.contours.size() != expected_contours)
        {
            std::cerr << "layer " << layer.index << " has "
                      << layer.contours.size() << " contours, expected "
                      << expected_contours << '\n';
            return false;
        }
        const double expected_length = 40.0 * static_cast<double>(expected_contours);
        if (std::abs(layer.path_length_mm - expected_length) > 0.05)
        {
            std::cerr << "layer " << layer.index << " length "
                      << layer.path_length_mm << ", expected "
                      << expected_length << '\n';
            return false;
        }
    }
    return true;
}
} // namespace

int main()
{
    std::cout << "pyslm version: " << pyslm::version() << '\n';

    SliceSettings settings;
    settings.layer_thickness_mm = 1.0;

    {
        const SliceResult result = SliceBuild({MakeCube("cube", 0.0)}, settings);
        if (!result.ok)
        {
            std::cerr << "single cube failed: " << result.error << '\n';
            return 1;
        }
        if (result.layers.size() != 10)
        {
            std::cerr << "single cube layers: " << result.layers.size()
                      << ", expected 10\n";
            return 2;
        }
        if (!CheckLayerLengths(result, 1))
            return 3;
        if (!result.layers[0].hatches.empty() ||
            result.layers[0].hatch_path_length_mm != 0.0 ||
            result.total_hatch_path_length_mm != 0.0)
        {
            std::cerr << "hatching was generated while disabled\n";
            return 4;
        }
        if (std::abs(result.total_path_length_mm - 400.0) > 0.1)
        {
            std::cerr << "single cube total length: "
                      << result.total_path_length_mm << '\n';
            return 4;
        }
        std::cout << "single cube: " << result.layers.size()
                  << " layers, " << result.total_path_length_mm << " mm\n";
    }

    {
        SliceSettings hatch_settings = settings;
        hatch_settings.hatch.enabled = true;
        hatch_settings.hatch.spacing_mm = 1.0;
        hatch_settings.hatch.angle_deg = 0.0;
        hatch_settings.hatch.layer_angle_increment_deg = 0.0;

        const SliceResult result = SliceBuild(
            {MakeCube("hatch", 0.0, 7)}, hatch_settings);
        if (!result.ok)
        {
            std::cerr << "hatch cube failed: " << result.error << '\n';
            return 10;
        }
        if (result.layers.size() != 10)
        {
            std::cerr << "hatch cube layers: " << result.layers.size()
                      << ", expected 10\n";
            return 11;
        }
        for (const SliceLayer& layer : result.layers)
        {
            if (layer.contours.size() != 1 || layer.hatches.empty())
            {
                std::cerr << "hatch layer " << layer.index
                          << " has no contours or fills\n";
                return 12;
            }
            for (const SliceHatchLine& hatch : layer.hatches)
                if (hatch.model_id != 7)
                {
                    std::cerr << "hatch model id was not copied\n";
                    return 20;
                }
            if (std::abs(layer.contour_path_length_mm - 40.0) > 0.05 ||
                layer.hatch_path_length_mm <= 0.0)
            {
                std::cerr << "hatch layer " << layer.index
                          << " lengths are wrong\n";
                return 13;
            }
            if (std::abs(layer.path_length_mm -
                         (layer.contour_path_length_mm +
                          layer.hatch_path_length_mm)) > 1.0e-6)
            {
                std::cerr << "hatch layer " << layer.index
                          << " total length is wrong\n";
                return 14;
            }
        }
        if (std::abs(result.total_path_length_mm -
                     (result.total_contour_path_length_mm +
                      result.total_hatch_path_length_mm)) > 1.0e-6)
        {
            std::cerr << "hatch result total length is wrong\n";
            return 15;
        }
        std::cout << "hatch cube: " << result.layers[0].hatches.size()
                  << " lines on first layer, "
                  << result.total_hatch_path_length_mm << " mm fill\n";
    }

    {
        SliceSettings angle_settings = settings;
        angle_settings.hatch.enabled = true;
        angle_settings.hatch.spacing_mm = 1.0;
        angle_settings.hatch.angle_deg = 0.0;
        angle_settings.hatch.layer_angle_increment_deg = 67.0;

        const SliceResult result = SliceBuild(
            {MakeCube("angle", 0.0)}, angle_settings);
        if (!result.ok || result.layers.size() < 2)
        {
            std::cerr << "angle cube failed\n";
            return 16;
        }
        const double first_angle = LongestHatchAngle(result.layers[0]);
        const double second_angle = LongestHatchAngle(result.layers[1]);
        if (first_angle < 0.0 || second_angle < 0.0)
        {
            std::cerr << "angle cube has no long hatch line\n";
            return 17;
        }
        const double difference =
            AcuteLineAngleDifference(first_angle, second_angle);
        if (std::abs(difference - 67.0) > 1.0)
        {
            std::cerr << "layer angle difference " << difference
                      << ", expected 67\n";
            return 18;
        }
        std::cout << "hatch angle step: " << difference << " degrees\n";
    }

    {
        SliceSettings capped_settings = settings;
        capped_settings.hatch.enabled = true;
        capped_settings.hatch.spacing_mm = 0.01;
        capped_settings.max_hatch_segments = 10;
        const SliceResult result = SliceBuild(
            {MakeCube("capped", 0.0)}, capped_settings);
        if (result.ok || result.error.find("填充") == std::string::npos)
        {
            std::cerr << "hatch segment cap was not enforced\n";
            return 19;
        }
    }

    {
        const SliceResult result = SliceBuild(
            {MakeCube("left", 0.0, 1), MakeCube("right", 20.0, 2)}, settings);
        if (!result.ok)
        {
            std::cerr << "two cubes failed: " << result.error << '\n';
            return 5;
        }
        if (result.layers.size() != 10)
        {
            std::cerr << "two cubes layers: " << result.layers.size()
                      << ", expected 10\n";
            return 6;
        }
        if (!CheckLayerLengths(result, 2))
            return 7;
        std::cout << "two cubes: " << result.layers.size()
                  << " layers, " << result.total_path_length_mm << " mm\n";
    }

    {
        const SliceResult result = SliceBuild({}, settings);
        if (result.ok || result.error.empty())
        {
            std::cerr << "empty input should fail\n";
            return 8;
        }
    }

    {
        std::size_t calls = 0;
        const SliceResult result = SliceBuild(
            {MakeCube("cancel", 0.0)}, settings,
            [&calls](std::size_t, std::size_t) { return ++calls <= 3; });
        if (!result.cancelled || result.ok)
        {
            std::cerr << "cancellation was not reported\n";
            return 9;
        }
    }

    std::cout << "PASSED\n";
    return 0;
}
