#include "bridge.h"

namespace bf2dev {

BridgeResult BridgeHealthCheck() {
    return {
        true,
        0,
        "Standalone mode active. No Xen or localhost bridge dependency."
    };
}

BridgeResult BridgeForceStartLobby() {
    return {
        false,
        0,
        "Local runtime handler not wired yet."
    };
}

} // namespace bf2dev
