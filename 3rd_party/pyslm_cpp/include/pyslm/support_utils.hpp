#pragma once
#include "support_geometry.hpp"

// 支撑生成使用的角度、投影与高度图工具。
namespace pyslm::support {

// 行优先的数值矩阵。
struct NumericMatrix {
    std::size_t rows=0,cols=0;
    std::vector<double> values;
    double& at(std::size_t row,std::size_t col) { return values.at(row*cols+col); }
    double at(std::size_t row,std::size_t col) const { return values.at(row*cols+col); }
};

// 面索引及其相邻面列表。
using FaceAdjacency=std::vector<std::pair<std::size_t,std::vector<std::size_t>>>;
// 投影支撑代价：代价值与对应高度图。
struct ProjectionSupportCost { double cost=0.; NumericMatrix heightMap; };

// 计算每个面的相邻面。
PYSLM_API FaceAdjacency getAdjacentFaces(const Mesh& mesh);
// 计算支撑角；可指定打印方向法向量，并按连通性合并。
PYSLM_API std::vector<double> getSupportAngles(const Part& part,std::optional<Vec3> unitNormal=std::nullopt,bool useConnectivity=true);
// 计算面在 Z 方向的投影权重。
PYSLM_API std::vector<double> getFaceZProjectionWeight(const Mesh& mesh,bool useConnectivity=false);
// 提取悬垂面网格。
PYSLM_API Mesh getOverhangMesh(const Part& part,double overhangAngle,bool useConnectivity=false);
// 提取悬垂面并拆分为多个连通网格。
PYSLM_API std::vector<Mesh> getOverhangMeshes(const Part& part,double overhangAngle,bool useConnectivity=false);
// 近似计算支撑力矩面积。
PYSLM_API double approximateSupportMomentArea(const Part& part,double overhangAngle);
// 近似支撑面积；原版按面返回（名称与浮点标注具有历史含义）。
PYSLM_API std::vector<double> getApproximateSupportArea(const Part& part,double overhangAngle,bool projected=false);
// 按面质心近似支撑映射，保留原版 N×N 广播结果。
PYSLM_API NumericMatrix approximateSupportMapByCentroid(const Part& part,double overhangAngle,bool includeTriangleVertices=false);
// 近似投影支撑代价。
PYSLM_API ProjectionSupportCost approximateProjectionSupportCost(const Part& part,double overhangAngle,bool includeTriangleVertices=false);
// 生成高度图；栅格为 X 优先：行数对应 X，列数对应 Y。
PYSLM_API NumericMatrix generateHeightMap(const Mesh& mesh,Vec3 upVec={0,0,1},double resolution=0.5,const Polygons& offsetPolygons={});
// 高度图的另一种实现/变体。
PYSLM_API NumericMatrix generateHeightMap2(const Mesh& mesh,Vec3 upVec={0,0,1},double resolution=0.5,const Polygons& offsetPolygons={});

} // namespace pyslm::support
