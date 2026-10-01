#pragma once

#include "image.h"
#include <Zydis/Zydis.h>
#include <map>

namespace StarfieldHT::discovery {

struct Instruction {
    uint32_t rva;
    ZydisDecodedInstruction decoded;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    std::vector<uint32_t> indirectTargets;
    int imageRelativeMemory = -1;
    uint32_t RelativeTarget(size_t operand) const;
};

struct Reference {
    uint32_t owner;
    uint32_t instruction;
    uint32_t target;
    uint8_t operand;
};

struct Fingerprint {
    uint64_t shape = 14695981039346656037ull;
    uint64_t constants = 14695981039346656037ull;
    uint32_t instructions = 0;
    bool operator==(const Fingerprint& other) const {
        return shape == other.shape && constants == other.constants && instructions == other.instructions;
    }
};

struct OperandLocation {
    uint32_t instruction;
    uint8_t operand;
};

class Instructions {
public:
    explicit Instructions(const Image& image, const std::vector<uint32_t>& virtualEntries = {});
    std::vector<Instruction> Decode(uint32_t owner) const;
    Fingerprint Identify(const std::vector<Instruction>& body,
                         const std::vector<OperandLocation>& variableConstants = {}) const;
    std::vector<Reference> Referencing(uint32_t target) const;
    std::vector<Reference> Calling(uint32_t target) const;
    const std::map<uint32_t, Fingerprint>& Fingerprints() const { return fingerprints_; }
    const std::map<uint32_t, std::string>& Rejected() const { return rejected_; }
    const Image& image;

private:
    ZydisDecoder decoder_;
    std::map<uint32_t, std::vector<FunctionRange>> ranges_;
    std::map<uint32_t, Fingerprint> fingerprints_;
    std::map<uint32_t, std::string> rejected_;
    std::vector<Reference> references_;
    std::vector<Reference> calls_;
};

}
