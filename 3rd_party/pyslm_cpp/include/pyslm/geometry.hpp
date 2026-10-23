#pragma once

#include "types.hpp"
#include <array>
#include <string>
#include <utility>
#include <vector>

// 层集合、几何变换与构建数据校验工具。
namespace pyslm::geometry {

// 以 layerId 为键的有序层字典。
// 保持插入顺序：与原版 Python 字典的迭代顺序一致。
class PYSLM_API LayerDict {
public:
    using Entry = std::pair<int, Layer>;    // 层 ID 与层数据
    using Container = std::vector<Entry>;
    Container entries;                       // 按插入顺序保存的条目
    Layer& at(int layerId);                  // 按 ID 访问（不存在则抛异常）
    const Layer& at(int layerId) const;
    Layer& operator[](int layerId);          // 按 ID 访问，不存在时插入
    std::size_t size() const noexcept { return entries.size(); }
    bool empty() const noexcept { return entries.empty(); }
    Container::iterator begin() noexcept { return entries.begin(); }
    Container::iterator end() noexcept { return entries.end(); }
    Container::const_iterator begin() const noexcept { return entries.begin(); }
    Container::const_iterator end() const noexcept { return entries.end(); }
};

// 由层列表创建字典。
PYSLM_API LayerDict createLayerDict(const std::vector<Layer>& layerList);
// 合并多个层字典；C++ 值为独立副本，不会修改源数据。
PYSLM_API LayerDict mergeLayers(const std::vector<LayerDict>& layerLists);
// 平移单个几何的坐标。
PYSLM_API void translateLayerGeoms(LayerGeometry& geometry, Vec2 translation);
// 平移多个几何的坐标。
PYSLM_API void translateLayerGeoms(std::vector<LayerGeometry>& geometries, Vec2 translation);
// 用 4 元素参数变换单个几何（原版约定的二维变换）。
PYSLM_API void transformLayerGeoms(LayerGeometry& geometry, const std::array<double,4>& transform);
// 用 9 元素矩阵变换单个几何（原版约定的三维变换）。
PYSLM_API void transformLayerGeoms(LayerGeometry& geometry, const std::array<double,9>& transform);
// 批量版本的二维变换。
PYSLM_API void transformLayerGeoms(std::vector<LayerGeometry>& geometries, const std::array<double,4>& transform);
// 批量版本的三维变换。
PYSLM_API void transformLayerGeoms(std::vector<LayerGeometry>& geometries, const std::array<double,9>& transform);
// 按模型 ID 与参数集 ID 查找加工参数。
PYSLM_API const BuildStyle& getBuildStyleById(const std::vector<Model>& models, int mid, int bid);
PYSLM_API BuildStyle& getBuildStyleById(std::vector<Model>& models, int mid, int bid);
// 按层 ID 查找层。
PYSLM_API const Layer& getLayerById(const std::vector<Layer>& layers, int layerId);
PYSLM_API Layer& getLayerById(std::vector<Layer>& layers, int layerId);
// 按模型 ID 查找模型。
PYSLM_API const Model& getModel(const std::vector<Model>& models, int mid);
PYSLM_API Model& getModel(std::vector<Model>& models, int mid);
// 生成可读的文本描述。
PYSLM_API std::string toString(const BuildStyle& style);
PYSLM_API std::string toString(const LayerGeometry& geometry);
PYSLM_API std::string toString(const Layer& layer);

// 构建数据校验器。
class PYSLM_API ModelValidator {
public:
    // 检查加工参数是否完整、合法。
    static bool validateBuildStyle(const BuildStyle& style);
    // 检查模型是否合法。
    static bool validateModel(const Model& model);
    // 校验整个构建；警告写入 warnings（为空则丢弃），不会使用全局回调。
    static bool validateBuild(const std::vector<Model>& models, const std::vector<Layer>& layers,
                              std::vector<std::string>* warnings = nullptr);
};

} // namespace pyslm::geometry
