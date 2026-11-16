#pragma once
#include "death_label_session.h"
#include "input_timing.h"
#include "programs.h"
#include <functional>
#include <iostream>
#include <optional>

namespace cave::daioujou_ps2 {
struct DiscProgramResult {
    std::string_view next;
    bool completed{};
};

inline DiscProgramResult RunDiscProgram(const PS2Runtime::IoPaths& paths, cave::ps2::NativeHost& host,
                                        bool hosted, bool inputTiming,
                                        const std::function<void(PS2Runtime&)>& configure = {}) {
    PS2Runtime::setIoPaths(paths);
    const auto name = paths.elfPath.filename().string();
    PS2Runtime runtime(ProgramTable(name),host,ResolveDiscExecutable);
    runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
    if (configure) configure(runtime);
    if (!runtime.initialize() || !runtime.loadELF(paths.elfPath.string()))
        throw std::runtime_error("Cannot load DaiOuJou PS2 program: " + name);
    if (inputTiming) BindInputTiming(runtime,name);
    std::optional<DeathLabelSession> session;
    if (hosted) session.emplace(runtime);
    std::cerr<<"[ps2-disc-program] active="<<name<<'\n';
    runtime.run();
    return {runtime.requestedExecutable(),session && session->Completed()};
}
}
