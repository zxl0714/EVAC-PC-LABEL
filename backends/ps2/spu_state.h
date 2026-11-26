#pragma once
#include "Iop_Spu2.h"
#include "state_archive.h"

namespace Iop {
struct SpuStateAccess {
    template<class Archive, class Register>
    static void Packed(Archive& archive, Register& reg) {
        uint16_t word = reg;
        archive.Field(word);
        if constexpr (std::is_same_v<Archive, cave::ps2::StateReader>) reg <<= word;
    }

    template<class Archive>
    static void Voice(Archive& archive, CSpuBase::CHANNEL& voice, CSpuBase::CSampleReader& reader) {
        Packed(archive, voice.volumeLeft);
        Packed(archive, voice.volumeRight);
        archive.Fields(voice.volumeLeftAbs, voice.volumeRightAbs, voice.pitch, voice.address);
        Packed(archive, voice.adsrLevel);
        Packed(archive, voice.adsrRate);
        archive.Fields(voice.adsrVolume, voice.repeat, voice.repeatSet, voice.status, voice.current);
        archive.Fields(reader.m_srcSampleIdx, reader.m_srcSamplingRate, reader.m_nextSampleAddr,
                       reader.m_repeatAddr, reader.m_pitch, reader.m_s1, reader.m_s2,
                       reader.m_done, reader.m_nextValid, reader.m_endFlag, reader.m_didChangeRepeat);
        for (auto& sample : reader.m_buffer) archive.Field(sample);
        if constexpr (std::is_same_v<Archive, cave::ps2::StateReader>) {
            if (voice.status > CSpuBase::RELEASE || reader.m_nextSampleAddr > reader.m_ramSize - 16 ||
                reader.m_repeatAddr > reader.m_ramSize - 16 ||
                reader.m_srcSampleIdx / 0x1000 >= std::size(reader.m_buffer) - 1)
                throw std::invalid_argument("Invalid SPU2 voice cursor");
            reader.UpdateSampleStep();
        }
    }

    template<class Archive>
    static void Core(Archive& archive, CSpuBase& core) {
        archive.Fields(core.m_baseSamplingRate, core.m_ctrl, core.m_irqAddr, core.m_irqPending,
                       core.m_transferMode, core.m_transferAddr, core.m_core0OutputOffset,
                       core.m_channelOn.f, core.m_channelReverb.f, core.m_reverbWorkAddrStart,
                       core.m_reverbWorkAddrEnd, core.m_reverbCurrAddr, core.m_reverbTicks,
                       core.m_inputVolL, core.m_inputVolR, core.m_extInputVolL, core.m_extInputVolR);
        for (auto& reg : core.m_reverb) archive.Field(reg);
        for (unsigned i = 0; i < CSpuBase::MAX_CHANNEL; ++i)
            Voice(archive, core.m_channel[i], core.m_reader[i]);
        auto& block = core.m_blockReader;
        archive.Fields(core.m_blockWritePtr, block.m_baseSamplingRate, block.m_srcSampleIdx,
                       block.m_spdifBypass);
        archive.Bytes(block.m_blockBuffer);
        if constexpr (std::is_same_v<Archive, cave::ps2::StateReader>) {
            if (core.m_blockWritePtr > sizeof(block.m_blockBuffer) ||
                core.m_transferAddr > core.m_ramSize || core.m_core0OutputOffset >= 0x200 ||
                core.m_reverbWorkAddrEnd > core.m_ramSize)
                throw std::invalid_argument("Invalid SPU2 transfer state");
            block.UpdateSampleStep();
        }
    }

    template<class Archive>
    static void Transfer(Archive& archive, CSpu2& regs, CSpuIrqWatcher& irq) {
        archive.Fields(regs.m_spdifOutput, regs.m_spdifMode, regs.m_spdifMedia);
        for (unsigned i = 0; i < 2; ++i) {
            archive.Fields(irq.m_irqAddr[i], irq.m_irqPending[i], regs.m_core[i]->m_baseSamplingRate);
            Core(archive, regs.m_core[i]->m_spuBase);
        }
    }
};
}
