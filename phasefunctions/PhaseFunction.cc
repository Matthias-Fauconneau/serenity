#include "PhaseFunction.h"

namespace Tungsten {

void PhaseFunction::fromJson(const rapidjson::Value &/*v*/, const Scene &/*scene*/)
{
}

rapidjson::Value PhaseFunction::toJson(Allocator &allocator) const
{
    return JsonSerializable::toJson(allocator);
}

}
