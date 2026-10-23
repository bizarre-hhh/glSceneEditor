#pragma once

#include "export.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace pyslm {

// ===========================================================================
// 基础几何类型
// ===========================================================================

// 二维点/向量（双精度）。除 Layer::z 外，公开接口的长度单位均为 mm。
struct Vec2 {
    double x = 0.0, y = 0.0;
    double& operator[](std::size_t i) { return i == 0 ? x : y; }
    double operator[](std::size_t i) const { return i == 0 ? x : y; }
};
// 三维点/向量（双精度）。
struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
    double& operator[](std::size_t i) { return i == 0 ? x : (i == 1 ? y : z); }
    double operator[](std::size_t i) const { return i == 0 ? x : (i == 1 ? y : z); }
};
// 逐分量加减法，以及向量与标量的乘除法。
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x+b.x, a.y+b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x-b.x, a.y-b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x*s, a.y*s}; }
inline Vec2 operator/(Vec2 a, double s) { return {a.x/s, a.y/s}; }
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x*s, a.y*s, a.z*s}; }
inline Vec3 operator/(Vec3 a, double s) { return {a.x/s, a.y/s, a.z/s}; }
// 点积；norm 返回向量长度；cross 返回三维叉积。
inline double dot(Vec2 a, Vec2 b) { return a.x*b.x+a.y*b.y; }
inline double dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline double norm(Vec2 a) { return std::hypot(a.x,a.y); }
inline double norm(Vec3 a) { return std::sqrt(dot(a,a)); }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }

// 一条折线/轮廓；闭合轮廓通常首尾使用同一个点。
using Path = std::vector<Vec2>;
// 多条折线或轮廓（例如外轮廓与内孔）。
using Paths = std::vector<Path>;
// 二维包围盒：{xmin, ymin, xmax, ymax}。
using Bounds2 = std::array<double,4>;
// 三维包围盒：{xmin, ymin, zmin, xmax, ymax, zmax}。
using Bounds3 = std::array<double,6>;
// 4×4 变换矩阵，按行优先存储。
using Matrix4 = std::array<double,16>;
// 一条三维扫描线：{起点, 终点}。
using HatchLine = std::array<Vec3,2>;
// 三维扫描线集合。
using HatchLines = std::vector<HatchLine>;
// 三角面片：三个顶点索引。
using Triangle = std::array<std::uint32_t,3>;

// 三角网格。faces 通过索引引用 vertices。
struct Mesh {
    std::vector<Vec3> vertices;                        // 顶点坐标
    std::vector<Triangle> faces;                       // 三角形顶点索引
    std::map<std::string,std::vector<double>> faceAttributes; // 按名称保存的逐面属性
};

// ===========================================================================
// 枚举
// ===========================================================================

// 激光工作模式：连续（CW）或脉冲（Pulse）。
enum class LaserMode : int { CW=0, Pulse=1 };
// 层内几何类型：无效、轮廓多边形、扫描填充、点。
enum class LayerGeometryType : int { Invalid=0, Polygon=1, Hatch=2, Pnts=3 };
// 取层内几何的顺序：默认、轮廓优先、填充优先。
enum class ScanMode : int { Default=0, ContourFirst=1, HatchFirst=2 };

// ===========================================================================
// 构建数据
// ===========================================================================

// 构建文件头部信息。
struct Header {
    std::string filename;          // 源文件名
    std::array<int,2> version{0,0}; // 文件格式版本 {major, minor}
    int zUnit=1000;                // z 坐标的单位换算系数
};

// 一套激光加工参数（对应原版 BuildStyle）。
struct BuildStyle {
    std::string name, description;                           // 名称与说明
    int bid=0;                                               // 参数集 ID
    double laserPower=0.0, laserSpeed=0.0, laserFocus=0.0;   // 功率、速度、焦距
    int laserId=1;                                           // 激光器编号
    LaserMode laserMode=LaserMode::Pulse;                    // 连续/脉冲
    int pointDistance=0, pointExposureTime=0, pointDelay=0, jumpDelay=0; // 点参数与延时
    double jumpSpeed=0.0;                                    // 跳转速度
    // 可选：记录 jumpSpeed 在原版动态语言中的类型，供严格基准验证使用。
    // 不设置表示按普通 C++ 数值处理；false 表示保留 Python 浮点语义。
    std::optional<bool> jumpSpeedSourceIsInteger;
    // 一次性设置常用参数（ID、焦距、功率、曝光、速度、激光器和模式）。
    PYSLM_API void setStyle(int id, int focus, double power, int exposureTime,
        int exposureDistance, double speed=0.0, int laser=1,
        LaserMode mode=LaserMode::Pulse, const std::string& styleName={},
        const std::string& styleDescription={});
};

// 构建中的一个模型（零件），可包含多套加工参数。
struct Model {
    explicit Model(int modelId=0):mid(modelId){}
    int mid=0, topLayerId=0;                          // 模型 ID、最高层 ID
    std::string name, buildStyleDescription, buildStyleName; // 名称与默认参数说明
    std::vector<BuildStyle> buildStyles;              // 该模型的参数集合
};

// 层内的一段几何数据。以值语义保存，kind 决定其含义。
struct LayerGeometry {
    LayerGeometryType kind=LayerGeometryType::Invalid; // 几何类型
    int mid=0, bid=0;                                 // 所属模型 ID、参数集 ID
    std::string subType;                              // 子类型标记
    Path coords;                                      // 坐标序列（含义随 kind 变化）
    LayerGeometryType type() const noexcept { return kind; }
    // 计算该几何的二维包围盒。
    PYSLM_API Bounds2 boundingBox() const;
    // 扫描线数量：coords 按起点/终点成对存储，因此数量是点数的一半。
    double numHatches() const noexcept { return static_cast<double>(coords.size())/2.0; }
    // 轮廓点数：闭合折线首尾点重合，因此减去 1。
    std::ptrdiff_t numContours() const noexcept { return static_cast<std::ptrdiff_t>(coords.size())-1; }
    std::size_t numPoints() const noexcept { return coords.size(); }
};
// 填充几何（kind = Hatch）。
struct HatchGeometry : LayerGeometry {
    HatchGeometry() { kind=LayerGeometryType::Hatch; }
    HatchGeometry(int model, int style, Path points={}) { kind=LayerGeometryType::Hatch; mid=model; bid=style; coords=std::move(points); }
};
// 轮廓几何（kind = Polygon）。
struct ContourGeometry : LayerGeometry {
    ContourGeometry() { kind=LayerGeometryType::Polygon; }
    ContourGeometry(int model, int style, Path points={}) { kind=LayerGeometryType::Polygon; mid=model; bid=style; coords=std::move(points); }
};
// 点几何（kind = Pnts）。
struct PointsGeometry : LayerGeometry {
    PointsGeometry() { kind=LayerGeometryType::Pnts; }
    PointsGeometry(int model, int style, Path points={}) { kind=LayerGeometryType::Pnts; mid=model; bid=style; coords=std::move(points); }
};

// 一个打印层。
struct Layer {
    std::int64_t z=0;                  // 层高，原版按整数微米保存
    int id=0;                          // 层 ID
    std::string name;                  // 层名称
    std::uint64_t layerFilePosition=0; // 在源文件中的位置（若适用）
    std::vector<LayerGeometry> geometry; // 层内几何集合
    Layer(std::int64_t height=0,int layerId=0):z(height),id(layerId){}
    bool isLoaded() const noexcept { return true; } // C++ 版本中数据总是已加载
    int layerId() const noexcept { return id; }
    void setLayerId(int value) noexcept { id=value; }
    void appendGeometry(const LayerGeometry& value) { geometry.push_back(value); }
    // 按指定扫描顺序返回几何。
    PYSLM_API std::vector<LayerGeometry> getGeometry(ScanMode mode=ScanMode::Default) const;
    // 只返回轮廓几何。
    PYSLM_API std::vector<LayerGeometry> getContourGeometry() const;
    // 只返回填充几何。
    PYSLM_API std::vector<LayerGeometry> getHatchGeometry() const;
    // 只返回点几何。
    PYSLM_API std::vector<LayerGeometry> getPointsGeometry() const;
};

// 返回库版本字符串。
PYSLM_API const char* version() noexcept;

} // namespace pyslm
