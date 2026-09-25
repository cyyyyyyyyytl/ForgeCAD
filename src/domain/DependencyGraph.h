#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace forge::domain {

    class DependencyGraph {
    public:
        void addNode(const std::string& id);
        std::size_t size() const;
        bool addDependency(const std::string& featureId,const std::string& dependsOnId);
        std::vector<std::string> dependenciesOf(const std::string& id) const;
        std::vector<std::string> dependentsOf( const std::string& id) const;
        std::vector<std::string> downstreamOf(const std::string& id) const;
    private:
        // 一个编号对应一张“它需要谁”的名单。
        std::unordered_map<std::string, std::vector<std::string>> deps_;
        bool reachable(const std::string& start,const std::string& target) const;

    };

} // namespace forge::domain