#include "domain/DependencyGraph.h"

namespace forge::domain {

    void DependencyGraph::addNode(const std::string& id)
    {
        deps_[id];
    }
    std::size_t DependencyGraph::size() const
    {
        return deps_.size();
    }
    bool DependencyGraph::addDependency(const std::string& featureId,const std::string& dependsOnId) {
        if (!deps_.contains(featureId) ||!deps_.contains(dependsOnId)) {
            return false;
        }
        if (featureId == dependsOnId) {
            return false;
        }

        auto& dependencies = deps_[featureId];
        for (const std::string& existing : dependencies) {
            if (existing == dependsOnId) {
                return false;
            }
        }
        if (reachable(dependsOnId, featureId)) {
            return false;
        }
        dependencies.push_back(dependsOnId);

        return true;
    }
    bool DependencyGraph::reachable(const std::string& start,const std::string& target) const{
        if (start == target) {
            return true;
        }

        const auto it = deps_.find(start);
        if (it == deps_.end()) {
            return false;
        }

        for (const std::string& next : it->second) {
            if (reachable(next, target)) {
                return true;
            }
        }

        return false;
    }
    std::vector<std::string> DependencyGraph::dependenciesOf(const std::string& id) const {
        const auto it = deps_.find(id);

        if (it == deps_.end()) {
            return {};
        }

        return it->second;
    }

    std::vector<std::string> DependencyGraph::dependentsOf(const std::string& id) const
    {
        std::vector<std::string> result;

        for (const auto& [featureId, dependencies] : deps_) {
            for (const std::string& required : dependencies) {
                if (required == id) {
                    result.push_back(featureId);
                    break;
                }
            }
        }

        return result;
    }

    std::vector<std::string> DependencyGraph::downstreamOf(const std::string& id) const {
        std::vector<std::string> result;
        std::vector<std::string> pending{id};

        while (!pending.empty()) {
            const std::string current = pending.back();
            pending.pop_back();

            // 下一步：找出依赖 current 的特征
        }

        return result;
    }
} // namespace forge::domain