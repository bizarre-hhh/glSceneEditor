#ifndef SLICE_ENGINE_H
#define SLICE_ENGINE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Pure C++ slicing layer built on top of pyslm. Coordinates are build-plate
// millimeters: X/Y from the plate origin and Z measured upward from the plate.
struct SliceTriangle
{
    std::array<double, 3> vertices[3];
};

struct SliceModelInput
{
    std::uint64_t id = 0;
    std::string name;
    std::vector<SliceTriangle> triangles;
};

struct SlicePath
{
    std::vector<std::array<double, 2>> points;
};

struct SliceHatchLine
{
    std::uint64_t model_id = 0;
    std::array<double, 2> start;
    std::array<double, 2> end;
};

struct HatchSettings
{
    bool enabled = false;
    double spacing_mm = 0.08;
    double angle_deg = 45.0;
    double layer_angle_increment_deg = 67.0;
    double spot_compensation_mm = 0.08;
};

struct SliceLayer
{
    std::size_t index = 0;      // 1-based index among non-empty layers
    std::int64_t grid_index = 0; // layer plane index on the global Z grid
    double z_mm = 0.0;
    std::vector<SlicePath> contours;
    std::vector<SliceHatchLine> hatches;
    double contour_path_length_mm = 0.0;
    double hatch_path_length_mm = 0.0;
    double path_length_mm = 0.0; // contour + hatch
};

struct SliceSettings
{
    double layer_thickness_mm = 0.03;
    HatchSettings hatch;
    std::size_t max_hatch_segments = 4000000;
};

struct SliceIssue
{
    std::uint64_t model_id = 0;
    std::string name;
    std::string message;
};

struct SliceResult
{
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::size_t model_count = 0;
    std::size_t slice_call_count = 0;
    double max_z_mm = 0.0;
    double total_contour_path_length_mm = 0.0;
    double total_hatch_path_length_mm = 0.0;
    double total_path_length_mm = 0.0;
    std::vector<SliceLayer> layers;
    std::vector<SliceIssue> issues;
};

inline constexpr double kMinLayerThicknessMm = 0.001;
inline constexpr double kMaxLayerThicknessMm = 10.0;
inline constexpr double kMinHatchSpacingMm = 0.001;
inline constexpr double kMaxHatchSpacingMm = 10.0;
inline constexpr std::size_t kMaxSliceLayers = 20000;
inline constexpr std::size_t kMaxSliceHatchSegments = 4000000;

using SliceProgressCallback =
    std::function<bool(std::size_t completed, std::size_t total)>;

// Slice every model on the global layer grid. Layers without any contour are
// omitted. A failed model is reported in SliceResult::issues and does not stop
// the other models. Cancellation discards the partial result.
SliceResult SliceBuild(const std::vector<SliceModelInput>& models,
                       const SliceSettings& settings,
                       const SliceProgressCallback& progress = {});

#endif
