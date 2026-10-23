#pragma once
#include "types.hpp"
#include <optional>

// 样条曲线拟合与离散化。
namespace pyslm {

// 一条 B 样条曲线。
struct SplineCurve {
    Path control;              // 控制点
    std::vector<double> knots; // 节点向量
    int degree=3;              // 曲线阶数
    bool closed=false;         // 是否闭合
    int fitStatus=0;           // 拟合状态（0 表示成功）
};

// 用平滑 B 样条拟合输入点；closed 输入会把两端控制点取均值。
// smooth 为平滑系数。
PYSLM_API SplineCurve fitSpline(const Path& points,double smooth=0.002);
// 把样条离散为折线；scale 控制采样密度，count 可指定采样点数。
PYSLM_API Path discretizeSpline(const SplineCurve& curve,double scale=1.0,
                                std::optional<std::size_t> count=std::nullopt);
// 尝试用圆拟合输入点；不满足条件时返回空。
PYSLM_API std::optional<Path> fitClosedCircle(const Path& points,double scale);
// 离散化闭合圆参数曲线。
PYSLM_API Path discretizeClosedCircle(const Path& controls,double scale=1.0);

} // namespace pyslm
