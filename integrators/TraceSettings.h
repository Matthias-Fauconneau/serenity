#pragma once
#include "io/JsonUtils.h"

struct TraceSettings
{
    bool enableConsistencyChecks;
    bool enableTwoSidedShading;
    int minBounces;
    int maxBounces;

    TraceSettings()
    : enableConsistencyChecks(false),
      enableTwoSidedShading(true),
      minBounces(0),
      maxBounces(64)
    {
    }

    void fromJson(const rapidjson::Value &v)
    {
        ::fromJson(v, "min_bounces", minBounces);
        ::fromJson(v, "max_bounces", maxBounces);
        ::fromJson(v, "enable_consistency_checks", enableConsistencyChecks);
        ::fromJson(v, "enable_two_sided_shading", enableTwoSidedShading);
    }

    rapidjson::Value toJson(rapidjson::Document::AllocatorType &allocator) const
    {
        rapidjson::Value v(rapidjson::kObjectType);
        v.AddMember("min_bounces", minBounces, allocator);
        v.AddMember("max_bounces", maxBounces, allocator);
        v.AddMember("enable_consistency_checks", enableConsistencyChecks, allocator);
        v.AddMember("enable_two_sided_shading", enableTwoSidedShading, allocator);
        return v;
    }
};
