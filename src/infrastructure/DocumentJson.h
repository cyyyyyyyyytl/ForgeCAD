#pragma once
#include "application/DocumentData.h"
#include <QByteArray>
namespace forge::infrastructure {
class DocumentJson {
public:
    static QByteArray encode(const application::DocumentData& data);
    static application::DocumentData decode(const QByteArray& bytes);
};
}
