#pragma once

#include "core.hpp"
#include <optional>
#include <utility>

// 支撑结构生成：块状支撑与网格支撑。
namespace pyslm::support {

// 支撑结构基类：保存被支撑零件及支撑体的网格。
class PYSLM_API SupportStructure {
public:
    // 非拥有指针：使用支撑期间由调用方保证 Part 存活。
    const Part* supportObject=nullptr;
    std::optional<Mesh> supportVolume,supportSurface; // 支撑体积/表面网格
    bool intersectsPart=false;                        // 是否与零件相交
    SupportStructure(const Part* object=nullptr,std::optional<Mesh> volume=std::nullopt,
                     std::optional<Mesh> surface=std::nullopt,bool intersects=false);
    virtual ~SupportStructure()=default;
    virtual Mesh geometry() const=0;                  // 生成支撑网格
    virtual double volume() const=0;                  // 支撑体积
    static Polygon flattenSupportRegion(const Mesh& region); // 支撑区域投影为多边形
    double projectedSupportArea() const;              // 水平投影面积
    double supportArea() const;                       // 支撑表面积
};

// 块状支撑基类。
class PYSLM_API BlockSupportBase : public SupportStructure {
public:
    using SupportStructure::SupportStructure;
    Mesh geometry() const override;                   // 支撑网格
    double volume() const override;                   // 支撑体积
    Mesh supportBoundary() const;                     // 支撑边界网格
    // 把多个平面剖面三角化成一个网格。
    static Mesh triangulateSections(const std::vector<std::optional<PlanarSection>>& sections);
};

// 支撑生成器基类：查找悬垂点/边。
class PYSLM_API BaseSupportGenerator {
public:
    static constexpr double PYCLIPPER_SCALEFACTOR=1e4;    // Clipper 定点缩放系数
    static constexpr double POINT_OVERHANG_TOLERANCE=0.05; // 悬垂点判定容差
    virtual ~BaseSupportGenerator()=default;
    // 返回零件上的悬垂点索引。
    static std::vector<std::size_t> findOverhangPoints(const Part& part);
    // 返回悬垂边（顶点索引对）；分别控制面悬垂角与边悬垂角。
    static std::vector<std::array<std::uint32_t,2>> findOverhangEdges(
                           const Part& part,double overhangAngle=45.0,double edgeOverhangAngle=10.0);
};

// 块状支撑生成器：识别支撑区域并生成块状支撑。
class PYSLM_API BlockSupportGenerator : public BaseSupportGenerator {
public:
    static constexpr double supportSkinSideTolerance=1.0-1e-3; // 支撑表皮侧向容差
    static constexpr double intersectionVolumeTolerance=50.0;  // 相交体积容差
    static constexpr double gaussianBlurSigma=1.0;             // 高斯模糊 sigma
    double minimumAreaThreshold=5.0,rayProjectionResolution=0.2; // 最小面积阈值、射线投影分辨率
    double lowerProjectionOffset=0.05,upperProjectionOffset=0.05; // 上/下投影偏移
    double innerSupportEdgeGap=0.2,outerSupportEdgeGap=0.5;       // 内/外支撑边缘间隙
    double triangulationSpacing=2.0,simplifyPolygonFactor=0.5,overhangAngle=45.0; // 三角化间距、简化系数、悬垂角
    bool useApproxBasePlateSupport=false;                          // 是否使用近似基板支撑
    std::optional<double> splineSimplificationFactor=20.0;         // 样条简化系数
    // 根据投影距离与悬垂角计算高度图梯度阈值。
    static double gradThreshold(double projectionDistance,double overhangAngle);
    // 过滤支撑区域（面积、边缘间隙等）。
    void filterSupportRegion(const Mesh& region) const;
    // 生成支撑与零件相交处的高度图。
    void generateIntersectionHeightMap() const;
    // 识别需要支撑的区域，返回块状支撑列表。
    std::vector<BlockSupportBase> identifySupportRegions(const Part& part,double angle,
                                                       bool findSelfIntersectingSupport=true) const;
};

// 网格支撑的切片类型：边界、X 方向切片、Y 方向切片。
enum class GridMeshType : int { BOUNDARY=0,SLICE_X=1,SLICE_Y=2 };

// 网格支撑的公共参数。
struct GridSupportParameters {
    std::array<double,2> gridSpacing{3.0,3.0};                 // 网格间距
    bool useSupportBorder=true,useSupportSkin=true,generateTrussGrid=true,mergeMesh=false; // 边界/表皮/桁架/合并开关
    bool useUpperSupportTeeth=true,useLowerSupportTeeth=false; // 上/下齿开关
    int numSkinMeshSubdivideIterations=2;                      // 表皮细分次数
    double supportWallThickness=0.5,supportBorderDistance=3.0,trussWidth=1.0,trussAngle=45.0; // 壁厚、边界距离、桁架宽度/角度
    double supportTeethHeight=1.5,supportTeethTopLength=0.1,supportTeethBottomLength=1.5; // 齿高度与上下齿长
    double supportTeethBaseInterval=0.2,supportTeethUpperPenetration=0.2; // 齿间距与上齿侵入量
};

// 网格块状支撑。
class PYSLM_API GridBlockSupport : public BlockSupportBase,public GridSupportParameters {
public:
    using BlockSupportBase::BlockSupportBase;
    static constexpr double pairTolerance=0.1;                 // 点/边配对容差
    static Polygon holeGeometry();                             // 网格孔洞几何
    static Paths clipLines(const Paths& boundaries,const HatchLines& lines); // 用边界裁剪扫描线
    static HatchLines generateMeshGrid(const Polygon& polygon,double spacing=5.0,double angle=45.0); // 生成网格线
    void setSupportWallThickness(double value);                // 设置支撑壁厚
    Mesh geometry() const override;                            // 支撑网格
    std::vector<Mesh> borderGeometry() const;                  // 边界网格
    std::pair<Mesh,Mesh> sliceGeometry() const;                // X/Y 方向切片网格
    Polygon generateSliceBoundingBoxPolygon(const PlanarSection& section) const; // 剖面的包围盒多边形
    Path toothProfile() const;                                 // 齿廓
    std::optional<Polygons> generateSliceGeometry(const PlanarSection& section) const; // 剖面切片几何
    Polygons generateSliceGeometryDepr(const Polygons& sections) const; // 已弃用的切片实现
    Paths generateSupportSkinInfill(const Path& polygon,bool returnPolyNodes=false) const; // 表皮填充
    std::vector<Mesh> generateSupportSkins(std::map<std::string,Paths>* diagnostics=nullptr) const; // 表皮网格
    std::pair<Mesh,Mesh> generateSupportSlices() const;        // 支撑切片
    static Mesh concatenateMeshes(const std::vector<Mesh>& meshes); // 合并多个网格
    // 用同一平面法向在多个高度上剖分网格。
    static std::vector<std::optional<PlanarSection>> sectionMultiplane(
                    const Mesh& volume,Vec3 origin,Vec3 normal,const std::vector<double>& heights);
    // 生成 X/Y 两个方向的网格切片。
    std::pair<std::vector<std::optional<PlanarSection>>,std::vector<std::optional<PlanarSection>>> generateGridSlices() const;
    // 在高度 z 处切片，返回支撑轮廓与内部路径。
    static std::pair<Paths,Paths> slice(const std::vector<Mesh>& supports,double z);
};

// 网格支撑生成器。
class PYSLM_API GridBlockSupportGenerator : public BlockSupportGenerator,public GridSupportParameters {
public:
    GridBlockSupportGenerator();
    // 识别支撑区域，返回网格块状支撑。
    std::vector<GridBlockSupport> identifySupportRegions(const Part& part,double angle,
                                                       bool findSelfIntersectingSupport=true) const;
};

} // namespace pyslm::support
