#pragma once
#include <Zydis/Zydis.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace cave::ps4::cpu {
inline ZydisEncoderOperand Reg(ZydisRegister value) {
    ZydisEncoderOperand result{};result.type=ZYDIS_OPERAND_TYPE_REGISTER;result.reg.value=value;return result;
}
inline ZydisEncoderOperand Imm(std::uint64_t value) {
    ZydisEncoderOperand result{};result.type=ZYDIS_OPERAND_TYPE_IMMEDIATE;result.imm.u=value;return result;
}
inline ZydisEncoderOperand Mem(ZydisRegister base,std::int64_t displacement,unsigned size) {
    ZydisEncoderOperand result{};result.type=ZYDIS_OPERAND_TYPE_MEMORY;
    result.mem.base=base;result.mem.displacement=displacement;result.mem.size=static_cast<ZyanU16>(size);return result;
}
inline ZydisRegister Gpr(unsigned index){return static_cast<ZydisRegister>(ZYDIS_REGISTER_RAX+index);}
class Assembler {
public:
    Assembler(std::uint8_t* data,std::size_t capacity):data_(data),capacity_(capacity){}
    void Emit(ZydisMnemonic mnemonic,std::initializer_list<ZydisEncoderOperand> operands={},
              ZydisEncodableEncoding encodings=ZYDIS_ENCODABLE_ENCODING_LEGACY) {
        ZydisEncoderRequest request{};request.machine_mode=ZYDIS_MACHINE_MODE_LONG_64;
        request.mnemonic=mnemonic;request.allowed_encodings=encodings;
        request.operand_count=static_cast<ZyanU8>(operands.size());
        if(operands.size()>ZYDIS_ENCODER_MAX_OPERANDS)throw std::runtime_error("Too many adapter operands");
        std::size_t index{};for(const auto& operand:operands)request.operands[index++]=operand;
        auto size=capacity_-used_;
        if(!ZYAN_SUCCESS(ZydisEncoderEncodeInstruction(&request,data_+used_,&size)))throw std::runtime_error("Cannot encode CPU adapter instruction");
        used_+=size;
    }
    void Data64(std::uint64_t value) {
        if(sizeof(value)>capacity_-used_)throw std::runtime_error("CPU adapter is too large");
        std::memcpy(data_+used_,&value,sizeof(value));used_+=sizeof(value);
    }
    std::size_t Size()const{return used_;}
private:
    std::uint8_t* data_;std::size_t capacity_,used_{};
};
}
