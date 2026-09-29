#pragma once
#include "domain/FeatureDefinition.h"
#include <QJsonObject>
namespace forge::infrastructure {
QJsonObject encodeDefinition(const domain::FeatureDefinition& definition);
domain::FeatureDefinition decodeDefinition(const QJsonObject& object);
}
