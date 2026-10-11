#pragma once

#include <string>
#include <vector>

namespace bf2dev {

struct FeatureDescriptor {
    const char* id;
    const char* label;
    const char* category;
    bool toggle;
    bool enabled;
    bool wired;
    const char* frostAsset;
};

struct FeatureResult {
    bool ok = false;
    std::string message;
};

std::vector<FeatureDescriptor>& Features();
FeatureResult ExecuteFeature(FeatureDescriptor& feature);

} // namespace bf2dev
