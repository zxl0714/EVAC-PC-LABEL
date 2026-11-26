#pragma once
#include <cstdint>
#include "../../../backends/ps2/rpc_return.h"
#include <span>

namespace cave::ps2 {
// Optional host extension for IOP clients of an EE RPC server.
class EeRpcHost {
public:
    virtual ~EeRpcHost() = default;
    virtual uint32_t FindEeRpc(uint32_t sid) const = 0;
    virtual bool QueueEeRpc(uint32_t server, uint32_t function, std::span<const uint8_t> request,
                            uint32_t receiveSize, IopRpcReply reply) = 0;
};
}
