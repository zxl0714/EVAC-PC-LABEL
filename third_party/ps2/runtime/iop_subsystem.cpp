#include "ps2x/iop/iop_subsystem.h"

#include "iop_service.h"
#include "iop_module_manager.h"
#include "emulator/iop_emulator.h"
#include "module_factories.h"
#include "ps2x/iop/ps2_path.h"
#include "../../../backends/ps2/state_archive.h"

#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ps2x::iop
{
    namespace detail
    {
        struct IopModuleState
        {
            static void write(cave::ps2::StateWriter& ar, const IopModuleManager& manager)
            {
                ar.Field(manager.m_nextHleId);
                std::map<int32_t, IopModuleManager::Record> records(manager.m_records.begin(), manager.m_records.end());
                ar.Items(records, UINT32_MAX, 13, [&](const auto& item) {
                    ar.Field(item.first);
                    ar.String(item.second.key);
                    ar.Fields(item.second.references, item.second.physical);
                });
                std::map<std::string, uint32_t> refs(manager.m_loadedKeyReferences.begin(), manager.m_loadedKeyReferences.end());
                ar.Items(refs, UINT32_MAX, 8, [&](const auto& item) {
                    ar.String(item.first);
                    ar.Field(item.second);
                });
            }

            static void read(cave::ps2::StateReader& ar, IopModuleManager& manager)
            {
                manager.reset();
                ar.Field(manager.m_nextHleId);
                if (manager.m_nextHleId < 0x40000000) throw std::invalid_argument("Invalid next IOP service ID");
                const auto records = ar.Count(UINT32_MAX, 13);
                for (uint32_t i = 0; i < records; ++i)
                {
                    int32_t id{};
                    ar.Field(id);
                    IopModuleManager::Record record;
                    record.key = ar.String(UINT32_MAX);
                    ar.Fields(record.references, record.physical);
                    if (id <= 0 || record.key.empty() || !record.references)
                        throw std::invalid_argument("Invalid IOP service record");
                    if (!record.physical && (id < 0x40000000 || id >= manager.m_nextHleId ||
                        !manager.m_hleIdsByKey.emplace(record.key, id).second))
                        throw std::invalid_argument("Invalid IOP service ID");
                    if (!manager.m_records.emplace(id, std::move(record)).second)
                        throw std::invalid_argument("Duplicate IOP module ID");
                }
                const auto keys = ar.Count(UINT32_MAX, 8);
                for (uint32_t i = 0; i < keys; ++i)
                {
                    auto key = ar.String(UINT32_MAX);
                    uint32_t refs{};
                    ar.Field(refs);
                    if (!refs || key.empty() || !manager.m_loadedKeyReferences.emplace(std::move(key), refs).second)
                        throw std::invalid_argument("Invalid IOP service reference count");
                }
            }
        };
    }

    class IopSubsystem::Impl
    {
    public:
        explicit Impl(IopHost &hostRef)
            : host(hostRef),
              emulator(hostRef)
        {
            coreServices.emplace_back(detail::createMcservService(host));
            coreServices.emplace_back(detail::createDbcmanService(host));
            coreServices.emplace_back(detail::createLibSdService(host));
            refreshServiceModuleKeys();
            rebuildRoutes();
        }

        bool serviceActive(const detail::IopService &service) const
        {
            return moduleManager.isLoaded(service.moduleAliases());
        }

        void refreshServiceModuleKeys()
        {
            std::vector<std::string> keys;
            for (const auto &service : coreServices)
            {
                for (std::string_view alias : service->moduleAliases())
                    keys.emplace_back(alias);
            }
            moduleManager.setServiceModuleKeys(std::move(keys));
        }

        using Routes = std::unordered_map<uint32_t, detail::IopService *>;
        Routes routesFor(const detail::IopModuleManager& manager, std::string& error) const
        {
            Routes result;
            error.clear();
            for (const auto &service : coreServices)
            {
                if (!manager.isLoaded(service->moduleAliases()))
                    continue;
                for (const uint32_t sid : service->sids())
                {
                    if (!result.emplace(sid, service.get()).second)
                    {
                        std::ostringstream out;
                        out << "duplicate IOP SID 0x" << std::hex << sid << " in core services";
                        error = out.str();
                        return {};
                    }
                }
            }
            return result;
        }

        void rebuildRoutes()
        {
            routes = routesFor(moduleManager, lastError);
        }

        void recordLoadOutcome(std::string_view path, bool hle)
        {
            constexpr size_t maxOutcomes = 32u;
            if (loadOutcomes.size() >= maxOutcomes || !loggedLoadPaths.emplace(path).second)
                return;
            std::string message = hle ? "[IOP:HLE] fallback module='" : "[IOP:load-failed] module='";
            message.append(path);
            message += hle ? "' physical IRX unavailable; using registered HLE provider"
                           : "' no HLE provider accepted the module; physical IRX was not loaded";
            loadOutcomes.push_back(message);
            host.log(hle ? LogLevel::Info : LogLevel::Warning, message);
        }

        IopHost &host;
        detail::ServiceList coreServices;
        Routes routes;
        std::vector<std::string> loadOutcomes;
        std::unordered_set<std::string> loggedLoadPaths;
        std::string lastError;
        detail::IopModuleManager moduleManager;
        detail::IopEmulator emulator;
    };

    IopSubsystem::IopSubsystem(IopHost &host)
        : m_impl(std::make_unique<Impl>(host))
    {
    }

    IopSubsystem::~IopSubsystem() = default;
    IopSubsystem::IopSubsystem(IopSubsystem &&) noexcept = default;
    IopSubsystem &IopSubsystem::operator=(IopSubsystem &&) noexcept = default;

    std::vector<uint8_t> IopSubsystem::saveState() const
    {
        std::vector<uint8_t> bytes;
        cave::ps2::StateWriter writer(bytes);
        writer.Field(uint32_t{1});
        detail::IopModuleState::write(writer, m_impl->moduleManager);
        writer.Blob(m_impl->emulator.saveState());
        return bytes;
    }

    void IopSubsystem::loadState(std::span<const uint8_t> bytes)
    {
        cave::ps2::StateReader reader(bytes);
        uint32_t version{};
        reader.Field(version);
        if (version != 1) throw std::invalid_argument("Unknown IOP subsystem state version");
        auto manager = m_impl->moduleManager;
        detail::IopModuleState::read(reader, manager);
        const auto emulatorState = reader.Blob();
        reader.Finish();
        std::string error;
        auto routes = m_impl->routesFor(manager, error);
        m_impl->emulator.loadState(emulatorState);
        m_impl->moduleManager = std::move(manager);
        m_impl->routes = std::move(routes);
        m_impl->lastError = std::move(error);
    }

    void IopSubsystem::reset()
    {
        m_impl->moduleManager.reset();
        m_impl->loadOutcomes.clear();
        m_impl->loggedLoadPaths.clear();
        for (auto &service : m_impl->coreServices)
        {
            if (service)
            {
                service->reset();
            }
        }
        m_impl->emulator.reset();
        m_impl->refreshServiceModuleKeys();
        m_impl->rebuildRoutes();
    }

    ModuleLoadResult IopSubsystem::loadModule(std::string_view path, const void *arguments, uint32_t argumentSize)
    {
        const ParsedPs2Path parsed = parsePs2Path(path);
        if (!parsed)
            return {true, -1, -1};

        if (parsed.device != Ps2PathDevice::Rom0)
        {
            ModuleLoadResult physical = m_impl->emulator.loadModule(path, arguments, argumentSize);
            if (physical.moduleId > 0)
            {
                m_impl->moduleManager.observePhysicalLoad(physical.moduleId, path);
                m_impl->rebuildRoutes();
                return physical;
            }
        }

        ModuleLoadResult hle = m_impl->moduleManager.loadHle(path);
        if (hle.moduleId > 0)
        {
            m_impl->rebuildRoutes();
            if (parsed.device != Ps2PathDevice::Rom0)
                m_impl->recordLoadOutcome(path, true);
        }
        else
        {
            m_impl->recordLoadOutcome(path, false);
        }
        return hle;
    }

    ModuleLoadResult IopSubsystem::loadModuleBuffer(uint32_t guestAddress, const void *arguments, uint32_t argumentSize)
    {
        return m_impl->emulator.loadModuleBuffer(guestAddress, arguments, argumentSize);
    }

    ModuleLoadResult IopSubsystem::loadModuleFromIopRam(uint32_t address, const void *arguments, uint32_t argumentSize)
    {
        return m_impl->emulator.loadModuleFromIopRam(address, arguments, argumentSize);
    }

    bool IopSubsystem::stopModule(int32_t moduleId, int32_t *result)
    {
        if (m_impl->moduleManager.stopHle(moduleId, result))
        {
            m_impl->rebuildRoutes();
            return true;
        }
        if (!m_impl->emulator.stopModule(moduleId, result))
            return false;
        m_impl->moduleManager.observePhysicalStop(moduleId);
        m_impl->rebuildRoutes();
        return true;
    }

    void IopSubsystem::runEeCycles(uint64_t eeCycles) noexcept
    {
        m_impl->emulator.runEeCycles(eeCycles);
    }

    void IopSubsystem::setAudioVolumes(unsigned stream, unsigned voices) noexcept
    {
        m_impl->emulator.setAudioVolumes(stream, voices);
    }

    void IopSubsystem::discardAudio() noexcept
    {
        m_impl->emulator.discardAudio();
    }

    size_t IopSubsystem::readPcm(void *stereo, size_t frames) noexcept
    {
        return m_impl->emulator.readPcm(stereo, frames);
    }

    RpcAbi IopSubsystem::selectRpcAbi(const RpcAbiRequest &request) const
    {
        for (const auto &service : m_impl->coreServices)
        {
            if (service && m_impl->serviceActive(*service))
            {
                const RpcAbi selected = service->selectRpcAbi(request);
                if (selected != RpcAbi::RuntimeDefault)
                {
                    return selected;
                }
            }
        }
        return RpcAbi::RuntimeDefault;
    }

    bool IopSubsystem::canBindRpc(uint32_t sid) const noexcept
    {
        if (m_impl->routes.find(sid) != m_impl->routes.end())
        {
            return true;
        }
        return m_impl->emulator.hasRpcServer(sid);
    }

    RpcResult IopSubsystem::handleRpc(const RpcRequest &request)
    {
        const auto route = m_impl->routes.find(request.sid);
        detail::IopService *hle = route != m_impl->routes.end() ? route->second : nullptr;

        RpcResult emulated = m_impl->emulator.handleRpc(request);
        if (emulated.handled || !hle)
        {
            return emulated;
        }
        return hle->handleRpc(request);
    }

    void IopSubsystem::completeEeRpc(const cave::ps2::IopRpcReply& reply, std::span<const uint8_t> bytes)
    {
        m_impl->emulator.completeEeRpc(reply, bytes);
    }

    void IopSubsystem::onSifTransfer(const SifTransfer &transfer)
    {
        for (auto &service : m_impl->coreServices)
        {
            if (service && m_impl->serviceActive(*service))
            {
                service->onSifTransfer(transfer);
            }
        }
        m_impl->emulator.onSifTransfer(transfer);
    }

    uint32_t IopSubsystem::allocateMemory(uint32_t size, uint32_t alignment)
    {
        return m_impl->emulator.allocateMemory(size, alignment);
    }

    bool IopSubsystem::freeMemory(uint32_t address)
    {
        return m_impl->emulator.freeMemory(address);
    }

    bool IopSubsystem::readMemory(uint32_t address, void *destination, size_t size) const
    {
        return m_impl->emulator.readMemory(address, destination, size);
    }

    bool IopSubsystem::writeMemory(uint32_t address, const void *source, size_t size)
    {
        return m_impl->emulator.writeMemory(address, source, size);
    }

    bool IopSubsystem::zeroMemory(uint32_t address, size_t size)
    {
        return m_impl->emulator.zeroMemory(address, size);
    }

    bool IopSubsystem::isMemoryRange(uint32_t address, size_t size) const
    {
        return m_impl->emulator.isMemoryRange(address, size);
    }

    DebugSnapshot IopSubsystem::debugSnapshot() const
    {
        DebugSnapshot snapshot;
        snapshot.emulatorCycles = m_impl->emulator.cycles();
        snapshot.emulatorInstructions = m_impl->emulator.instructions();
        snapshot.emulatorLoadedModules = m_impl->emulator.loadedModuleCount();
        snapshot.emulatorThreads = m_impl->emulator.threadCount();
        snapshot.emulatorRpcServers = m_impl->emulator.rpcServerCount();
        snapshot.diagnostics = m_impl->loadOutcomes;
        if (!m_impl->lastError.empty())
        {
            snapshot.diagnostics.push_back(m_impl->lastError);
        }

        for (const auto &service : m_impl->coreServices)
        {
            DebugService row;
            row.name = service->name();
            row.sids.assign(service->sids().begin(), service->sids().end());
            row.active = m_impl->serviceActive(*service);
            service->appendDebugMetrics(row.metrics);
            snapshot.services.push_back(std::move(row));
        }
        return snapshot;
    }
}
