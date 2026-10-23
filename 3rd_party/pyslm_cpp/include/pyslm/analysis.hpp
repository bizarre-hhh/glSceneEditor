#pragma once

#include "types.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <vector>

// 路径长度、加工时间与构建过程遍历分析。
// 单位约定：坐标 mm，速度 mm/s，延时微秒。
namespace pyslm::analysis {

// 单个层内几何内所有跳转的总长度（mm）。
PYSLM_API double getLayerGeometryJumpDistance(const LayerGeometry& layerGeom);
// 单个层内几何的扫描路径总长度（mm）。
PYSLM_API double getLayerGeometryPathLength(const LayerGeometry& layerGeom);
// 单个层内几何的跳转次数。
PYSLM_API std::size_t getLayerGeomTotalJumps(const LayerGeometry& layerGeom);
// 同一层内跨几何的跳转总长度（mm）。
PYSLM_API double getIntraLayerGeometryJumpLength(const Layer& layer);
// 整层的跳转总长度（mm）。
PYSLM_API double getLayerJumpLength(const Layer& layer);
// 整层的扫描路径总长度（mm）。
PYSLM_API double getLayerPathLength(const Layer& layer);
// 根据曝光参数换算等效激光速度（mm/s）。
PYSLM_API double getEffectiveLaserSpeed(const BuildStyle& style);
// 单个层内几何的加工时间；可选择是否计入跳转时间。
PYSLM_API double getLayerGeometryTime(const LayerGeometry& layerGeom,const std::vector<Model>& models,
                                    bool includeJumpTime=false,double jumpSpeed=5000.0,double jumpDelay=0.0);
// 整层加工时间；保留原版已记录的时间计算特性。
// 详见 docs/analysis_deviations.md。
PYSLM_API double getLayerTime(const Layer& layer,const std::vector<Model>& models,
                            bool includeJumpTime=true,double laserJumpSpeed=5000.0);

// 一段扫描线（起点、终点）及其时间分析类型。
using ScanVector=std::array<Vec2,2>;
using ScanVectors=std::vector<ScanVector>;
// 某一时刻的激光状态。
struct LaserState {
    Vec2 position;                 // 激光位置
    const BuildStyle* buildStyle=nullptr; // 当前加工参数
    bool isActive=false;           // 激光是否开启
};

// 构建时间树的节点：记录某层/某几何的累计加工时间。
struct PYSLM_API TimeNode {
    TimeNode* parent=nullptr;      // 父节点
    std::size_t id=0;              // 节点 ID
    const Layer* layer=nullptr;    // 对应层
    const LayerGeometry* geometry=nullptr; // 对应层内几何
    double time=0.0;               // 累计时间
    std::vector<TimeNode> children; // 子节点
    double getChildrenTime() const; // 所有子节点时间之和
};

// 把多层几何展开为连续的扫描线序列。
class PYSLM_API ScanVectorIterator {
public:
    explicit ScanVectorIterator(std::vector<Layer> layers);
    ScanVectors initialise();                                   // 初始化并返回全部扫描线
    static ScanVectors reshapeVectors(const Paths& vectorList); // 扁平路径转扫描线
    static Paths getLayerVectors(const Layer& layer);           // 提取单层扫描线
    bool next(ScanVector& vector);                              // 迭代下一条；结束返回 false
    void reset();                                               // 重置迭代位置
    const ScanVectors& vectors() const noexcept { return vectors_; }
private:
    std::vector<Layer> layers_;   // 层副本
    ScanVectors vectors_;         // 全部扫描线
    ScanVectors layerVectors_;    // 当前层的扫描线
    std::size_t index_=0,layerIndex_=0; // 当前扫描线/层索引
};

// 构建过程迭代器：按时间顺序遍历层与几何。
class PYSLM_API Iterator {
public:
    // 保留原版的“最后一层几何省略”和严格时间边界规则；
    // 使用前请阅读 docs/analysis_deviations.md。
    Iterator(std::vector<Model> models,std::vector<Layer> layers);
    virtual ~Iterator()=default;
    Iterator(const Iterator&)=delete;
    Iterator& operator=(const Iterator&)=delete;
    double time() const noexcept { return time_; }              // 当前时间
    double currentLayerGeometryTime() const noexcept { return layerGeomTime_; } // 当前几何耗时
    const TimeNode& tree() const;                               // 构建时间树
    virtual double dwellTime() const { return recoaterTime_; }  // 停留/铺粉时间
    double recoaterTime() const noexcept { return recoaterTime_; }
    void setRecoaterTime(double seconds);                       // 设置铺粉时间
    const std::vector<Layer>& layers() const noexcept { return layers_; }
    const std::vector<Model>& models() const noexcept { return models_; }
    void setLayers(std::vector<Layer> layers);
    void setModels(std::vector<Model> models);
    double getBuildTime() const;                                // 总构建时间
    double getLayerGeomTime(std::size_t layerId,std::size_t layerGeomId) const; // 指定几何耗时
    double getLayerTime(std::size_t layerId) const;             // 指定层耗时
    double getTimeByLayerGeometryId(std::size_t layerId,std::size_t layerGeomId) const; // 几何起始时间
    const TimeNode* getLayerGeometryNodeByTime(double time) const; // 按时间查节点
    const LayerGeometry* getLayerGeometryByTime(double time) const; // 按时间查几何
    std::optional<std::size_t> getLayerGeometryIdByTime(double time) const; // 按时间查几何 ID
    const TimeNode* getLayerNodeByTime(double time) const;      // 按时间查层节点
    const Layer* getLayerByTime(double time) const;             // 按时间查层
    std::optional<std::size_t> getLayerIdByTime(double time) const; // 按时间查层 ID
    double getTimeByLayerId(std::size_t layerId) const;         // 层的起始时间
    const LayerGeometry& getCurrentLayerGeometry() const;       // 当前几何
    const Layer& getCurrentLayer() const;                       // 当前层
    void seekByLayerGeometry(std::size_t layerId,std::size_t layerGeomId); // 定位到几何
    void seekByLayer(std::size_t layerId);                      // 定位到层
    virtual void seek(double time);                             // 定位到时间点
    virtual void reset();                                       // 回到起点
    virtual bool next(const LayerGeometry*& geometry);          // 前进到下一几何
protected:
    void generateCache() const;                                 // 生成时间缓存
    std::vector<Model> models_;                                 // 模型
    std::vector<Layer> layers_;                                 // 层
    double time_=0.0,layerGeomTime_=0.0,recoaterTime_=0.0;      // 当前时间、几何耗时、铺粉时间
    std::size_t layerIndex_=0,geometryIndex_=0;                 // 当前层/几何索引
    mutable bool cacheValid_=false;                             // 缓存是否有效
    mutable TimeNode tree_;                                     // 时间树根节点
};

// 按层内几何遍历的迭代器（使用基类构造与接口）。
class PYSLM_API LayerGeometryIterator : public Iterator {
public:
    using Iterator::Iterator;
};

// 激光扫描采样点。
struct ScanSample { Vec2 position; double time=0.0; bool laserOn=false; };

// 扫描迭代器：按固定时间步长采样激光位置与状态。
class PYSLM_API ScanIterator : public Iterator {
public:
    using Iterator::Iterator;
    double timestep=1e-3;                                       // 采样时间步长（秒）
    bool isLaserOn() const;                                     // 当前激光是否开启
    Vec2 getPointInLayerGeometry(double timeOffset,const LayerGeometry& geometry) const; // 几何内指定时刻的位置
    Vec2 getCurrentLaserPosition() const;                       // 当前激光位置
    const BuildStyle& getCurrentBuildStyle() const;             // 当前加工参数
    bool next(ScanSample& sample);                              // 前进到下一个采样点
};

} // namespace pyslm::analysis
