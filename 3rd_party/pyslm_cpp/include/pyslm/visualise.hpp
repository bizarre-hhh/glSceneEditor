#pragma once

#include "core.hpp"
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// 与绘图库无关的场景数据生成接口。
// 调用方可用 Qt、OpenGL 等自行渲染 PlotScene。
namespace pyslm::visualise {

// 绘图图元类型。
enum class PlotPrimitiveKind { Segments,Polyline,Points,Polygons,Arrow };

// 一个可渲染图元。
struct PlotPrimitive {
    PlotPrimitiveKind kind=PlotPrimitiveKind::Polyline; // 图元类型
    std::vector<Vec3> points;                          // 折线/点坐标
    std::vector<std::array<Vec3,2>> segments;          // 线段集合
    std::vector<std::vector<Vec3>> polygons;           // 多边形集合
    std::array<Vec3,2> arrow{}; // 标注箭头：起点（xytext）、终点（xy）
    std::vector<double> colorValues;                   // 逐点颜色值
    std::string color,fillColor,colormap;              // 线色、填充色、色图
    double lineWidth=0.7,pointSize=1.0;                // 线宽、点大小
    bool filled=false,dashed=false,orderLine=false,jump=false; // 填充/虚线/顺序线/跳转线
};

// 一个绘图场景（图元按创建顺序保存）。
struct PlotScene {
    bool plot3D=false,equalAspect=true;                // 是否三维、是否等比例
    // 图元创建顺序；同类几何的内部顺序保持不变。
    std::vector<PlotPrimitive> primitives;
    std::vector<std::size_t> colorbarCollections;      // 需要显示色条的图元索引
    std::size_t defaultColorIndex=0;                   // 默认色条索引
};

// 颜色/索引回调：根据层内几何生成逐点数值。
using IndexCallback=std::function<std::vector<double>(const LayerGeometry&)>;

// 绘制层几何的选项。
struct PlotOptions {
    bool plotContours=true,plotHatches=true,plotPoints=true; // 绘制哪类几何
    bool plot3D=false,plotArrows=false,plotOrderLine=false,plotColorbar=false; // 显示开关
    std::string index;                                  // 颜色索引字段
    IndexCallback indexCallback;                        // 自定义颜色回调
};

// 顺序扫描绘制的选项。
struct SequentialPlotOptions { bool plotArrows=false,plotOrderLine=false,plotJumps=false; };

// 多边形绘制选项。
struct PolygonPlotOptions {
    double zPos=0.0,lineWidth=0.7;                      // 高度与线宽
    std::string lineColor="k",fillColor="r";            // 线色与填充色
    bool plot3D=false,plotFilled=false;                 // 三维/填充开关
};

// 热图数据。
struct HeatMap {
    std::size_t width=0,height=0;
    std::vector<double> values; // 行优先，图像原点在下方
    double resolution=0.25;     // 像素尺寸
    Vec2 coordinateOffset;      // 从曝光点 XY 中减去的偏移（原地修改输入）
    std::string colormap="hot",interpolation="nearest"; // 色图与插值方式
    double at(std::size_t x,std::size_t y) const { return values.at(y*width+x); }
};

// 绘制单层。
// handle 非空时把图元累积到该场景，并返回场景的值拷贝；
// 不需要 Qt/Matplotlib 运行库。
PYSLM_API PlotScene plot(const Layer& layer,double zPos=0.0,const PlotOptions& options={},PlotScene* handle=nullptr);
// 绘制多层。
PYSLM_API PlotScene plotLayers(const std::vector<Layer>& layers,const PlotOptions& options={},PlotScene* handle=nullptr);
// 按扫描顺序绘制。
PYSLM_API PlotScene plotSequential(const Layer& layer,const SequentialPlotOptions& options={},PlotScene* handle=nullptr);
// 绘制一组路径多边形。
PYSLM_API PlotScene plotPolygon(const Paths& polygons,const PolygonPlotOptions& options={},PlotScene* handle=nullptr);
// 绘制一组带孔多边形。
PYSLM_API PlotScene plotPolygon(const Polygons& polygons,const PolygonPlotOptions& options={},PlotScene* handle=nullptr);
// 生成热图；会按原版行为修改曝光点 XY。
// 负索引按 NumPy 规则回绕一次，越界索引抛异常；能量按输入顺序累加。
PYSLM_API HeatMap plotHeatMap(const Part& part,double z,std::vector<Vec3>& exposurePoints,double resolution=0.25);

} // namespace pyslm::visualise
