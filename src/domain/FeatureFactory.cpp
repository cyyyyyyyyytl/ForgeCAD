#include "domain/FeatureFactory.h"        // 自己对应的头文件（.cpp 第一行永远是它）
#include "domain/BoxFeature.h"            // 要造 Box → 必须认识 BoxFeature
#include "domain/CylinderFeature.h"       // 要造 Cylinder → 必须认识 CylinderFeature
#include <stdexcept>                      // std::invalid_argument：未知类型/参数个数不对时抛
#include <string>                         // std::to_string（把"实际给了几个"拼进错误信息）

namespace forge::domain {

// ------------------------------------------------------------
// create：工厂的"分发台"
// ------------------------------------------------------------
// 逻辑：
//   ① 先查类型名：是 Box 还是 Cylinder（决定造谁、要几个参数）
//   ② 参数个数不对 = 调用方写错了（程序员的错）→ 抛异常，不让它带病运行
//   ③ 都匹配不上 = 未知类型 → 抛异常
// 返回值是基类指针：make_unique 产生的具体类指针自动"升级"成
//   unique_ptr<Feature>（多态转换），调用方拿到的永远是 Feature。
std::unique_ptr<Feature> FeatureFactory::create(const std::string& type,
                                                const std::string& id,
                                                const std::vector<double>& sizes) {
    if (type == "Box") {
        // Box 需要恰好 3 个数：length / width / height
        if (sizes.size() != 3) {
            throw std::invalid_argument("Box 需要 3 个尺寸参数(长/宽/高)，实际给了 "
                                        + std::to_string(sizes.size()) + " 个");
        }
        return std::make_unique<BoxFeature>(id, sizes[0], sizes[1], sizes[2]);
    }
    if (type == "Cylinder") {
        // Cylinder 需要恰好 2 个数：radius / height
        if (sizes.size() != 2) {
            throw std::invalid_argument("Cylinder 需要 2 个尺寸参数(半径/高度)，实际给了 "
                                        + std::to_string(sizes.size()) + " 个");
        }
        return std::make_unique<CylinderFeature>(id, sizes[0], sizes[1]);
    }
    // 走到这说明 type 谁都不认识——调用方写错了，响亮地炸出来提醒
    throw std::invalid_argument("未知特征类型: " + type);
}

} // namespace forge::domain
