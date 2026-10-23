#pragma once

#include "types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace pyslm {

// ===========================================================================
// 二维几何与切片辅助结构
// ===========================================================================

// 带孔多边形：一个外轮廓加若干内孔轮廓。
struct Polygon { Path exterior; Paths interiors; };
using Polygons=std::vector<Polygon>;

// 二维几何类型，取值与 Shapely 的几何类型对应。
enum class Geometry2DType : int { Point=0,LineString=1,LinearRing=2,Polygon=3,MultiPoint=4,MultiLineString=5,MultiPolygon=6,GeometryCollection=7 };

// 通用二维几何容器：按 type 决定 coords、polygon、parts 中哪一部分有效。
struct Geometry2D {
    Geometry2DType type=Geometry2DType::GeometryCollection;
    Path coords; // Point / LineString / LinearRing 的坐标
    Polygon polygon; // Polygon 类型的外轮廓与内孔
    std::vector<Geometry2D> parts; // 多几何或几何集合的子项
};

// 切片参数。
struct SliceOptions {
    bool fixPolygons=true;                                    // 是否修复无效多边形
    std::optional<double> simplificationFactor;               // 可选的轮廓简化量
    bool simplificationPreserveTopology=true;                 // 简化时是否保持拓扑
    std::string simplificationFactorMode="absolute";          // 简化量的解释方式，默认绝对长度
};

// 单层位图（通常用于曝光/支撑布尔掩码）。
struct Bitmap {
    std::size_t width=0,height=0;
    std::vector<std::uint8_t> pixels; // 行优先；y 与源坐标同向；取值为 0 或 1
    std::uint8_t at(std::size_t x,std::size_t y) const { return pixels.at(y*width+x); }
};

// 网格与平面求交后得到的平面剖面。
struct PlanarSection {
    Path vertices;                                         // 投影到平面上的二维顶点
    std::vector<Vec3> vertices3D;                          // 投影前选出的三维代表点
    std::vector<std::array<std::uint32_t,2>> edges;        // 剖面上的边（顶点索引对）
    std::vector<std::vector<std::uint32_t>> entities;      // 按原始实体分组的顶点索引
    Paths paths;                                           // 闭合、逆时针的离散路径（保持实体顺序）
    std::vector<std::array<Vec3,2>> rawLines;              // 未连接的原始交线段
    std::vector<std::size_t> faceIndex;                    // 每条线段来源的面索引
    bool isClosed=false;                                   // 剖面是否闭合
    bool gapRepairApplied=false;                           // 是否做过缺口修补
    double z=0.0;                                          // 剖切平面的 z 坐标
    Matrix4 to3D{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};         // 从剖面坐标回到三维的变换
};

// ===========================================================================
// 文档对象模型
// ===========================================================================

// 文档对象基类：Part 等对象的公共接口。
class PYSLM_API DocumentObject {
public:
    explicit DocumentObject(std::string name);
    virtual ~DocumentObject()=default;
    std::string name,label="Document Object";                  // 名称、显示标签
    std::vector<std::shared_ptr<DocumentObject>> attributes;    // 附加对象
    void setName(const std::string& value) { name=value; }
    virtual Bounds3 boundingBox() const=0;                     // 三维包围盒
    virtual Vec3 extents() const=0;                            // 包围盒尺寸
    virtual bool usesBoundaryLayers() const { return false; }  // 是否使用边界层
    virtual void setRequiresBoundaryLayers() {}
};

// 零件：持有网格，并提供切片、投影、变换等操作。
class PYSLM_API Part : public DocumentObject {
public:
    static constexpr double POLYGON_FIX_EPSILON=1e-3;          // 多边形修复容差
    explicit Part(std::string name);
    Vec3 origin{0,0,0},rotation{0,0,0},scaleFactor{1,1,1};     // 平移、旋转、缩放
    std::string partType="Part";                               // 零件类型标记
    std::string toString() const;                              // 生成可读描述
    bool isDirty() const noexcept;                             // 变换缓存是否失效
    // 从 STL 文件加载网格；可选择修复几何、合并顶点。
    void setGeometry(const std::string& filename,bool fixGeometry=true,bool mergeVertices=true);
    // 直接设置内存中的网格。
    void setGeometry(const Mesh& mesh,bool fixGeometry=true,bool mergeVertices=true);
    // 直接设置网格，不做修复。
    void setGeometryByMesh(const Mesh& mesh);
    bool hasGeometry() const noexcept { return hasGeometry_; } // 是否已设置几何
    const Mesh& geometry() const;                              // 获取变换后的网格
    void regenerate() const;                                   // 重新计算变换缓存
    Matrix4 getTransform() const;                              // 获取当前 4×4 变换矩阵
    Bounds3 boundingBox() const override;                      // 变换后的包围盒
    Vec3 extents() const override;                             // 变换后的尺寸
    double volume() const;                                     // 网格体积
    double surfaceArea() const;                                // 网格表面积
    bool checkGeometry() const;                                // 检查网格有效性
    void dropToPlatform(double zPos=0.0);                      // 平移到指定平台高度
    Path getProjectedHull() const;                             // XY 投影的凸包轮廓
    Polygon getProjectedHullPolygon() const;                   // XY 投影的凸包多边形
    Polygons getProjectedArea() const;                         // XY 投影轮廓
    Geometry2D getProjectedGeometry() const;                   // XY 投影的通用几何
    std::optional<PlanarSection> getTrimeshSlice(double z) const; // 平面剖分
    Paths getVectorSlice(double z,const SliceOptions& options={}) const; // 矢量切片轮廓
    Polygons getVectorSlicePolygons(double z,const SliceOptions& options={}) const; // 带孔多边形切片
    Bitmap getBitmapSlice(double z,double resolution,std::optional<Vec2> origin=std::nullopt) const; // 位图切片
    static Paths path2DToPathList(const Polygons& shapes);     // 多边形列表转路径列表
private:
    Mesh source_;                                  // 原始网格
    bool hasGeometry_=false;                       // 是否已设置几何
    mutable Mesh transformed_;                     // 变换后的缓存网格
    mutable bool dirty_=true;                      // 缓存是否需要重算
    mutable Vec3 cachedOrigin_{},cachedRotation_{},cachedScale_{1,1,1}; // 上次变换参数
};

// 文档：管理零件及其依赖关系，并汇总包围盒。
class PYSLM_API Document {
public:
    void addObject(std::shared_ptr<DocumentObject> object);     // 添加对象
    void recalculateDocument();                                 // 重新计算依赖与缓存
    std::vector<std::shared_ptr<DocumentObject>> getDependencyList() const; // 获取依赖列表
    std::shared_ptr<DocumentObject> head() const;               // 获取根对象
    std::vector<std::shared_ptr<Part>> parts() const;           // 获取所有零件
    Bounds3 boundingBox() const;                                // 整个文档的包围盒
    Bounds3 partBoundingBox() const;                            // 所有零件的包围盒
    Vec3 extents() const;                                       // 整个文档的尺寸
    Vec3 partExtents() const;                                   // 所有零件的尺寸
    // 导出可供 GUI 使用的依赖图描述（不依赖绘图库）。
    std::string drawNetworkGraph() const;
    // 按类型筛选对象（只匹配精确类型）。
    template<class T> std::vector<std::shared_ptr<T>> getObjectsByType() const {
        static_assert(std::is_base_of_v<DocumentObject,T>);
        std::vector<std::shared_ptr<T>> result;
        for(const auto& object:objects_) if(typeid(*object)==typeid(T)) result.push_back(std::static_pointer_cast<T>(object));
        return result;
    }
private:
    std::vector<std::shared_ptr<DocumentObject>> objects_;      // 文档对象
    std::vector<std::array<std::size_t,2>> dependencies_;       // 对象依赖关系
};

// ===========================================================================
// 网格与平面工具函数
// ===========================================================================

// 读取 ASCII 或二进制 STL。
PYSLM_API Mesh loadSTL(const std::string& filename);
// 按指定小数位量化并合并重复顶点。
PYSLM_API void mergeMeshVertices(Mesh& mesh,int digits=8);
// 构造与给定原点和法向量对应的平面变换矩阵。
PYSLM_API Matrix4 planeTransform(Vec3 origin,Vec3 normal);
// 计算网格与平面的交线段，并返回每条线段对应的面索引。
PYSLM_API std::pair<std::vector<std::array<Vec3,2>>,std::vector<std::size_t>>
    meshPlane(const Mesh& mesh,Vec3 origin,Vec3 normal);
// 对网格做平面剖分，结果可能为空。
PYSLM_API std::optional<PlanarSection> sectionMesh(const Mesh& mesh,Vec3 origin,Vec3 normal);
// 把二维线段集合连接成平面剖面路径。
PYSLM_API PlanarSection pathFromLines2D(const std::vector<std::array<Vec2,2>>& lines,int digits=5);

} // namespace pyslm
