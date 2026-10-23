#pragma once
#include "core.hpp"
#include <optional>
#include <string>

// 支撑高度图的生成、变换与轮廓提取。
namespace pyslm::support {

// 二维高度图（栅格值）。
struct HeightMap {
    std::size_t width=0,height=0;    // 列数、行数
    std::vector<double> values;      // 行优先保存的高度值
    bool float32=false;              // 原始数据是否为 float32
    double& at(std::size_t x,std::size_t y) { return values.at(y*width+x); }
    double at(std::size_t x,std::size_t y) const { return values.at(y*width+x); }
    bool empty() const noexcept { return values.empty(); }
};

// 把网格沿投影方向渲染为高度图。
// resolution 为像素尺寸，flipDir 控制投影方向，bbox 可限定投影范围。
PYSLM_API HeightMap projectHeightMap(const Mesh& mesh,double resolution=0.05,
    bool flipDir=false,std::optional<Bounds3> bbox=std::nullopt);
// 返回 OpenGL 渲染环境信息。
PYSLM_API std::string openGLRendererInfo();
// 交换高度图的行列。
PYSLM_API HeightMap transposeHeightMap(const HeightMap& input);
// 垂直翻转高度图。
PYSLM_API HeightMap flipHeightMapVertical(const HeightMap& input);
// 在四周填充指定宽度的边界，默认值为 1.0。
PYSLM_API HeightMap padHeightMap(const HeightMap& input,std::size_t border=2,double value=1.0);
// 计算梯度幅值并做高斯平滑（sigma 像素，truncate=4，reflect 边界）。
// 运算顺序与原版保持一致。
PYSLM_API HeightMap heightMapGradientMagnitude(const HeightMap& input,double sigma=1.0);
// 在指定高度 level 上提取等值线，可传入掩码。
// 返回点的 Vec2 含义为 (行, 列)，与原版一致。
PYSLM_API Paths findHeightMapContours(const HeightMap& image,double level,
    const HeightMap* mask=nullptr);

} // namespace pyslm::support
