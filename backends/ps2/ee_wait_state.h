#pragma once

#include "runtime/ee_scheduler.h"
#include "state_archive.h"

namespace cave::ps2 {
template<class T, class Archive>
T& WaitPayload(Archive&, EeWaitPayload& payload) {
    if constexpr (std::is_same_v<Archive, StateReader>) return payload.emplace<T>();
    else return std::get<T>(payload);
}

template<class Archive>
void TransferEeCompletion(Archive& ar, EeWaitCompletion& action) {
    // Tags follow the v1 file format, independently of the variant's storage layout.
    uint8_t tag{};
    if constexpr (std::is_same_v<Archive, StateWriter>) {
        if (std::holds_alternative<EeResumeAtPc>(action)) tag = 1;
        if (std::holds_alternative<EeInvertFieldParity>(action)) tag = 2;
        if (std::holds_alternative<EeDeferredPadRead>(action)) tag = 3;
        if (std::holds_alternative<EeMpegPicture>(action)) tag = 4;
        if (std::holds_alternative<EeCdStreamRead>(action)) tag = 5;
    }
    ar.Field(tag);
    if constexpr (std::is_same_v<Archive, StateReader>) {
        switch (tag) {
        case 0: action = std::monostate{}; break;
        case 1: action = EeResumeAtPc{}; break;
        case 2: action = EeInvertFieldParity{}; break;
        case 3: action = EeDeferredPadRead{}; break;
        case 4: action = EeMpegPicture{}; break;
        case 5: action = EeCdStreamRead{}; break;
        default: throw std::invalid_argument("Unknown EE wait action");
        }
    }
    if (auto* resume = std::get_if<EeResumeAtPc>(&action)) ar.Field(resume->pc);
    if (auto* cd = std::get_if<EeCdStreamRead>(&action)) {
        ar.Fields(cd->requestedSectors, cd->buffer, cd->errorAddress, cd->sectorsRead);
        if (cd->sectorsRead > cd->requestedSectors) throw std::invalid_argument("Invalid EE CD read progress");
    }
}

template<class Archive>
void TransferEeWait(Archive& ar, EeWaitState& wait) {
    uint8_t reason = static_cast<uint8_t>(wait.reason);
    ar.Field(reason);
    if constexpr (std::is_same_v<Archive, StateReader>) wait.reason = static_cast<EeWaitReason>(reason);
    switch (wait.reason) {
    case EeWaitReason::None:
    case EeWaitReason::Sleep:
        if constexpr (std::is_same_v<Archive, StateReader>) wait.payload = std::monostate{};
        break;
    case EeWaitReason::Semaphore: ar.Field(WaitPayload<EeSemaphoreWait>(ar, wait.payload).id); break;
    case EeWaitReason::EventFlag: {
        auto& w = WaitPayload<EeEventFlagWait>(ar, wait.payload);
        ar.Fields(w.id, w.bits, w.mode, w.resultAddress);
        break;
    }
    case EeWaitReason::VSync: {
        auto& w = WaitPayload<EeVSyncWait>(ar, wait.payload);
        ar.Fields(w.afterTick, w.fixedResult);
        break;
    }
    case EeWaitReason::External:
    case EeWaitReason::Mpeg: {
        auto& w = WaitPayload<EeExternalWait>(ar, wait.payload);
        ar.Fields(w.type, w.token);
        break;
    }
    default: throw std::invalid_argument("Invalid EE wait reason");
    }
    TransferEeCompletion(ar, wait.completion);
}
}
