#pragma once
#include "instructions.h"
#include "rtti.h"
#include <map>

namespace StarfieldHT::discovery {

struct NamedAnchor { const char* text; uint32_t instruction; uint8_t operand; };
struct MethodContract {
    const char* name;
    Fingerprint fingerprint;
    const char* owner;
    std::vector<NamedAnchor> names;
};
struct LinkContract {
    const char* source;
    uint32_t instruction;
    uint8_t operand;
    const char* destination;
};
struct DataContract {
    const char* source;
    uint32_t instruction;
    uint8_t operand;
    const char* destination;
    uint32_t size;
    uint32_t alignment;
    uint32_t required;
    uint32_t forbidden;
};
struct MemberContract {
    const char* source;
    uint32_t instruction;
    uint8_t operand;
    const char* destination;
    uint32_t width;
    uint32_t alignment;
    uint32_t containingSize;
    int32_t adjustment;
};
struct ContractSet {
    std::vector<MethodContract> methods;
    std::vector<LinkContract> links;
    std::vector<DataContract> data;
    std::vector<MemberContract> members;
    std::vector<LinkContract> tables;
};
struct ResolvedContracts {
    std::map<std::string, uint32_t> methods;
    std::map<std::string, uint32_t> data;
    std::map<std::string, uint32_t> members;
    std::map<std::string, uint32_t> tables;
    std::map<std::string, uint32_t> slots;
    std::map<std::string, uint32_t> returns;
    std::map<std::string, uint32_t> classes;
};
ResolvedContracts ResolveContracts(const Image& image, const ContractSet& contracts);

}
