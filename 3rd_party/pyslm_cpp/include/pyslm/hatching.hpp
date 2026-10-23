#pragma once

#include "types.hpp"
#include <memory>
#include <optional>
#include <utility>

// 填充（hatching）与扫描线排序接口。
// 长度单位为 mm，角度参数单位为度（特殊注明者除外）。
namespace pyslm::hatching {

// 带孔多边形：一个外轮廓加若干内孔。
struct ClosedPolygon {
    Path exterior;    // 外轮廓
    Paths interiors;  // 内孔轮廓
};

// 把一组闭合路径按包含关系整理成多边形（区分外轮廓与孔）。
PYSLM_API std::vector<ClosedPolygon> pathsToClosedPolygons(const Paths& paths);
// 取出多边形的全部轮廓（外轮廓在前，内孔在后）。
PYSLM_API Paths getContoursFromPolygon(const ClosedPolygon& polygon);
// 分别返回外轮廓集合与内孔集合。
PYSLM_API std::pair<Paths,Paths> getContourRingsFromPolygon(const ClosedPolygon& polygon);
// 单个多边形转路径列表。
PYSLM_API Paths poly2Paths(const ClosedPolygon& polygon);
// 原版 MultiPolygon 分支只返回外轮廓，此重载保持一致行为。
PYSLM_API Paths poly2Paths(const std::vector<ClosedPolygon>& polygons);
// 按容差简化多边形轮廓。
PYSLM_API std::vector<ClosedPolygon> simplifyBoundaries(const std::vector<ClosedPolygon>& polygons,
                                                      double tolerance=0.5);

// 扁平扫描线数组按“起点、终点、起点、终点……”连续存放。
// 判断数组长度是否为偶数且结构有效。
PYSLM_API bool isValidHatchArray(const Path& vectors) noexcept;
// 二维扁平扫描线数组转三维扫描线（z = 0）。
PYSLM_API HatchLines to3DHatchArray(const Path& vectors);
// 三维扫描线转回二维扁平数组。
PYSLM_API Path from3DHatchArray(const HatchLines& vectors);
// 路径转 Clipper 所需的 Vec3 形式。
PYSLM_API std::vector<std::vector<Vec3>> paths2clipper(const Paths& paths);
// Clipper 输出转路径；scaleFactor 为定点缩放系数。
PYSLM_API Paths clipper2Paths(const std::vector<std::vector<Vec3>>& paths,
                             double scaleFactor, bool close=false);
// 路径版本的轮廓简化。
PYSLM_API Paths simplifyBoundaries(const Paths& paths, double tolerance=0.5);
// NumPy 兼容的 argsort，供自定义原生扫描配方使用。
PYSLM_API std::vector<std::size_t> numpyArgsort(const std::vector<double>& values);
// 判断岛边界与区域的关系，每个结果对为 (是否相交, 是否重叠)。
PYSLM_API std::vector<std::pair<bool,bool>> classifyIslandBoundaries(const Paths& region,
                                                                 const Paths& islandBoundaries);

// ===========================================================================
// 扫描线排序器
// ===========================================================================

// 排序器接口：输入扁平扫描线数组，返回重排后的数组。
class PYSLM_API BaseSort {
public:
    virtual ~BaseSort() = default;
    virtual Path sort(const Path& vectors) const = 0;
};
// 不改变扫描线方向。
class PYSLM_API UnidirectionalSort : public BaseSort {
public: Path sort(const Path& vectors) const override;
};
// 逐条翻转扫描线方向（每条线都交换起点和终点）。
class PYSLM_API FlipSort : public BaseSort {
public: Path sort(const Path& vectors) const override;
};
// 隔一条翻转一次扫描线方向。
class PYSLM_API AlternateSort : public BaseSort {
public: Path sort(const Path& vectors) const override;
};
// 先按扫描方向统一朝向，再隔条翻转。
class PYSLM_API HatchDirectionalSort : public BaseSort {
public: Path sort(const Path& vectors) const override;
};
// 依次应用多个排序器。
class PYSLM_API ChainSort : public BaseSort {
public:
    std::vector<std::shared_ptr<BaseSort>> sorters;  // 排序器链
    explicit ChainSort(std::vector<std::shared_ptr<BaseSort>> chain={});
    Path sort(const Path& vectors) const override;
};
// 按扫描线中点在指定方向上的投影排序。
class PYSLM_API LinearSort : public BaseSort {
public:
    double hatchAngle=0.0;  // 排序方向（度）
    explicit LinearSort(double angle=0.0):hatchAngle(angle){}
    Path sort(const Path& vectors) const override;
};
// 贪心聚类排序：先按容差聚类，再按指定方向排序，以减少跳转。
class PYSLM_API GreedySort : public BaseSort {
public:
    double hatchAngle=0.0, hatchTol=0.5, clusterDistance=5.0;  // 排序角度、聚类容差、聚类距离
    bool sortY=false;                                          // 是否按 Y 方向排序
    explicit GreedySort(double angle=0.0, double tolerance=0.5)
        :hatchAngle(angle),hatchTol(tolerance==0.0?0.5:tolerance){}
    Path sort(const Path& vectors) const override;
};

// ===========================================================================
// 填充器
// ===========================================================================

// 填充器基类，提供偏移、包围盒、裁剪等公共工具。
class PYSLM_API BaseHatcher {
public:
    static constexpr std::int64_t CLIPPER_SCALEFACTOR=100000;  // Clipper 定点缩放系数
    virtual ~BaseHatcher() = default;
    // 定点运算可达到的坐标误差。
    static constexpr double error() noexcept { return 1.0/CLIPPER_SCALEFACTOR; }
    // 对边界做等距偏移（正值向外/负值向内由原版约定决定）。
    static Paths offsetBoundary(const Paths& paths, double offset);
    // 对多个多边形分别做等距偏移。
    static std::vector<Paths> offsetPolygons(const std::vector<Paths>& polygons, double offset);
    // 计算多边形或路径的包围盒。
    static Bounds2 polygonBoundingBox(const Paths& paths);
    static Bounds2 polygonBoundingBox(const Path& path);
    // 计算边界集合的包围盒。
    static Bounds2 boundaryBoundingBox(const Paths& paths);
    // 用边界裁剪扫描线。
    static HatchLines clipLines(const Paths& paths, const HatchLines& lines);
    // 用边界裁剪多组轮廓线。
    static std::vector<std::vector<Vec3>> clipContourLines(const Paths& paths,
                                            const std::vector<std::vector<Vec3>>& contours);
    // 在边界内按间距和角度生成扫描线。
    virtual HatchLines generateHatching(const Paths& paths, double spacing,
                                       double angle=90.0) const;
    // 对边界执行完整填充，返回一层几何；失败时返回空。
    virtual std::optional<Layer> hatch(const Paths& boundary) const = 0;
};

// 岛内部填充区域：负责局部坐标与方向的变换。
class PYSLM_API InnerHatchRegion {
public:
    Vec2 origin{};                       // 区域原点
    // 旋转角，单位为弧度（与原版 InnerHatchRegion 一致）。
    double orientation=0.0;
    virtual ~InnerHatchRegion() = default;
    Path transformCoordinates2D(const Path& coords) const;        // 二维坐标变换
    HatchLines transformCoordinates(const HatchLines& coords) const; // 扫描线坐标变换
    std::array<double,4> rotationMatrix2D() const;                // 二维旋转矩阵
    std::array<double,9> rotationMatrix3D() const;                // 三维旋转矩阵
    void setIntersecting(bool state) noexcept { intersecting_=state; }
    void setRequiresClipping(bool state) noexcept { requiresClipping_=state; }
    bool isIntersecting() const noexcept { return intersecting_; }
    bool requiresClipping() const noexcept { return requiresClipping_; }
    virtual Path boundary() const = 0;                            // 区域边界
    virtual HatchLines hatch() const = 0;                         // 区域内部扫描线
private:
    bool intersecting_=false, requiresClipping_=false;            // 相交/裁剪状态
};

// 标准填充器：轮廓 + 扫描线填充。
class PYSLM_API Hatcher : public BaseHatcher {
public:
    bool scanContourFirst=false, hatchingEnabled=true;            // 轮廓优先、是否启用填充
    int numInnerContours=1, numOuterContours=1;                   // 内/外轮廓道数
    double spotCompensation=0.08, contourOffset=0.08, volumeOffsetHatch=0.08; // 光斑补偿与轮廓偏移
    double layerAngleIncrement=0.0, hatchDistance=0.08, hatchAngle=45.0;      // 层间角度增量、填充间距、填充角度
    std::shared_ptr<BaseSort> hatchSortMethod;                    // 扫描线排序器
    std::optional<Layer> hatch(const Paths& boundary) const override;
};

// 条纹填充器：把区域划分为条纹后分别填充。
class PYSLM_API StripeHatcher : public Hatcher {
public:
    double stripeWidth=5.0, stripeOverlap=0.1, stripeOffset=0.5;  // 条纹宽度、重叠、偏移
    HatchLines generateHatching(const Paths& paths,double spacing,double angle=90.0) const override;
};

// 基础岛式填充器：把区域划分为岛后分别填充。
class PYSLM_API BasicIslandHatcher : public Hatcher {
public:
    double islandWidth=5.0, islandOverlap=0.1, islandOffset=0.5;  // 岛宽度、重叠、偏移
    HatchLines generateHatching(const Paths& paths,double spacing,double angle=90.0) const override;
};

// 单个岛区域。
class PYSLM_API Island : public InnerHatchRegion {
public:
    std::array<int,2> posId{0,0};        // 岛在网格中的位置索引
    int id=0;                            // 岛 ID
    double islandWidth=0.0, islandOverlap=0.0, hatchDistance=0.1; // 尺寸与间距
    Island(Vec2 position={},double radians=0.0,double width=0.0,double overlap=0.0,double spacing=0.1);
    Path localBoundary() const;          // 局部坐标下的边界
    Path boundary() const override;      // 变换后的边界
    HatchLines generateInternalHatch(bool isOdd=true) const; // 内部扫描线，isOdd 控制奇偶段方向
    HatchLines hatch() const override;
    // 原版跨实例缓存单个边界；在不相关的填充流程之间需要显式重置。
    static void resetBoundaryCache();
};

// 岛式填充器：生成、裁剪岛并执行填充。
class PYSLM_API IslandHatcher : public Hatcher {
public:
    double islandWidth=5.0, islandOverlap=0.1, islandOffset=0.5; // 岛划分参数
    // 根据边界和角度生成岛。
    virtual std::vector<Island> generateIslands(const Paths& paths,double angle=90.0) const;
    // 判断岛与边界的相交关系，返回两组索引。
    std::pair<std::vector<std::size_t>,std::vector<std::size_t>> intersectIslands(
                                                    const Paths& paths,std::vector<Island>& islands) const;
    // 用主体区域裁剪岛。
    Paths clipIslands(const Paths& paths,const Paths& subjects) const;
    std::optional<Layer> hatch(const Paths& boundary) const override;
};

// 把一层几何转换为曝光点：XY 为位置，z 为沉积能量。
// includePowerDeposited=false 时 z 恒为 0。
PYSLM_API std::vector<Vec3> getExposurePoints(const Layer& layer,const std::vector<Model>& models,
                                           bool includePowerDeposited=true);

} // namespace pyslm::hatching
