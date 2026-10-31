#include "cpu/compatibility.h"
#include "cpu/assembler.h"
#include "cpu/function_ranges.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <array>
#include <atomic>
#include <thread>
#include <string_view>
#include <string>
#include <fstream>
#include <iterator>
#include <map>

using namespace cave::ps4::cpu;
namespace {
unsigned checks{};
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
struct Memory {
    std::uint8_t* data=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Memory(){if(!data)throw std::runtime_error("Fixture allocation failed");}
    ~Memory(){VirtualFree(data,0,MEM_RELEASE);}
    void Executable(){DWORD previous{};Check(VirtualProtect(data,4096,PAGE_EXECUTE_READ,&previous)!=0,"Fixture RX");FlushInstructionCache(GetCurrentProcess(),data,4096);}
};
const Image* trap_owner{};
void* tracked_page{};unsigned tracked_writes{};
LONG CALLBACK TrackedWrite(EXCEPTION_POINTERS* exception) {
    const auto& record=*exception->ExceptionRecord;
    const auto target=record.NumberParameters>=2?record.ExceptionInformation[1]:0;
    const auto begin=reinterpret_cast<std::uintptr_t>(tracked_page);
    if(tracked_page && record.ExceptionCode==EXCEPTION_ACCESS_VIOLATION && record.NumberParameters>=2 &&
       record.ExceptionInformation[0]==1 && target>=begin && target-begin<4096) {
        DWORD previous{};if(VirtualProtect(tracked_page,4096,PAGE_READWRITE,&previous)){++tracked_writes;return EXCEPTION_CONTINUE_EXECUTION;}
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
LONG CALLBACK Trap(EXCEPTION_POINTERS* exception){return trap_owner && trap_owner->HandleException(exception)?EXCEPTION_CONTINUE_EXECUTION:EXCEPTION_CONTINUE_SEARCH;}
ZydisMnemonic Mnemonic(Operation operation) {
    constexpr ZydisMnemonic names[]={ZYDIS_MNEMONIC_BEXTR,ZYDIS_MNEMONIC_ANDN,ZYDIS_MNEMONIC_BLSI,ZYDIS_MNEMONIC_BLSMSK,ZYDIS_MNEMONIC_BLSR,ZYDIS_MNEMONIC_LZCNT,ZYDIS_MNEMONIC_TZCNT};
    return names[static_cast<unsigned>(operation)];
}
std::uint64_t Expected(Operation operation,unsigned bits,std::uint64_t input,std::uint64_t control) {
    if(bits==32)input=static_cast<std::uint32_t>(input);
    std::uint64_t result{};
    switch(operation) {
    case Operation::Bextr:
        // Bit-by-bit oracle avoids the shift/mask implementation under test.
        for(unsigned bit=0;bit<((control>>8)&255);++bit) {
            const auto position=(control&255)+bit;
            if(position<bits && (input&(1ull<<position)))result|=1ull<<bit;
        }break;
    case Operation::Andn:result=input&~control;break;
    case Operation::Blsi:if(input)result=1ull<<std::countr_zero(input);break;
    case Operation::Blsmsk:result=input?((input^(input-1))):~0ull;break;
    case Operation::Blsr:result=input?input-(1ull<<std::countr_zero(input)):0;break;
    case Operation::Lzcnt:result=bits==32?std::countl_zero(static_cast<std::uint32_t>(input)):std::countl_zero(input);break;
    case Operation::Tzcnt:result=bits==32?std::countr_zero(static_cast<std::uint32_t>(input)):std::countr_zero(input);break;
    default:break;
    }
    return bits==32?static_cast<std::uint32_t>(result):result;
}
void Semantics() {
    constexpr std::uint64_t values[]={0,1,2,0x80000000ull,0x8000000000000000ull,~0ull,0xabcdef1234567890ull};
    for(unsigned bits:{32u,64u})for(unsigned kind=0;kind<7;++kind)for(auto input:values) {
        const auto operation=static_cast<Operation>(kind);
        for(unsigned start:{0u,1u,31u,32u,63u,64u,255u})for(unsigned length:{0u,1u,31u,32u,63u,64u,255u}) {
            const auto control=(length<<8)|start;std::uint64_t flags=0x202;
            const auto result=Evaluate(operation,bits,input,control,flags);
            Check(result==Expected(operation,bits,input,control),"Integer instruction semantics");
            Check((flags&0x202)==0x202,"Control flags preserved");
            if(operation==Operation::Bextr || operation==Operation::Andn)Check(!(flags&0x801),"BEXTR/ANDN clears CF/OF");
        }
    }
}
void ExecutedAdapters() {
    constexpr std::uint64_t values[]={0,1,0x80000000ull,~0ull,0xabcdef1234567890ull};
    for(unsigned bits:{32u,64u})for(unsigned kind=0;kind<7;++kind)for(unsigned source_mode:{0u,1u,2u})for(unsigned destination:{0u,1u,2u,3u,7u,10u,12u}) {
        const auto operation=static_cast<Operation>(kind);Memory fixture;Assembler code(fixture.data,4096);
        // Windows ABI: RCX=input or pointer, RDX=control. R8=flag destination.
        const bool nonvolatile=destination==3 || destination==7 || destination==12;
        if(nonvolatile)code.Emit(ZYDIS_MNEMONIC_PUSH,{Reg(Gpr(destination))});
        const auto dst=Reg(static_cast<ZydisRegister>((bits==32?ZYDIS_REGISTER_EAX:ZYDIS_REGISTER_RAX)+destination));
        const auto input=source_mode==1?Mem(ZYDIS_REGISTER_RCX,0,bits/8):source_mode==2?Mem(ZYDIS_REGISTER_RSP,-64,bits/8):Reg(bits==32?ZYDIS_REGISTER_ECX:ZYDIS_REGISTER_RCX);
        if(source_mode==2)code.Emit(ZYDIS_MNEMONIC_MOV,{Mem(ZYDIS_REGISTER_RSP,-64,8),Reg(ZYDIS_REGISTER_RCX)});
        const auto control=Reg(bits==32?ZYDIS_REGISTER_EDX:ZYDIS_REGISTER_RDX);
        // Protect both ends of the guest's SysV red zone.
        code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_R10),Imm(0x1234567812345678ull)});
        code.Emit(ZYDIS_MNEMONIC_MOV,{Mem(ZYDIS_REGISTER_RSP,-8,8),Reg(ZYDIS_REGISTER_R10)});
        code.Emit(ZYDIS_MNEMONIC_MOV,{Mem(ZYDIS_REGISTER_RSP,-128,8),Reg(ZYDIS_REGISTER_R10)});
        if(operation==Operation::Bextr)code.Emit(Mnemonic(operation),{dst,input,control},ZYDIS_ENCODABLE_ENCODING_VEX);
        else if(operation==Operation::Andn)code.Emit(Mnemonic(operation),{dst,control,input},ZYDIS_ENCODABLE_ENCODING_VEX);
        else code.Emit(Mnemonic(operation),{dst,input},(operation==Operation::Lzcnt || operation==Operation::Tzcnt)?ZYDIS_ENCODABLE_ENCODING_LEGACY:ZYDIS_ENCODABLE_ENCODING_VEX);
        code.Emit(ZYDIS_MNEMONIC_PUSHFQ);code.Emit(ZYDIS_MNEMONIC_POP,{Reg(ZYDIS_REGISTER_R11)});
        code.Emit(ZYDIS_MNEMONIC_MOV,{Mem(ZYDIS_REGISTER_R8,0,8),Reg(ZYDIS_REGISTER_R11)});
        code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_R11),Mem(ZYDIS_REGISTER_RSP,-128,8)});
        code.Emit(ZYDIS_MNEMONIC_MOV,{Mem(ZYDIS_REGISTER_R8,8,8),Reg(ZYDIS_REGISTER_R11)});
        if(destination)code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_RAX),Reg(Gpr(destination))});
        if(nonvolatile)code.Emit(ZYDIS_MNEMONIC_POP,{Reg(Gpr(destination))});
        code.Emit(ZYDIS_MNEMONIC_RET);
        using Function=std::uint64_t(*)(std::uint64_t,std::uint64_t,std::uint64_t*);
        const auto function=reinterpret_cast<Function>(fixture.data);
        const auto hardware=DetectFeatures();
        const bool native=operation==Operation::Lzcnt?hardware.lzcnt:hardware.bmi1;
        std::vector<std::array<std::uint64_t,2>> reference;
        if(native) {
            fixture.Executable();
            for(auto value:values)for(unsigned start:{0u,8u,32u,64u,255u})for(unsigned length:{0u,4u,32u,64u,255u}) {
                std::uint64_t output[2]{};const auto parameter=source_mode==1?reinterpret_cast<std::uint64_t>(&value):value;
                const auto actual=function(parameter,(length<<8)|start,output);
                reference.push_back({actual,output[0]});
            }
            DWORD protection{};Check(VirtualProtect(fixture.data,4096,PAGE_READWRITE,&protection)!=0,"Fixture writable for conversion");
        }
        Image patched(Features{},true);patched.Prepare({fixture.data,code.Size()});fixture.Executable();
        Check(patched.Stats().trampolines+patched.Stats().short_traps==1,"Exactly one authored site patched");
        trap_owner=&patched;
        std::size_t case_index{};
        for(auto value:values)for(unsigned start:{0u,8u,32u,64u,255u})for(unsigned length:{0u,4u,32u,64u,255u}) {
            std::uint64_t output[2]{};const auto parameter=source_mode==1?reinterpret_cast<std::uint64_t>(&value):value;
            const auto requested=(length<<8)|start;
            const auto actual=function(parameter,requested,output);
            Check(actual==Expected(operation,bits,value,requested),"Executed adapter result");
            Check(output[1]==0x1234567812345678ull,"Guest red zone preserved");
            Check((output[0]&0x800)==0,"Overflow cleared");
            if(native) {
                const auto flag_mask=operation==Operation::Bextr?0x841u:(operation==Operation::Lzcnt || operation==Operation::Tzcnt)?0x41u:0x8c1u;
                Check(actual==reference[case_index][0],"Native CPU result differential");
                Check((output[0]&flag_mask)==(reference[case_index][1]&flag_mask),"Native CPU defined flags differential");
            }
            ++case_index;
        }
        if(bits==64 && kind==0 && source_mode==0 && destination==0) {
            std::atomic<unsigned> failures{};std::vector<std::jthread> workers;
            for(unsigned thread=0;thread<6;++thread)workers.emplace_back([&,thread]{
                for(unsigned i=0;i<10000;++i) {
                    std::uint64_t output[2]{};const auto value=0xabcdef1234567890ull+i+thread;
                    if(function(value,0x408,output)!=((value>>8)&15) || output[1]!=0x1234567812345678ull)++failures;
                }
            });
            workers.clear();Check(failures==0,"Concurrent shared adapter execution");
        }
        trap_owner=nullptr;
    }
}
void FailedReadPreservesContext() {
    Memory fixture;Assembler code(fixture.data,4096);
    code.Emit(ZYDIS_MNEMONIC_LZCNT,{Reg(ZYDIS_REGISTER_EAX),Mem(ZYDIS_REGISTER_RCX,0,4)});
    code.Emit(ZYDIS_MNEMONIC_RET);Image image(Features{},true);image.Prepare({fixture.data,code.Size()});
    Check(image.Stats().short_traps==1,"Short memory instruction uses bounded handler");
    CONTEXT context{};context.Rax=0x12345678;context.Rcx=0;context.Rip=reinterpret_cast<DWORD64>(fixture.data)+1;context.EFlags=0x202;
    const auto before=context;EXCEPTION_RECORD record{};record.ExceptionCode=EXCEPTION_BREAKPOINT;record.ExceptionAddress=fixture.data;
    EXCEPTION_POINTERS exception{&record,&context};
    Check(!image.HandleException(&exception),"Invalid memory is not converted into a successful result");
    Check(std::memcmp(&before,&context,sizeof(context))==0,"Failed read preserves guest context");
}
void FeatureSelection() {
    for(bool bmi1:{false,true})for(bool lzcnt:{false,true}) {
        Memory fixture;Assembler code(fixture.data,4096);
        code.Emit(ZYDIS_MNEMONIC_BEXTR,{Reg(ZYDIS_REGISTER_RAX),Reg(ZYDIS_REGISTER_RCX),Reg(ZYDIS_REGISTER_RDX)},ZYDIS_ENCODABLE_ENCODING_VEX);
        code.Emit(ZYDIS_MNEMONIC_LZCNT,{Reg(ZYDIS_REGISTER_RAX),Reg(ZYDIS_REGISTER_RCX)});
        code.Emit(ZYDIS_MNEMONIC_RET);Features features{};features.bmi1=bmi1;features.lzcnt=lzcnt;
        Image image(features,false);image.Prepare({fixture.data,code.Size()});
        Check(image.Stats().converted[0]==std::size_t(!bmi1),"BMI1 capability gate");
        Check(image.Stats().converted[5]==std::size_t(!lzcnt),"LZCNT capability gate");
    }
}
void NativePassThrough() {
    Memory fixture;Assembler code(fixture.data,4096);
    code.Emit(ZYDIS_MNEMONIC_BEXTR,{Reg(ZYDIS_REGISTER_RAX),Reg(ZYDIS_REGISTER_RCX),Reg(ZYDIS_REGISTER_RDX)},ZYDIS_ENCODABLE_ENCODING_VEX);
    code.Emit(ZYDIS_MNEMONIC_RET);std::vector<std::uint8_t> before(fixture.data,fixture.data+code.Size());
    Features available{};available.bmi1=available.lzcnt=available.movbe=true;Image image(available,false);image.Prepare({fixture.data,code.Size()});
    Check(image.Stats().trampolines==0 && image.Stats().short_traps==0,"Available CPU path is unchanged");
    Check(std::memcmp(before.data(),fixture.data,before.size())==0,"Native bytes unchanged");
}
void FunctionMetadata() {
    // An authored .eh_frame_hdr/CIE/FDE pair. The second BEXTR byte sequence
    // represents embedded game data, outside the compiler's function extent.
    Memory fixture;std::span<std::uint8_t> bytes(fixture.data,4096);
    const auto put=[&](std::size_t at,std::uint32_t value){std::memcpy(bytes.data()+at,&value,4);};
    bytes[0x100]=1;bytes[0x101]=0x1b;bytes[0x102]=3;bytes[0x103]=0x3b;
    put(0x104,0xfc);put(0x108,1);put(0x10c,0x200);put(0x110,0x120);
    put(0x200,16);put(0x204,0);
    const std::uint8_t cie[]={1,'z','R',0,1,0x78,16,1,0x1b};
    std::memcpy(bytes.data()+0x208,cie,sizeof(cie));
    put(0x220,16);put(0x224,0x24);put(0x228,0xd8);put(0x22c,6);
    const std::uint8_t instruction[]={0xc4,0xe2,0x78,0xf7,0xc1,0xc3};
    std::memcpy(bytes.data()+0x300,instruction,sizeof(instruction));
    std::memcpy(bytes.data()+0x400,instruction,sizeof(instruction));
    const auto ranges=FunctionRanges(bytes,0x100,20);
    Check(ranges.size()==1 && ranges[0].address==0x300 && ranges[0].size==6,"Function metadata exact extent");
    Image image(Features{},true);
    for(const auto& range:ranges)image.Prepare(bytes.subspan(static_cast<std::size_t>(range.address),static_cast<std::size_t>(range.size)));
    Check(image.Stats().trampolines==1,"Only function instruction converted");
    Check(std::memcmp(bytes.data()+0x400,instruction,sizeof(instruction))==0,"Embedded ROM bytes untouched");
    const auto invalid=[&]{try{(void)FunctionRanges(bytes,0x100,20);}catch(const std::runtime_error&){return true;}return false;};
    put(0x108,2);Check(invalid(),"Truncated function table rejected");put(0x108,1);
    put(0x10c,0x201);Check(invalid(),"FDE/table mismatch rejected");put(0x10c,0x200);
    put(0x224,0xffff);Check(invalid(),"Out-of-bounds CIE rejected");put(0x224,0x24);
    put(0x22c,4096);Check(invalid(),"Out-of-image function rejected");put(0x22c,6);
    bytes[0x210]=0xff;Check(invalid(),"Unsupported FDE encoding rejected");
}
void WordCounts() {
    constexpr std::uint64_t initial=0xabcdef1234567890ull;
    for(auto operation:{Operation::Lzcnt,Operation::Tzcnt})for(bool memory:{false,true}) {
        Memory fixture;Assembler code(fixture.data,4096);
        code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_RAX),Imm(initial)});
        code.Emit(Mnemonic(operation),{Reg(ZYDIS_REGISTER_AX),memory?Mem(ZYDIS_REGISTER_RCX,0,2):Reg(ZYDIS_REGISTER_CX)});
        code.Emit(ZYDIS_MNEMONIC_RET);
        Image image(Features{},true);image.Prepare({fixture.data,code.Size()});fixture.Executable();trap_owner=&image;
        using Function=std::uint64_t(*)(std::uint64_t);
        for(std::uint64_t source:{0ull,1ull,2ull,0x8000ull,0xffffull,0x10000ull}) {
            const auto word=static_cast<std::uint16_t>(source);
            const auto expected=operation==Operation::Lzcnt?std::countl_zero(word):std::countr_zero(word);
            const auto actual=reinterpret_cast<Function>(fixture.data)(memory?reinterpret_cast<std::uintptr_t>(&word):source);
            Check(actual==((initial&~0xffffull)|static_cast<unsigned>(expected)),"16-bit count preserves upper destination");
        }
        trap_owner=nullptr;
    }
}
void ByteSwaps() {
    for(unsigned bits:{16u,32u,64u})for(bool store:{false,true})for(unsigned displacement:{0u,16u}) {
        Memory fixture;Assembler code(fixture.data,4096);
        code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_RAX),Imm(0xabcdef1234567890ull)});
        code.Emit(ZYDIS_MNEMONIC_PUSH,{Imm(0x243)});code.Emit(ZYDIS_MNEMONIC_POPFQ);
        const auto reg=Reg(bits==16?ZYDIS_REGISTER_AX:bits==32?ZYDIS_REGISTER_EAX:ZYDIS_REGISTER_RAX);
        if(store)code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_RAX),Reg(ZYDIS_REGISTER_RDX)});
        const auto memory=Mem(ZYDIS_REGISTER_RCX,displacement,bits/8);
        if(store)code.Emit(ZYDIS_MNEMONIC_MOVBE,{memory,reg});else code.Emit(ZYDIS_MNEMONIC_MOVBE,{reg,memory});
        code.Emit(ZYDIS_MNEMONIC_PUSHFQ);code.Emit(ZYDIS_MNEMONIC_POP,{Reg(ZYDIS_REGISTER_R11)});
        code.Emit(ZYDIS_MNEMONIC_MOV,{Mem(ZYDIS_REGISTER_R8,0,8),Reg(ZYDIS_REGISTER_R11)});
        code.Emit(ZYDIS_MNEMONIC_RET);
        Image image(Features{},true);image.Prepare({fixture.data,code.Size()});fixture.Executable();trap_owner=&image;
        Check(image.Stats().converted[static_cast<unsigned>(Operation::Movbe)]==1,"MOVBE site converted");
        using Function=std::uint64_t(*)(void*,std::uint64_t,std::uint64_t*);
        for(const std::uint64_t source:{0ull,1ull,0x123456789abcdef0ull,~0ull}) {
            std::array<std::uint8_t,32> buffer;buffer.fill(0xaa);
            if(!store)std::memcpy(buffer.data()+displacement,&source,bits/8);
            std::uint64_t flags{};const auto actual=reinterpret_cast<Function>(fixture.data)(buffer.data(),source,&flags);
            std::uint64_t swapped{};for(unsigned i=0;i<bits/8;++i)swapped=(swapped<<8)|buffer[displacement+i];
            if(store)Check(swapped==(bits==64?source:source&((1ull<<bits)-1)),"MOVBE store byte order");
            else Check(actual==(bits==16?(0xabcdef1234567890ull&~0xffffull)|swapped:swapped),"MOVBE load and destination width");
            Check((flags&0x8d5)==(0x243&0x8d5),"MOVBE preserves flags");
            for(unsigned i=0;i<buffer.size();++i)if(i<displacement || i>=displacement+bits/8)Check(buffer[i]==0xaa,"MOVBE bounded memory write");
        }
        trap_owner=nullptr;
    }
    for(bool supported:{false,true}) {
        Memory fixture;Assembler code(fixture.data,4096);code.Emit(ZYDIS_MNEMONIC_MOVBE,{Reg(ZYDIS_REGISTER_EAX),Mem(ZYDIS_REGISTER_RCX,0,4)});
        Features features{};features.bmi1=features.lzcnt=true;features.movbe=supported;
        Image image(features,false);image.Prepare({fixture.data,code.Size()});
        Check(image.Stats().converted[static_cast<unsigned>(Operation::Movbe)]==std::size_t(!supported),"MOVBE feature gate");
    }
}
void VectorPreservation() {
    const auto features=DetectFeatures();if(!features.avx)return;
    Memory fixture;Assembler code(fixture.data,4096);
    code.Emit(ZYDIS_MNEMONIC_MOVDQU,{Mem(ZYDIS_REGISTER_RSP,-32,16),Reg(ZYDIS_REGISTER_XMM15)});
    for(unsigned reg:{0u,5u,15u})code.Emit(ZYDIS_MNEMONIC_VMOVDQU,
        {Reg(static_cast<ZydisRegister>(ZYDIS_REGISTER_YMM0+reg)),Mem(ZYDIS_REGISTER_R8,0,32)},ZYDIS_ENCODABLE_ENCODING_VEX);
    code.Emit(ZYDIS_MNEMONIC_STD);
    code.Emit(ZYDIS_MNEMONIC_BEXTR,{Reg(ZYDIS_REGISTER_EAX),Mem(ZYDIS_REGISTER_RCX,0,4),Reg(ZYDIS_REGISTER_EDX)},ZYDIS_ENCODABLE_ENCODING_VEX);
    code.Emit(ZYDIS_MNEMONIC_PUSHFQ);code.Emit(ZYDIS_MNEMONIC_POP,{Reg(ZYDIS_REGISTER_R11)});code.Emit(ZYDIS_MNEMONIC_CLD);
    code.Emit(ZYDIS_MNEMONIC_MOV,{Mem(ZYDIS_REGISTER_R9,96,8),Reg(ZYDIS_REGISTER_R11)});
    unsigned offset{};for(unsigned reg:{0u,5u,15u}) {
        code.Emit(ZYDIS_MNEMONIC_VMOVDQU,{Mem(ZYDIS_REGISTER_R9,offset,32),Reg(static_cast<ZydisRegister>(ZYDIS_REGISTER_YMM0+reg))},ZYDIS_ENCODABLE_ENCODING_VEX);offset+=32;
    }
    code.Emit(ZYDIS_MNEMONIC_MOVDQU,{Reg(ZYDIS_REGISTER_XMM15),Mem(ZYDIS_REGISTER_RSP,-32,16)});
    code.Emit(ZYDIS_MNEMONIC_VZEROUPPER,{},ZYDIS_ENCODABLE_ENCODING_VEX);code.Emit(ZYDIS_MNEMONIC_RET);
    Image image(features,true);image.Prepare({fixture.data,code.Size()});fixture.Executable();
    std::array<std::uint8_t,32> pattern;for(unsigned i=0;i<pattern.size();++i)pattern[i]=static_cast<std::uint8_t>(i*7+1);
    std::array<std::uint8_t,104> output{};const std::uint32_t input=0x12345678;
    using Function=std::uint64_t(*)(const void*,std::uint64_t,const void*,void*);
    Check(reinterpret_cast<Function>(fixture.data)(&input,0x408,pattern.data(),output.data())==6,"AVX adapter integer result");
    for(unsigned i=0;i<96;++i)Check(output[i]==pattern[i%32],"Guest YMM state preserved across host helper");
    std::uint64_t flags{};std::memcpy(&flags,output.data()+96,8);Check((flags&0x400)!=0,"Guest direction flag preserved");
}
void ReportedBextrContract() {
    Memory fixture;Assembler code(fixture.data,4096);
    code.Emit(ZYDIS_MNEMONIC_PUSH,{Reg(ZYDIS_REGISTER_RDI)});
    code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_RDI),Reg(ZYDIS_REGISTER_RCX)});
    code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_RAX),Reg(ZYDIS_REGISTER_RDX)});
    const auto site=code.Size();
    code.Emit(ZYDIS_MNEMONIC_BEXTR,{Reg(ZYDIS_REGISTER_ECX),Mem(ZYDIS_REGISTER_RDI,0,4),Reg(ZYDIS_REGISTER_EAX)},ZYDIS_ENCODABLE_ENCODING_VEX);
    const std::uint8_t expected[]={0xc4,0xe2,0x78,0xf7,0x0f};
    Check(code.Size()-site==sizeof(expected) && std::memcmp(fixture.data+site,expected,sizeof(expected))==0,"Issue 6 instruction encoding");
    code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_RAX),Reg(ZYDIS_REGISTER_RCX)});
    code.Emit(ZYDIS_MNEMONIC_POP,{Reg(ZYDIS_REGISTER_RDI)});code.Emit(ZYDIS_MNEMONIC_RET);
    Image image(Features{},false);image.Prepare({fixture.data,code.Size()});fixture.Executable();
    using Function=std::uint64_t(*)(const void*,std::uint64_t);
    const std::uint32_t source=0x12345678;
    Check(reinterpret_cast<Function>(fixture.data)(&source,0x408)==6,"Issue 6 compatibility execution");
}
void TrackedStore() {
    Memory fixture,target;Assembler code(fixture.data,4096);
    code.Emit(ZYDIS_MNEMONIC_MOV,{Reg(ZYDIS_REGISTER_EAX),Reg(ZYDIS_REGISTER_EDX)});
    const auto site=code.Size();code.Emit(ZYDIS_MNEMONIC_MOVBE,{Mem(ZYDIS_REGISTER_RCX,0,4),Reg(ZYDIS_REGISTER_EAX)});
    code.Emit(ZYDIS_MNEMONIC_RET);Image image(Features{},true);image.Prepare({fixture.data,code.Size()});fixture.Executable();trap_owner=&image;
    Check(image.Stats().short_traps==1,"Tracked store uses short-instruction path");
    DWORD previous{};Check(VirtualProtect(target.data,4096,PAGE_READONLY,&previous)!=0,"Track fixture write protection");
    tracked_page=target.data;tracked_writes=0;auto* tracker=AddVectoredExceptionHandler(1,TrackedWrite);
    Check(tracker!=nullptr,"Install write tracking fixture");
    using Function=void(*)(void*,std::uint64_t);
    reinterpret_cast<Function>(fixture.data)(target.data,0x12345678);
    RemoveVectoredExceptionHandler(tracker);tracked_page=nullptr;
    std::uint32_t result{};std::memcpy(&result,target.data,4);
    Check(tracked_writes==1 && result==0x78563412,"MOVBE store preserves write-fault tracking");
    CONTEXT context{};context.Rcx=0;context.Rax=0x12345678;context.Rip=reinterpret_cast<DWORD64>(fixture.data+site)+1;context.EFlags=0x202;
    const auto before=context;EXCEPTION_RECORD record{};record.ExceptionCode=EXCEPTION_BREAKPOINT;record.ExceptionAddress=fixture.data+site;
    EXCEPTION_POINTERS exception{&record,&context};
    Check(!image.HandleException(&exception) && std::memcmp(&context,&before,sizeof(context))==0,"Invalid store leaves guest context unchanged");
    trap_owner=nullptr;
}
void Bench() {
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    const auto features=DetectFeatures();
    for(auto operation:{Operation::Bextr,Operation::Lzcnt})for(unsigned bits:{32u,64u}) {
        Memory fixture;Assembler code(fixture.data,4096);
        const auto destination=Reg(bits==32?ZYDIS_REGISTER_EAX:ZYDIS_REGISTER_RAX);
        const auto source=Reg(bits==32?ZYDIS_REGISTER_ECX:ZYDIS_REGISTER_RCX);
        if(operation==Operation::Bextr)code.Emit(Mnemonic(operation),{destination,source,Reg(bits==32?ZYDIS_REGISTER_EDX:ZYDIS_REGISTER_RDX)},ZYDIS_ENCODABLE_ENCODING_VEX);
        else code.Emit(Mnemonic(operation),{destination,source});
        code.Emit(ZYDIS_MNEMONIC_RET);
        using Function=std::uint64_t(*)(std::uint64_t,std::uint64_t);
        const auto function=reinterpret_cast<Function>(fixture.data);
        const auto measure=[&](unsigned iterations) {
            LARGE_INTEGER begin{},end{};std::uint64_t sum{};QueryPerformanceCounter(&begin);
            for(unsigned i=0;i<iterations;++i)sum+=function(0xabcdef1234567890ull+i,0x408);
            QueryPerformanceCounter(&end);
            return std::pair<double,std::uint64_t>{double(end.QuadPart-begin.QuadPart)*1e9/double(frequency.QuadPart)/iterations,sum};
        };
        fixture.Executable();const bool native=operation==Operation::Bextr?features.bmi1:features.lzcnt;
        double native_ns{};if(native)native_ns=measure(250000).first;
        DWORD previous{};Check(VirtualProtect(fixture.data,4096,PAGE_READWRITE,&previous)!=0,"Benchmark writable");
        Image compatibility(features,true);compatibility.Prepare({fixture.data,code.Size()});fixture.Executable();trap_owner=&compatibility;
        const auto converted=measure(compatibility.Stats().short_traps?10000:250000);
        std::printf("{\"operation\":\"%s\",\"bits\":%u,\"native_ns\":%.3f,\"compatible_ns\":%.3f,\"short_traps\":%zu,\"checksum\":%llu}\n",
            operation==Operation::Bextr?"bextr":"lzcnt",bits,native_ns,converted.first,compatibility.Stats().short_traps,converted.second);
        trap_owner=nullptr;
    }
}
void Audit(const char* path,bool functions=false,std::uint64_t header=0,std::uint64_t header_size=0) {
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Missing code fixture");
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    if(bytes.size()>512ull*1024*1024)throw std::runtime_error("Code fixture too large");
    ZydisDecoder decoder{};ZydisDecoderInit(&decoder,ZYDIS_MACHINE_MODE_LONG_64,ZYDIS_STACK_WIDTH_64);
    std::map<std::string,std::size_t> counts;
    const auto ranges=functions?FunctionRanges(bytes,header,header_size):std::vector<CodeRange>{{0,bytes.size()}};
    if(functions)counts["function_ranges"]=ranges.size();
    for(const auto& range:ranges)for(std::size_t offset=static_cast<std::size_t>(range.address),end=offset+static_cast<std::size_t>(range.size);offset<end;) {
        ZydisDecodedInstruction instruction{};ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
        if(!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder,bytes.data()+offset,end-offset,&instruction,operands))){++offset;continue;}
        ++counts[ZydisISASetGetString(instruction.meta.isa_set)];offset+=instruction.length;
        if(functions && (instruction.meta.isa_set==ZYDIS_ISA_SET_SSE4A || instruction.meta.isa_set==ZYDIS_ISA_SET_MOVBE)) {
            ZydisFormatter formatter{};ZydisFormatterInit(&formatter,ZYDIS_FORMATTER_STYLE_INTEL);char text[256]{};
            ZydisFormatterFormatInstruction(&formatter,&instruction,operands,instruction.operand_count_visible,text,sizeof(text),offset-instruction.length,nullptr);
            std::fprintf(stderr,"function 0x%llx+0x%llx, instruction 0x%zx: %s\n",range.address,range.size,offset-instruction.length,text);
        }
    }
    std::putchar('{');bool first=true;
    for(const auto& [name,count]:counts){std::printf("%s\"%s\":%zu",first?"":",",name.c_str(),count);first=false;}
    std::puts("}");
}
}
int main(int argc,char** argv) {
    auto* handler=AddVectoredExceptionHandler(1,Trap);
    try{Check(handler!=nullptr,"Install fixture handler");
        if(argc==2 && std::string_view(argv[1])=="--bench"){Bench();RemoveVectoredExceptionHandler(handler);return 0;}
        if(argc==3 && std::string_view(argv[1])=="--audit"){Audit(argv[2]);RemoveVectoredExceptionHandler(handler);return 0;}
        if(argc==5 && std::string_view(argv[1])=="--audit-functions"){Audit(argv[2],true,std::stoull(argv[3],nullptr,0),std::stoull(argv[4],nullptr,0));RemoveVectoredExceptionHandler(handler);return 0;}
        Semantics();NativePassThrough();FeatureSelection();FailedReadPreservesContext();ExecutedAdapters();WordCounts();ByteSwaps();VectorPreservation();FunctionMetadata();ReportedBextrContract();TrackedStore();
        RemoveVectoredExceptionHandler(handler);std::printf("PS4 CPU compatibility: %u checks passed\n",checks);return 0;
    }catch(const std::exception& error){if(handler)RemoveVectoredExceptionHandler(handler);std::fprintf(stderr,"CPU compatibility: %s (check %u)\n",error.what(),checks);return 1;}
}
