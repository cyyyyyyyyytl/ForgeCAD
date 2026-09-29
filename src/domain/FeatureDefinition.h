#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace forge::domain {
// Local sketch coordinates. Bulge=tan(sweep/4) for the outgoing circular arc;
// zero makes a line. Positive sweep is counterclockwise in the local UV plane.
struct ProfileVertex { double u=0, v=0, bulge=0; };
struct PathPoint {
    std::array<double,3> point{};
    std::optional<std::array<double,3>> through; // Optional point on outgoing 3D arc.
};
struct FeatureDefinition {
    std::vector<ProfileVertex> vertices;
    std::vector<PathPoint> pathPoints;
    std::vector<std::string> edgeIds;
    bool empty() const { return vertices.empty() && pathPoints.empty() && edgeIds.empty(); }
};
}
