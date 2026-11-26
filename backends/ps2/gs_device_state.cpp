#include "gs_device_private.h"
#include "state_archive.h"
#include "shaders/swizzle_utils.h"

namespace ParallelGS {
using cave::ps2::StateReader;
using cave::ps2::StateWriter;

// Keep the serializer here so the pinned renderer only needs to grant access.
// Vulkan allocations, recorded command buffers and draw-handler pointers are
// rebuilt locally. Palette contents can outlive their original VRAM pixels.
struct SnapshotAccess {
    static constexpr uint32_t Version = 1;
    static constexpr uint32_t MaxTransferBytes = 4096u * 4096u * 4u;
    using Transfer = GSInterface::TransferState;
    using Vertices = decltype(GSInterface::vertex_queue);
    using Palette = std::array<uint8_t, CLUTSize>;

    template<class Archive>
    static void Registers(Archive& ar, RegisterState& r, PrivRegisterState& p) {
        for (auto& c : r.ctx) {
            ar.Fields(c.tex0.bits, c.tex1.bits, c.clamp.bits, c.xyoffset.bits,
                      c.miptbl_1_3.bits, c.miptbl_4_6.bits, c.scissor.bits,
                      c.alpha.bits, c.test.bits, c.fba.bits, c.frame.bits, c.zbuf.bits);
        }
        ar.Fields(r.prim.bits, r.rgbaq.bits, r.st.bits, r.uv.bits, r.fog.bits,
                  r.prmodecont.bits, r.texclut.bits, r.texa.bits, r.fogcol.bits, r.dimx.bits,
                  r.dthe.bits, r.colclamp.bits, r.pabe.bits, r.bitbltbuf.bits,
                  r.trxpos.bits, r.trxreg.bits, r.trxdir.bits, r.scanmsk.bits,
                  r.internal_q, r.cached_cbp[0], r.cached_cbp[1]);
        for (unsigned i = 0; i <= 28; i += 2) ar.Field(p.qwords_lo[i]);
        for (unsigned i : {0u, 2u, 8u, 16u}) ar.Field(p.qwords_hi[i]);
    }

    template<class Archive>
    static void Geometry(Archive& ar, GIFPath (&paths)[4], Vertices& vertices) {
        for (auto& path : paths) {
            Reg128<GIFTagBits> tag(path.tag);
            ar.Fields(tag.qwords[0], tag.qwords[1], path.reg, path.loop);
            if (path.loop > tag.desc.NLOOP || path.reg >= (tag.desc.NREG ? tag.desc.NREG : 16u))
                throw std::invalid_argument("Invalid GIF tag cursor");
            path.tag = tag.desc;
        }
        ar.Field(vertices.count);
        if (vertices.count > Vertices::MaxEntries) throw std::invalid_argument("Invalid GS vertex count");
        for (unsigned i = 0; i < vertices.count; ++i) {
            auto& p = vertices.pos[i];
            auto& a = vertices.attr[i];
            ar.Fields(p.pos.x, p.pos.y, p.z, a.st.x, a.st.y, a.q, a.rgba, a.fog, a.uv.x, a.uv.y);
        }
    }

    template<class Archive>
    static void TransferRegisters(Archive& ar, Transfer& t) {
        ar.Fields(t.copy.bitbltbuf.bits, t.copy.trxpos.bits, t.copy.trxreg.bits,
                  t.copy.trxdir.bits, t.copy.needs_shadow_vram,
                  t.host_to_local_active, t.required_qwords, t.last_flushed_qwords);
    }

    static void WriteTransfer(StateWriter& out, Transfer& t) {
        TransferRegisters(out, t);
        out.Blob({reinterpret_cast<const uint8_t*>(t.host_to_local_payload.data()),
                  t.host_to_local_payload.size() * sizeof(uint64_t)});
        const size_t remaining = 16ull * (t.fifo_readback_128b_size - t.fifo_readback_128b_offset);
        const uint8_t* fifo = remaining ? t.fifo_readback.data() + 16ull * t.fifo_readback_128b_offset : nullptr;
        out.Blob({fifo, remaining});
    }

    static void ReadTransfer(StateReader& in, Transfer& t) {
        TransferRegisters(in, t);
        const auto payload = in.Blob(MaxTransferBytes);
        const auto fifo = in.Blob(MaxTransferBytes);
        const size_t qwords = payload.size() / sizeof(uint64_t);
        if (payload.size() % sizeof(uint64_t) || fifo.size() % 16 ||
            t.last_flushed_qwords != qwords || t.required_qwords > MaxTransferBytes / sizeof(uint64_t))
            throw std::invalid_argument("Invalid GS transfer size");
        if (t.host_to_local_active) {
            const auto& rect = t.copy.trxreg.desc;
            const uint32_t required = rect.RRW * rect.RRH * get_bits_per_pixel(t.copy.bitbltbuf.desc.DPSM) / 64;
            if (t.copy.trxdir.desc.XDIR != HOST_TO_LOCAL || t.required_qwords != required || qwords >= required)
                throw std::invalid_argument("Invalid pending GS upload");
        } else if (qwords) throw std::invalid_argument("Unexpected GS upload payload");
        t.host_to_local_payload.resize(qwords);
        if (!payload.empty()) std::memcpy(t.host_to_local_payload.data(), payload.data(), payload.size());
        if (!fifo.empty()) {
            t.fifo_readback.reserve(fifo.size());
            if (!t.fifo_readback.data()) throw std::bad_alloc();
            std::memcpy(t.fifo_readback.data(), fifo.data(), fifo.size());
        }
        t.fifo_readback_128b_size = uint32_t(fifo.size() / 16);
    }

    static Palette ReadPalette(GSInterface& gs) {
        auto& renderer = gs.renderer;
        auto& device = *renderer.device;
        Vulkan::BufferCreateInfo info{};
        info.size = CLUTSize;
        info.domain = Vulkan::BufferDomain::CachedHost;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        auto staging = device.create_buffer(info);
        if (!staging) throw std::runtime_error("Could not allocate GS palette readback");
        auto cmd = device.request_command_buffer();
        auto& clut = *renderer.buffers.clut;
        cmd->buffer_barrier(clut, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        cmd->copy_buffer(*staging, 0, clut, gs.render_pass.clut_instance * CLUTSize, CLUTSize);
        cmd->buffer_barrier(*staging, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        Vulkan::Fence fence;
        device.submit(cmd, &fence);
        fence->wait();
        auto* mapped = device.map_host_buffer(*staging, Vulkan::MEMORY_ACCESS_READ_BIT);
        if (!mapped) throw std::runtime_error("Could not map GS palette readback");
        Palette palette;
        std::memcpy(palette.data(), mapped, palette.size());
        device.unmap_host_buffer(*staging, Vulkan::MEMORY_ACCESS_READ_BIT);
        return palette;
    }

    static void InstallPalette(GSInterface& gs, std::span<const uint8_t> palette) {
        auto& renderer = gs.renderer;
        const unsigned slot = (renderer.next_clut_instance + 1) % CLUTInstances;
        auto& device = *renderer.device;
        auto& clut = *renderer.buffers.clut;
        auto cmd = device.request_command_buffer();
        std::memcpy(cmd->update_buffer(clut, slot * CLUTSize, CLUTSize), palette.data(), CLUTSize);
        cmd->buffer_barrier(clut, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        Vulkan::Fence fence;
        device.submit(cmd, &fence);
        fence->wait();
        renderer.rewind_clut_instance(slot);
        gs.render_pass.clut_instance = gs.render_pass.latest_clut_instance = slot;
        gs.render_pass.num_memoized_palettes = 0;
    }

    static std::vector<uint8_t> Save(GSInterface& gs) {
        gs.flush();
        std::vector<uint8_t> result;
        result.reserve(cave::ps2::GsDevice::VramBytes + 4096);
        StateWriter out(result);
        out.Field(Version);
        Registers(out, gs.registers, gs.priv_registers);
        Geometry(out, gs.paths, gs.vertex_queue);
        WriteTransfer(out, gs.transfer_state);
        out.Bytes(ReadPalette(gs));
        constexpr size_t bytes = cave::ps2::GsDevice::VramBytes;
        out.Bytes({static_cast<const uint8_t*>(gs.map_vram_read(0, bytes)), bytes});
        return result;
    }

    static void Load(GSInterface& gs, std::span<const uint8_t> state) {
        StateReader in(state);
        uint32_t version{};
        in.Field(version);
        if (version != Version) throw std::invalid_argument("Unsupported GS drawing state");
        RegisterState registers{};
        PrivRegisterState priv{};
        Vertices vertices{};
        GIFPath paths[4]{};
        Transfer transfer{};
        Registers(in, registers, priv);
        Geometry(in, paths, vertices);
        ReadTransfer(in, transfer);
        auto palette = in.Take(CLUTSize);
        auto vram = in.Take(cave::ps2::GsDevice::VramBytes);
        in.Finish();

        gs.flush();
        std::memcpy(gs.map_vram_write(0, vram.size()), vram.data(), vram.size());
        gs.end_vram_write(0, vram.size());
        InstallPalette(gs, palette);
        gs.num_promoted_backbuffers = 0;
        gs.registers = registers;
        gs.priv_registers = priv;
        gs.transfer_state = std::move(transfer);
        std::copy(std::begin(paths), std::end(paths), gs.paths);
        gs.vertex_queue = vertices;
        const auto& offset = registers.ctx[registers.prim.desc.CTXT].xyoffset.desc;
        gs.render_pass.ofx = int32_t(offset.OFX);
        gs.render_pass.ofy = int32_t(offset.OFY);
        gs.clobber_register_state();
    }
};
}

namespace cave::ps2 {
std::vector<uint8_t> GsDevice::SaveDrawingState() {
    impl->CheckOwner();
    return ParallelGS::SnapshotAccess::Save(impl->gs);
}

void GsDevice::LoadDrawingState(std::span<const uint8_t> state) {
    impl->CheckOwner();
    ParallelGS::SnapshotAccess::Load(impl->gs, state);
    ++impl->workSerial;
    impl->blitFields = {};
    for (auto& lane : impl->fieldHistory) for (auto& field : lane) field.reset();
    impl->lastField.reset();
}
}
