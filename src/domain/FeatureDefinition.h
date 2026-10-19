#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace forge::domain {
// Local sketch coordinates. Bulge=tan(sweep/4) for the outgoing circular arc;
// zero makes a line. Positive sweep is counterclockwise in the local UV plane.
// 默认比较用于命令模式判断“定义是否真的改变”，避免无变化的编辑也压入历史。
struct ProfileVertex { double u=0, v=0, bulge=0; bool operator==(const ProfileVertex&) const = default; };
struct PathPoint {
    std::array<double,3> point{};
    std::optional<std::array<double,3>> through; // Optional point on outgoing 3D arc.
    bool operator==(const PathPoint&) const = default;
};
struct FeatureDefinition {
    std::vector<ProfileVertex> vertices;
    std::vector<PathPoint> pathPoints;
    std::vector<std::string> edgeIds;
    bool empty() const { return vertices.empty() && pathPoints.empty() && edgeIds.empty(); }
    bool operator==(const FeatureDefinition&) const = default;
};
}
