#include "compatibility.h"
#include "assembler.h"
#include <Zydis/Zydis.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace cave::ps4::cpu {
namespace {
std::atomic<bool> force_tests{};
constexpr std::uint64_t flag_mask=0x8d5; // CF,PF,AF,ZF,SF,OF; preserve controls.
constexpr std::uint64_t carry=1,zero=0x40,sign=0x80;
constexpr std::size_t page_size=4096,arena_size=65536;

int Register(ZydisRegister reg) {
    if(reg==ZYDIS_REGISTER_NONE)return -1;
    const auto full=ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,reg);
    if(full<ZYDIS_REGISTER_RAX || full>ZYDIS_REGISTER_R15)return -2;
    return static_cast<int>(full-ZYDIS_REGISTER_RAX);
}
bool Decode(const ZydisDecodedInstruction& decoded,const ZydisDecodedOperand* operands,
            std::uintptr_t address,Instruction& result) {
    switch(decoded.mnemonic) {
    case ZYDIS_MNEMONIC_BEXTR:result.operation=Operation::Bextr;break;
    case ZYDIS_MNEMONIC_ANDN:result.operation=Operation::Andn;break;
    case ZYDIS_MNEMONIC_BLSI:result.operation=Operation::Blsi;break;
    case ZYDIS_MNEMONIC_BLSMSK:result.operation=Operation::Blsmsk;break;
    case ZYDIS_MNEMONIC_BLSR:result.operation=Operation::Blsr;break;
    case ZYDIS_MNEMONIC_LZCNT:result.operation=Operation::Lzcnt;break;
    case ZYDIS_MNEMONIC_TZCNT:result.operation=Operation::Tzcnt;break;
    case ZYDIS_MNEMONIC_MOVBE:result.operation=Operation::Movbe;break;
    default:return false;
    }
    result.store=result.operation==Operation::Movbe && operands[0].type==ZYDIS_OPERAND_TYPE_MEMORY;
    const auto& destination_operand=operands[result.store?1:0];
    if(destination_operand.type!=ZYDIS_OPERAND_TYPE_REGISTER ||
       (operands[0].size!=32 && operands[0].size!=64 &&
        !(operands[0].size==16 && (result.operation==Operation::Lzcnt || result.operation==Operation::Tzcnt || result.operation==Operation::Movbe))))return false;
    const auto destination=Register(destination_operand.reg.value);
    if(destination<0)return false;
    result.destination=static_cast<unsigned>(destination);
    result.bits=operands[0].size;
    result.length=decoded.length;result.next=address+decoded.length;
    unsigned source_index=result.store?0:1;
    if(result.operation==Operation::Andn) {
        if(operands[1].type!=ZYDIS_OPERAND_TYPE_REGISTER)return false;
        const auto control=Register(operands[1].reg.value);
        if(control<0)return false;
        result.control=static_cast<unsigned>(control);source_index=2;
    } else if(result.operation==Operation::Bextr) {
        if(operands[2].type!=ZYDIS_OPERAND_TYPE_REGISTER)return false;
        const auto control=Register(operands[2].reg.value);
        if(control<0)return false;
        result.control=static_cast<unsigned>(control);
    }
    const auto& source=operands[source_index];
    if(source.type==ZYDIS_OPERAND_TYPE_REGISTER) {
        result.source.reg=Register(source.reg.value);
        return result.source.reg>=0;
    }
    if(source.type!=ZYDIS_OPERAND_TYPE_MEMORY ||
       source.mem.segment==ZYDIS_REGISTER_FS || source.mem.segment==ZYDIS_REGISTER_GS)return false;
    auto& memory=result.source;
    memory.memory=true;memory.address_bits=decoded.address_width;
    memory.rip_relative=source.mem.base==ZYDIS_REGISTER_RIP || source.mem.base==ZYDIS_REGISTER_EIP;
    memory.base=memory.rip_relative?-1:Register(source.mem.base);
    memory.index=Register(source.mem.index);memory.scale=source.mem.scale;
    memory.displacement=source.mem.disp.has_displacement?source.mem.disp.value:0;
    return memory.base>=-1 && memory.index>=-1;
}
std::uint64_t Address(const Frame& frame,const Instruction& instruction) {
    const auto& source=instruction.source;
    std::uint64_t address=static_cast<std::uint64_t>(source.displacement);
    if(source.rip_relative)address+=instruction.next;
    if(source.base>=0)address+=frame.registers[static_cast<std::size_t>(source.base)];
    if(source.index>=0)address+=frame.registers[static_cast<std::size_t>(source.index)]*source.scale;
    return source.address_bits==32?static_cast<std::uint32_t>(address):address;
}
std::uint64_t Source(const Frame& frame,const Instruction& instruction) {
    if(!instruction.source.memory)return frame.registers[static_cast<std::size_t>(instruction.source.reg)];
    std::uint64_t value{};
    std::memcpy(&value,reinterpret_cast<const void*>(Address(frame,instruction)),instruction.bits/8);
    return value;
}
bool Store(std::uint64_t address,std::uint64_t value,unsigned bytes) {
    // A real write must reach the runtime's write-fault tracker. Debugger APIs
    // can bypass page protection and leave texture dirtiness stale.
    __try {std::memcpy(reinterpret_cast<void*>(address),&value,bytes);return true;}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
void Execute(Frame* frame,const Instruction* instruction) {
    const auto source=instruction->store?frame->registers[instruction->destination]:Source(*frame,*instruction);
    const auto control=frame->registers[instruction->control];
    const auto value=Evaluate(instruction->operation,instruction->bits,source,control,frame->flags);
    if(instruction->store){std::memcpy(reinterpret_cast<void*>(Address(*frame,*instruction)),&value,instruction->bits/8);return;}
    auto& destination=frame->registers[instruction->destination];
    destination=instruction->bits==16?(destination&~0xffffull)|value:value;
}
std::uint64_t& ContextRegister(CONTEXT& context,unsigned index) {
    switch(index) {
    case 0:return context.Rax;case 1:return context.Rcx;case 2:return context.Rdx;case 3:return context.Rbx;
    case 4:return context.Rsp;case 5:return context.Rbp;case 6:return context.Rsi;case 7:return context.Rdi;
    case 8:return context.R8;case 9:return context.R9;case 10:return context.R10;case 11:return context.R11;
    case 12:return context.R12;case 13:return context.R13;case 14:return context.R14;default:return context.R15;
    }
}
}

Features DetectFeatures() {
    int registers[4]{};Features result;
    __cpuid(registers,0);const auto maximum=registers[0];
    if(maximum>=1) {
        __cpuidex(registers,1,0);const auto ecx=static_cast<unsigned>(registers[2]);
        result.ssse3=(ecx&(1u<<9))!=0;result.sse41=(ecx&(1u<<19))!=0;
        result.movbe=(ecx&(1u<<22))!=0;
        const bool saved_avx=(ecx&(1u<<27)) && ((_xgetbv(0)&6)==6);
        result.avx=(ecx&(1u<<28)) && saved_avx;result.f16c=(ecx&(1u<<29)) && result.avx;
    }
    if(maximum>=7) {__cpuidex(registers,7,0);result.bmi1=(registers[1]&8)!=0;result.bmi2=(registers[1]&256)!=0;}
    __cpuid(registers,static_cast<int>(0x80000000u));
    if(static_cast<unsigned>(registers[0])>=0x80000001u) {
        __cpuid(registers,static_cast<int>(0x80000001u));result.lzcnt=(registers[2]&32)!=0;
    }
    return result;
}
void ConfigureForTests(bool force){force_tests.store(force);}
bool ForcedForTests(){return force_tests.load();}

std::uint64_t Evaluate(Operation operation,unsigned bits,std::uint64_t source,
                       std::uint64_t control,std::uint64_t& flags) {
    const auto mask=bits==64?~0ull:((1ull<<bits)-1);
    source&=mask;control&=mask;
    std::uint64_t result{},status{};
    switch(operation) {
    case Operation::Movbe:
        for(unsigned byte=0;byte<bits/8;++byte)result|=((source>>(byte*8))&255)<<(bits-8-byte*8);
        return result; // MOVBE preserves all flags.
    case Operation::Bextr: {
        const auto start=static_cast<unsigned>(control&255),length=static_cast<unsigned>((control>>8)&255);
        if(start<bits && length) {
            const auto kept=(std::min)(length,bits-start);
            result=(source>>start)&(kept==64?~0ull:((1ull<<kept)-1));
        }
        if(!result)status|=zero;break;
    }
    case Operation::Andn:result=(~control)&source;break;
    case Operation::Blsi:result=source&(~source+1);if(source)status|=carry;break;
    case Operation::Blsmsk:result=source^(source-1);if(!source)status|=carry;break;
    case Operation::Blsr:result=source&(source-1);if(!source)status|=carry;break;
    case Operation::Lzcnt:
    case Operation::Tzcnt: {
        // An explicit integer loop also works on CPUs where the LZCNT encoding
        // silently executes BSR. No unsupported intrinsic in the fallback.
        result=bits;
        for(unsigned bit=0;bit<bits;++bit) {
            const auto position=operation==Operation::Lzcnt?bits-1-bit:bit;
            if(source&(1ull<<position)){result=bit;break;}
        }
        if(!source)status|=carry;if(!result)status|=zero;break;
    }
    default:break;
    }
    result&=mask;
    if(operation!=Operation::Bextr && operation!=Operation::Lzcnt && operation!=Operation::Tzcnt) {
        if(!result)status|=zero;if(result&(1ull<<(bits-1)))status|=sign;
    }
    flags=(flags&~flag_mask)|status;
    return result;
}

struct Image::Implementation {
    struct Arena {std::uint8_t* memory{};std::size_t used{};};
    Features features;bool force;Statistics statistics;
    std::vector<Arena> arenas;
    std::vector<std::unique_ptr<Instruction>> instructions;
    std::unordered_map<std::uintptr_t,const Instruction*> traps;
    explicit Implementation(Features features_,bool force_):features(features_),force(force_){}
    ~Implementation(){for(const auto& arena:arenas)VirtualFree(arena.memory,0,MEM_RELEASE);}
    bool Needed(Operation operation)const{return force || (operation==Operation::Movbe?!features.movbe:operation==Operation::Lzcnt?!features.lzcnt:!features.bmi1);}
    std::uint8_t* Page(std::uintptr_t source) {
        if(!arenas.empty() && arenas.back().used<arena_size &&
           std::llabs(static_cast<long long>(reinterpret_cast<std::uintptr_t>(arenas.back().memory))-static_cast<long long>(source))<0x70000000ll) {
            auto& arena=arenas.back();auto* result=arena.memory+arena.used;
            if(!VirtualAlloc(result,page_size,MEM_COMMIT,PAGE_READWRITE))throw std::runtime_error("Cannot commit CPU adapter");
            arena.used+=page_size;return result;
        }
        SYSTEM_INFO system{};GetSystemInfo(&system);
        const auto low=(std::max)(reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress),source>0x70000000?source-0x70000000:0);
        const auto high=(std::min)(reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress),source+0x70000000);
        auto cursor=(source+arena_size-1)&~static_cast<std::uintptr_t>(arena_size-1);
        for(unsigned direction=0;direction<2;++direction) {
            if(direction)cursor=(low+arena_size-1)&~static_cast<std::uintptr_t>(arena_size-1);
            while(cursor<high && arena_size<=high-cursor) {
                MEMORY_BASIC_INFORMATION region{};
                if(!VirtualQuery(reinterpret_cast<void*>(cursor),&region,sizeof(region)))break;
                if(region.State==MEM_FREE && arena_size<=region.RegionSize) {
                    auto* allocation=static_cast<std::uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(cursor),arena_size,MEM_RESERVE,PAGE_NOACCESS));
                    if(allocation){arenas.push_back({allocation,0});return Page(source);}
                }
                const auto end=reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
                if(end<=cursor)break;cursor=(end+arena_size-1)&~static_cast<std::uintptr_t>(arena_size-1);
            }
        }
        throw std::runtime_error("Cannot reserve a nearby CPU adapter arena");
    }
    std::uint8_t* Adapter(const Instruction* instruction) {
        auto* memory=Page(instruction->next);
        Assembler code(memory,page_size);
        const auto rsp=Reg(ZYDIS_REGISTER_RSP),rax=Reg(ZYDIS_REGISTER_RAX),rbx=Reg(ZYDIS_REGISTER_RBX);
        // Preserve guest state across the Windows helper, including YMM upper
        // halves: the host CRT may use AVX and issue VZEROUPPER internally.
        code.Emit(ZYDIS_MNEMONIC_LEA,{rsp,Mem(ZYDIS_REGISTER_RSP,-128,8)});code.Emit(ZYDIS_MNEMONIC_PUSHFQ);
        for(int reg=15;reg>=0;--reg)code.Emit(ZYDIS_MNEMONIC_PUSH,{Reg(Gpr(static_cast<unsigned>(reg)))});
        code.Emit(ZYDIS_MNEMONIC_LEA,{rax,Mem(ZYDIS_REGISTER_RSP,264,8)});
        code.Emit(ZYDIS_MNEMONIC_MOV,{Mem(ZYDIS_REGISTER_RSP,32,8),rax});
        code.Emit(ZYDIS_MNEMONIC_MOV,{rbx,rsp});
        code.Emit(ZYDIS_MNEMONIC_AND,{rsp,Imm(static_cast<std::uint64_t>(-16ll))});
        const unsigned vector_bytes=features.avx?32:16,vector_count=features.avx?16:6;
        code.Emit(ZYDIS_MNEMONIC_SUB,{rsp,Imm(32+vector_bytes*vector_count)});
        const auto vector_register=[&](unsigned reg){return Reg(static_cast<ZydisRegister>((features.avx?ZYDIS_REGISTER_YMM0:ZYDIS_REGISTER_XMM0)+reg));};
        for(unsigned reg=0;reg<vector_count;++reg)code.Emit(features.avx?ZYDIS_MNEMONIC_VMOVDQU:ZYDIS_MNEMONIC_MOVDQU,
            {Mem(ZYDIS_REGISTER_RSP,32+reg*vector_bytes,vector_bytes),vector_register(reg)},features.avx?ZYDIS_ENCODABLE_ENCODING_VEX:ZYDIS_ENCODABLE_ENCODING_LEGACY);
        if(features.avx)code.Emit(ZYDIS_MNEMONIC_VZEROUPPER,{},ZYDIS_ENCODABLE_ENCODING_VEX);
        code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_RCX),rbx});
        code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_RDX),Imm(reinterpret_cast<std::uintptr_t>(instruction))});
        code.Emit(ZYDIS_MNEMONIC_CLD); // Host ABI requires DF=0; guest flags are saved above.
        code.Emit(ZYDIS_MNEMONIC_MOV,{rax,Imm(reinterpret_cast<std::uintptr_t>(&Execute))});code.Emit(ZYDIS_MNEMONIC_CALL,{rax});
        for(unsigned reg=0;reg<vector_count;++reg)code.Emit(features.avx?ZYDIS_MNEMONIC_VMOVDQU:ZYDIS_MNEMONIC_MOVDQU,
            {vector_register(reg),Mem(ZYDIS_REGISTER_RSP,32+reg*vector_bytes,vector_bytes)},features.avx?ZYDIS_ENCODABLE_ENCODING_VEX:ZYDIS_ENCODABLE_ENCODING_LEGACY);
        code.Emit(ZYDIS_MNEMONIC_MOV,{rsp,rbx});
        for(unsigned reg=0;reg<16;++reg){if(reg==4)code.Emit(ZYDIS_MNEMONIC_LEA,{rsp,Mem(ZYDIS_REGISTER_RSP,8,8)});else code.Emit(ZYDIS_MNEMONIC_POP,{Reg(Gpr(reg))});}
        code.Emit(ZYDIS_MNEMONIC_POPFQ);code.Emit(ZYDIS_MNEMONIC_LEA,{rsp,Mem(ZYDIS_REGISTER_RSP,128,8)});
        code.Emit(ZYDIS_MNEMONIC_MOV,{rsp,Mem(ZYDIS_REGISTER_RSP,-232,8)});
        code.Emit(ZYDIS_MNEMONIC_JMP,{Mem(ZYDIS_REGISTER_RIP,0,8)});code.Data64(instruction->next);
        DWORD previous{};
        if(!VirtualProtect(memory,page_size,PAGE_EXECUTE_READ,&previous) ||
           !FlushInstructionCache(GetCurrentProcess(),memory,code.Size()))throw std::runtime_error("Cannot publish CPU adapter");
        return memory;
    }
};

Image::Image(Features features,bool force):implementation_(std::make_unique<Implementation>(features,force)){}
Image::~Image()=default;
const Statistics& Image::Stats()const{return implementation_->statistics;}
bool Image::Needed()const{return implementation_->force || !implementation_->features.bmi1 || !implementation_->features.lzcnt || !implementation_->features.movbe;}
void Image::Prepare(std::span<std::uint8_t> code) {
    auto& state=*implementation_;
    if(!Needed())return;
    ZydisDecoder decoder{};
    if(!ZYAN_SUCCESS(ZydisDecoderInit(&decoder,ZYDIS_MACHINE_MODE_LONG_64,ZYDIS_STACK_WIDTH_64)))throw std::runtime_error("Cannot initialize CPU decoder");
    for(std::size_t offset=0;offset<code.size();) {
        ZydisDecodedInstruction decoded{};ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
        if(!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder,code.data()+offset,code.size()-offset,&decoded,operands))){++offset;continue;}
        ++state.statistics.decoded;
        auto instruction=std::make_unique<Instruction>();
        const auto address=reinterpret_cast<std::uintptr_t>(code.data()+offset);
        if(Decode(decoded,operands,address,*instruction) && state.Needed(instruction->operation)) {
            if(decoded.length>=5) {
                auto* adapter=state.Adapter(instruction.get());
                const auto distance=static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(adapter))-static_cast<std::int64_t>(address+5);
                if(distance<std::numeric_limits<std::int32_t>::min() || distance>std::numeric_limits<std::int32_t>::max())throw std::runtime_error("CPU adapter jump is out of range");
                const auto relative=static_cast<std::int32_t>(distance);
                code[offset]=0xe9;std::memcpy(code.data()+offset+1,&relative,4);
                std::fill(code.begin()+static_cast<std::ptrdiff_t>(offset+5),code.begin()+static_cast<std::ptrdiff_t>(offset+decoded.length),0x90);
                ++state.statistics.trampolines;
            } else {
                // Four-byte register LZCNT cannot fit a near jump without
                // consuming another instruction/branch target. Keep it bounded.
                state.traps.emplace(address,instruction.get());code[offset]=0xcc;++state.statistics.short_traps;
            }
            ++state.statistics.converted[static_cast<std::size_t>(instruction->operation)];
            state.instructions.push_back(std::move(instruction));
        }
        offset+=decoded.length;
    }
}
bool Image::HandleException(EXCEPTION_POINTERS* exception)const {
    if(exception->ExceptionRecord->ExceptionCode!=EXCEPTION_BREAKPOINT)return false;
    const auto found=implementation_->traps.find(reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress));
    if(found==implementation_->traps.end())return false;
    auto& context=*exception->ContextRecord;const auto& instruction=*found->second;
    Frame frame{};for(unsigned reg=0;reg<16;++reg)frame.registers[reg]=ContextRegister(context,reg);
    frame.flags=context.EFlags;
    std::uint64_t value{};
    if(instruction.store)value=frame.registers[instruction.destination];
    else if(instruction.source.memory) {
        SIZE_T read{};
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(Address(frame,instruction)),&value,instruction.bits/8,&read) || read!=instruction.bits/8)return false;
    } else value=frame.registers[static_cast<std::size_t>(instruction.source.reg)];
    const auto result=Evaluate(instruction.operation,instruction.bits,value,frame.registers[instruction.control],frame.flags);
    if(instruction.store) {
        if(!Store(Address(frame,instruction),result,instruction.bits/8))return false;
        context.Rip=instruction.next;return true;
    }
    auto& destination=ContextRegister(context,instruction.destination);
    destination=instruction.bits==16?(destination&~0xffffull)|result:result;
    context.EFlags=static_cast<DWORD>(frame.flags);context.Rip=instruction.next;
    return true;
}
}
