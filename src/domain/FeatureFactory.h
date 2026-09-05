#pragma once                              // 防止本文件被重复包含（每个头文件都写，编译器只处理一次）
#include <memory>                         // std::unique_ptr（返回类型）
#include <string>                         // std::string（类型名）
#include <vector>                         // std::vector（参数个数不定，用容器装）
#include "domain/Feature.h"               // 返回基类指针——调用方只认识 Feature

namespace forge::domain {

// ============================================================
// FeatureFactory：特征工厂
// ------------------------------------------------------------
// 大白话：调用方说"我要一个 Box，id=Box001，尺寸 100×50×30"，
//   工厂负责 new 出对应的具体类，并以基类指针（unique_ptr<Feature>）交回。
// 为什么要有它：
//   ① 创建的知识（哪个类型 → 哪个类 → 要几个参数）收拢在唯一一处；
//   ② 调用方从此只认识"类型名字符串"和 Feature 接口，不认识任何具体类；
//   ③ 以后加新特征：加一个子类 + 工厂加一个分支，现有代码零改动。
// ============================================================
class FeatureFactory {
public:
    // 按类型名造特征。
    //   type  = "Box"(3 个数：长/宽/高)、"Cylinder"(2 个数：半径/高度)
    //           或 "Sphere"(1 个数：半径)
    //   id    = 特征的身份证号
    //   sizes = 尺寸参数（个数不对 → 抛 std::invalid_argument）
    //   未知 type → 抛 std::invalid_argument（调用方写错了，响亮地炸出来提醒）
    static std::unique_ptr<Feature> create(const std::string& type,
                                           const std::string& id,
                                           const std::vector<double>& sizes);
};

} // namespace forge::domain
