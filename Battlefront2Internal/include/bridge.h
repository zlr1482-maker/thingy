#pragma once

#include <string>

namespace bf2dev {

struct BridgeResult {
    bool ok = false;
    int httpStatus = 0;
    std::string message;
};

BridgeResult BridgeHealthCheck();
BridgeResult BridgeForceStartLobby();

} // namespace bf2dev
