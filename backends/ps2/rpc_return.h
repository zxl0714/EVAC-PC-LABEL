#pragma once
#include <cstdint>

namespace cave::ps2 {
inline constexpr uint32_t maxIopRpcBytes = 0x100000u;
struct IopRpcReply {
    uint64_t epoch{};
    uint32_t client{}, receive{}, thread{}, endFunction{}, endArgument{}, gp{};
    bool blocking{};
};
struct EeIopRpcReturn {
    uint32_t packet{}, receiveSize{};
    IopRpcReply reply{};
};
struct EeRpcReturn {
    uint32_t clientPtr{}, serverPtr{}, sid{}, rpcNum{}, mode{};
    uint32_t sendBuf{}, sendSize{}, receiveBuffer{}, receiveSize{};
    uint32_t endFunction{}, endParameter{}, completionSemaphore{}, guestDefaultResult{}, resultPointer{};
    bool callback{}, handledByIop{}, serverDispatched{}, suppressCallback{}, handled{}, copiedFallback{}, zeroedFallback{};
};
}
