#pragma once

#include <TopoDS_Shape.hxx>
#include <array>
#include <optional>

namespace forge::geometry {

struct ShapeAnalysis {
    bool empty = true;
    bool solidBody = false;
    int solids = 0, faces = 0, edges = 0, vertices = 0;
    // min XYZ followed by max XYZ, in world coordinates. Empty shapes have no bounds.
    std::optional<std::array<double, 6>> bounds;
    std::optional<double> volume;
    double area = 0;
    std::optional<std::array<double, 3>> volumeCentroid;
};

// Read-only analysis of exact B-Rep, independent of display tessellation and camera.
// Throws for null/invalid geometry; empty results are valid and have no bounding box.
class ShapeAnalyzer {
public:
    static ShapeAnalysis analyze(const TopoDS_Shape& shape);
};

} // namespace forge::geometry
