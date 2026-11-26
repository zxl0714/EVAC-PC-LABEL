#include "ps2_runtime.h"
#include "state_archive.h"
#include "sdk_state.h"
#include "Kernel/Syscalls/Helpers/State.h"

#include <algorithm>
#include <map>

namespace ps2_stubs {
namespace {
template<class Map, class Write>
void WriteMap(cave::ps2::StateWriter& out,const Map& values,Write write) {
    std::vector<typename Map::key_type> keys;
    keys.reserve(values.size());
    for (const auto& [key,value]:values) keys.push_back(key);
    std::sort(keys.begin(),keys.end());
    out.Items(keys,65536,1,[&](const auto& key){ write(key,values.at(key)); });
}
template<class Map,class Read>
Map ReadMap(cave::ps2::StateReader& in,size_t minimum,Read read) {
    Map values;
    const auto count=in.Count(65536,minimum);
    for (unsigned n=0;n<count;++n) {
        typename Map::key_type key{};
        typename Map::mapped_type value{};
        read(key,value);
        if (!values.emplace(std::move(key),std::move(value)).second)
            throw std::invalid_argument("Duplicate PS2 SDK state entry");
    }
    return values;
}
}
std::vector<uint8_t> saveSdkState() {
    std::scoped_lock lock(g_rpc_mutex,g_bootmode_mutex,g_tls_mutex,g_osd_mutex,g_sif_module_mutex);
    std::vector<uint8_t> bytes;
    cave::ps2::StateWriter out(bytes);
    out.Fields(uint32_t{1},g_rpc_initialized,g_rpc_next_id,g_rpc_packet_index,g_rpc_server_index,
               g_rpc_active_queue,g_bootmode_initialized,g_bootmode_pool_offset,g_tls_index,
               g_osd_config_initialized,g_osd_config_raw,g_osd_config2_raw,g_next_sif_module_id);
    WriteMap(out,g_rpc_servers,[&](auto key,const auto& v) { out.Fields(key,v.sid,v.sd_ptr); });
    WriteMap(out,g_rpc_clients,[&](auto key,const auto& v) { out.Fields(key,v.busy,v.last_rpc,v.sid); });
    WriteMap(out,g_bootmode_addresses,[&](auto key,auto address) { out.Fields(key,address); });
    WriteMap(out,g_sif_modules_by_id,[&](auto key,const auto& m) {
        out.Fields(key,m.id,m.refCount,m.loaded); out.String(m.path); out.String(m.pathKey);
    });
    WriteMap(out,g_sif_module_id_by_path,[&](const auto& path,auto id) { out.String(path); out.Field(id); });
    return bytes;
}
void loadSdkState(std::span<const uint8_t> bytes) {
    cave::ps2::StateReader in(bytes);
    uint32_t version{},nextRpc{},packet{},server{},queue{},bootOffset{},tls{},osd1{},osd2{};
    bool rpc{},boot{},osd{};
    int32_t nextModule{};
    in.Fields(version,rpc,nextRpc,packet,server,queue,boot,bootOffset,tls,osd,osd1,osd2,nextModule);
    if (version!=1 || bootOffset>kBootModePoolBytes || nextModule<=0)
        throw std::invalid_argument("Invalid PS2 SDK state");
    auto servers=ReadMap<decltype(g_rpc_servers)>(in,12,[&](auto& key,auto& v){ in.Fields(key,v.sid,v.sd_ptr); });
    auto clients=ReadMap<decltype(g_rpc_clients)>(in,13,[&](auto& key,auto& v){ in.Fields(key,v.busy,v.last_rpc,v.sid); });
    auto bootModes=ReadMap<decltype(g_bootmode_addresses)>(in,5,[&](auto& key,auto& addr){ in.Fields(key,addr); });
    auto modules=ReadMap<decltype(g_sif_modules_by_id)>(in,21,[&](auto& key,auto& m){
        in.Fields(key,m.id,m.refCount,m.loaded); m.path=in.String(4096); m.pathKey=in.String(4096);
        if (key!=m.id) throw std::invalid_argument("Invalid PS2 SIF module ID");
    });
    auto ids=ReadMap<decltype(g_sif_module_id_by_path)>(in,8,[&](auto& key,auto& id){
        key=in.String(4096); in.Field(id);
    });
    in.Finish();
    std::scoped_lock lock(g_rpc_mutex,g_bootmode_mutex,g_tls_mutex,g_osd_mutex,g_sif_module_mutex);
    g_rpc_servers=std::move(servers); g_rpc_clients=std::move(clients);
    g_rpc_initialized=rpc; g_rpc_next_id=nextRpc; g_rpc_packet_index=packet;
    g_rpc_server_index=server; g_rpc_active_queue=queue;
    g_bootmode_addresses=std::move(bootModes); g_bootmode_initialized=boot; g_bootmode_pool_offset=bootOffset;
    g_tls_index=tls; g_osd_config_initialized=osd; g_osd_config_raw=osd1; g_osd_config2_raw=osd2;
    g_sif_modules_by_id=std::move(modules); g_sif_module_id_by_path=std::move(ids);
    g_next_sif_module_id=nextModule;
}
}

std::vector<uint8_t> PS2Runtime::saveEeServices() const
{
    std::scoped_lock lock(m_guestHeapMutex, m_asyncCallbackStackMutex, m_eeKernelStateMutex);
    std::vector<uint8_t> bytes;
    cave::ps2::StateWriter ar(bytes);
    ar.Fields(uint32_t{1}, m_guestHeapBase, m_guestHeapEnd, m_guestHeapLimit, m_guestHeapSuggestedBase,
              m_guestHeapConfigured, m_asyncCallbackStackFloor, m_asyncCallbackStackTop);
    ar.Items(m_guestHeapBlocks, UINT32_MAX, 9, [&](const auto& block) {
        ar.Fields(block.addr, block.size, block.free);
    });
    std::vector<int> threads;
    for (const auto& [id, handlers] : m_eeExitHandlers) threads.push_back(id);
    std::sort(threads.begin(), threads.end());
    ar.Items(threads, UINT32_MAX, 8, [&](int id) {
        ar.Field(id);
        ar.Items(m_eeExitHandlers.at(id), UINT32_MAX, 8, [&](const auto& handler) {
            ar.Fields(handler.function, handler.argument);
        });
    });
    std::map<uint32_t, uint32_t> overrides(m_eeSyscallOverrides.begin(), m_eeSyscallOverrides.end());
    ar.Items(overrides, UINT32_MAX, 8, [&](const auto& item) { ar.Fields(item.first, item.second); });
    std::vector<uint32_t> mirrors(m_eeSyscallMirrorAddresses.begin(), m_eeSyscallMirrorAddresses.end());
    std::sort(mirrors.begin(), mirrors.end());
    ar.Items(mirrors, UINT32_MAX, 4, [&](uint32_t address) { ar.Field(address); });
    return bytes;
}

void PS2Runtime::loadEeServices(std::span<const uint8_t> bytes)
{
    cave::ps2::StateReader ar(bytes);
    uint32_t version{}, base{}, end{}, limit{}, suggested{}, floor{}, top{};
    bool configured{};
    ar.Fields(version, base, end, limit, suggested, configured, floor, top);
    // Some ELFs leave no room for an async stack (top <= floor).
    if (version != 1 || base > end || end > limit || limit > PS2_RAM_SIZE ||
        suggested > PS2_RAM_SIZE || floor > PS2_RAM_SIZE || top > PS2_RAM_SIZE)
        throw std::invalid_argument("Invalid EE heap or stack bounds");
    std::vector<GuestHeapBlock> blocks;
    uint32_t previousEnd = base;
    ar.Items(blocks, PS2_RAM_SIZE, 9, [&](GuestHeapBlock& block) {
        ar.Fields(block.addr, block.size, block.free);
        if (!configured || !block.size || block.addr < previousEnd ||
            block.addr > limit || block.size > limit - block.addr)
            throw std::invalid_argument("Invalid or overlapping EE heap allocation");
        previousEnd = block.addr + block.size;
    });
    decltype(m_eeExitHandlers) handlers;
    const auto threadCount = ar.Count(UINT32_MAX, 8);
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        int id{};
        ar.Field(id);
        auto [entry, inserted] = handlers.try_emplace(id);
        if (!inserted) throw std::invalid_argument("Duplicate EE exit handler owner");
        ar.Items(entry->second, UINT32_MAX, 8, [&](auto& handler) { ar.Fields(handler.function, handler.argument); });
    }
    decltype(m_eeSyscallOverrides) overrides;
    const auto overrideCount = ar.Count(UINT32_MAX, 8);
    for (uint32_t i = 0; i < overrideCount; ++i)
    {
        uint32_t number{}, function{};
        ar.Fields(number, function);
        if (!function || !overrides.emplace(number, function).second)
            throw std::invalid_argument("Invalid EE syscall override");
    }
    decltype(m_eeSyscallMirrorAddresses) mirrors;
    constexpr uint32_t mirrorLimit = 0x00080000u;
    const auto mirrorCount = ar.Count(mirrorLimit / 4, 4);
    for (uint32_t i = 0; i < mirrorCount; ++i)
    {
        uint32_t address{};
        ar.Field(address);
        if ((address & 3u) || address >= mirrorLimit || !mirrors.insert(address).second)
            throw std::invalid_argument("Invalid EE syscall mirror address");
    }
    ar.Finish();
    std::scoped_lock lock(m_guestHeapMutex, m_asyncCallbackStackMutex, m_eeKernelStateMutex);
    m_guestHeapBase = base;
    m_guestHeapEnd = end;
    m_guestHeapLimit = limit;
    m_guestHeapSuggestedBase = suggested;
    m_guestHeapConfigured = configured;
    m_asyncCallbackStackFloor = floor;
    m_asyncCallbackStackTop = top;
    m_guestHeapBlocks.swap(blocks);
    m_eeExitHandlers.swap(handlers);
    m_eeSyscallOverrides.swap(overrides);
    m_eeSyscallMirrorAddresses.swap(mirrors);
}
