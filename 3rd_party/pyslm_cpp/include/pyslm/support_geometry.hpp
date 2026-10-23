#pragma once
#include "core.hpp"
#include <optional>
#include <string>

// 支撑相关的网格、多边形与三角化工具。
namespace pyslm::support {

using Path3D=std::vector<Vec3>;                       // 三维折线
using Edge=std::array<std::uint32_t,2>;               // 一条边的两个顶点索引
struct Triangulation { Path vertices; std::vector<Triangle> faces; }; // 顶点与三角面
struct StrutIntersection { bool intersects=false; Path3D locations; }; // 支杆与网格的相交结果
// PolygonTree 的值语义表示：多边形、是否孔洞、子节点。
struct PolygonTree { Path polygon; bool isHole=false; std::vector<PolygonTree> children; };

// ---- 网格查询与处理 ----

PYSLM_API std::vector<Vec3> meshFaceNormals(const Mesh& mesh);        // 逐面法向量
PYSLM_API std::vector<Vec3> meshVertexNormals(const Mesh& mesh);      // 逐顶点法向量
PYSLM_API std::vector<double> meshFaceAreas(const Mesh& mesh);        // 逐面面积
PYSLM_API double meshVolume(const Mesh& mesh);                        // 网格体积
PYSLM_API Bounds3 meshBounds(const Mesh& mesh);                       // 网格包围盒
// 唯一边；排序规则与原版一致：较大端点在前，较小端点在后。
PYSLM_API std::vector<Edge> meshUniqueEdges(const Mesh& mesh);
PYSLM_API std::vector<std::array<std::size_t,2>> meshFaceAdjacency(const Mesh& mesh); // 相邻面索引对
PYSLM_API std::vector<Edge> meshFaceAdjacencyEdges(const Mesh& mesh); // 相邻面共享边
PYSLM_API Mesh processMesh(const Mesh& mesh,bool validate=false);     // 清理/合并网格
PYSLM_API Mesh selectMeshFaces(const Mesh& mesh,const std::vector<std::size_t>& faces,bool process=true); // 选取面生成子网格
PYSLM_API std::vector<Mesh> splitMeshComponents(const Mesh& mesh);    // 拆分连通分量
PYSLM_API std::vector<Path3D> meshOutline(const Mesh& mesh,int digits=8); // 网格外轮廓

// ---- 多边形三角化与实体构造 ----

// 用 Triangle 库三角化多边形，triangleArgs 为 Triangle 的命令行选项。
PYSLM_API Triangulation triangulateShapelyPolygon(const Polygon& polygon,const std::string& triangleArgs="p");
// 分别给出外轮廓与内孔后三角化。
PYSLM_API Triangulation triangulatePolygonFromPaths(const Path& exterior,const Paths& interiors,const std::string& triangleArgs="p");
// 用 earcut 三角化一组多边形。
PYSLM_API Triangulation triangulatePolygonEarcut(const Polygons& polygons,bool isClosed=false);
// 把平面三角化结果拉伸为实体网格。
PYSLM_API Mesh extrudeTriangulation(const Path& vertices,const std::vector<Triangle>& faces,double height);
// 拉伸网格的面；可给固定高度或逐顶点高度。
PYSLM_API Mesh extrudeFace(const Mesh& mesh,std::optional<double> height=std::nullopt,const std::vector<double>& heightArray={});
// 沿路径扫掠多边形，可选逐点旋转角与缩放系数。
PYSLM_API Mesh sweepPolygon(const Polygon& polygon,const Path3D& path,const std::vector<double>& angles={},const std::vector<double>& scaleFactors={},const std::string& triangleArgs="p");
// 网格布尔运算。
PYSLM_API Mesh boolUnion(const Mesh& a,const Mesh& b);
PYSLM_API Mesh boolIntersect(const Mesh& a,const Mesh& b);
PYSLM_API Mesh boolDiff(const Mesh& a,const Mesh& b);
// 原版函数明确拒绝该操作（Manifold 不支持）。
PYSLM_API Mesh resolveIntersection(const Mesh& mesh);

// ---- 多边形辅助函数 ----

PYSLM_API Polygon generatePolygonBoundingBox(const Bounds3& bounds); // 包围盒的底面多边形
PYSLM_API Polygons flattenSupportRegion(const Mesh& mesh);           // 支撑区域投影为多边形
PYSLM_API Paths path2DToPathList(const Polygons& polygons);          // 多边形转路径列表
PYSLM_API Paths createPath2DfromPaths(const Paths& paths);           // 路径规范化
// 区分外轮廓与内孔，可选择闭合多边形。
PYSLM_API std::pair<Paths,Paths> sortExteriorInteriorRings(const Polygons& polygons,bool closePolygon=false);
PYSLM_API std::pair<Paths,Paths> sortExteriorInteriorRings(const PolygonTree& tree,bool closePolygon=false);

// ---- 几何求交 ----

// 检查半径为 radius 的支杆（a→b）是否与网格相交。
PYSLM_API StrutIntersection checkStrutCylinderIntersection(Vec3 a,Vec3 b,double radius,const Mesh& mesh,int numPoints=6,bool centreOnly=false);
// 射线与网格的最近非负交点；无交点返回空。
PYSLM_API std::optional<Vec3> firstRayIntersection(const Mesh& mesh,Vec3 origin,Vec3 direction);

} // namespace pyslm::support
