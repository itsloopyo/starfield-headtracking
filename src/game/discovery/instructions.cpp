#include "instructions.h"

#include <algorithm>
#include <limits>
#include <set>

namespace StarfieldHT::discovery {
namespace {

void Hash(uint64_t& hash, uint64_t value) {
    for (size_t i = 0; i < sizeof(value); ++i) {
        hash ^= value & 0xff;
        hash *= 1099511628211ull;
        value >>= 8;
    }
}

std::vector<Reference> ReferencesAt(const std::vector<Reference>& references, uint32_t target) {
    const auto begin = std::lower_bound(references.begin(), references.end(), target,
        [](const auto& reference, uint32_t rva) { return reference.target < rva; });
    const auto end = std::upper_bound(begin, references.end(), target,
        [](uint32_t rva, const auto& reference) { return rva < reference.target; });
    return {begin, end};
}

}

uint32_t Instruction::RelativeTarget(size_t operand) const {
    Require(operand < decoded.operand_count_visible, "Invalid instruction operand");
    const auto& value = operands[operand];
    Require((value.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && value.imm.is_relative)
            || (value.type == ZYDIS_OPERAND_TYPE_MEMORY && value.mem.base == ZYDIS_REGISTER_RIP),
            "Operand is not image-relative");
    const int64_t displacement = value.type == ZYDIS_OPERAND_TYPE_MEMORY ? value.mem.disp.value : value.imm.value.s;
    const int64_t next = static_cast<int64_t>(rva) + decoded.length;
    Require(displacement >= -next && displacement <= std::numeric_limits<uint32_t>::max() - next,
            "Relative instruction target overflows");
    return static_cast<uint32_t>(next + displacement);
}

Instructions::Instructions(const Image& source, const std::vector<uint32_t>& virtualEntries) : image(source) {
    Require(ZYAN_SUCCESS(ZydisDecoderInit(&decoder_, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64)),
            "Could not initialize AMD64 decoder");
    for (const auto& range : image.functions) ranges_[range.owner].push_back(range);
    std::vector<uint32_t> pending;
    for (const auto& [owner, ranges] : ranges_) pending.push_back(owner);
    std::set<uint32_t> leafTargets;
    const auto addLeaf = [&](uint32_t target) {
        Require(image.Contains(target, 1, Image::Readable | Image::Executable, Image::Writable),
                "Leaf entry is not executable");
        if (image.FindOwner(target) || !leafTargets.insert(target).second) return;
        uint32_t end = target + 512;
        const auto following = std::lower_bound(image.functions.begin(), image.functions.end(), target,
            [](auto range, uint32_t address) { return range.begin < address; });
        if (following != image.functions.end()) end = std::min(end, following->begin);
        for (const auto& section : image.sections) {
            if (target >= section.begin && target < section.end) end = std::min(end, section.end);
        }
        ranges_.emplace(target, std::vector<FunctionRange>{{target, end, target}});
        pending.push_back(target);
    };
    for (uint32_t entry : virtualEntries) addLeaf(entry);
    for (size_t next = 0; next < pending.size(); ++next) {
        const uint32_t owner = pending[next];
        std::vector<Instruction> body;
        try {
            body = Decode(owner);
        } catch (const std::runtime_error& error) {
            rejected_.emplace(owner, error.what());
            continue;
        }
        fingerprints_.emplace(owner, Identify(body));
        for (const auto& instruction : body) {
            for (uint8_t i = 0; i < instruction.decoded.operand_count_visible; ++i) {
                const auto& operand = instruction.operands[i];
                if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY && operand.mem.base == ZYDIS_REGISTER_RIP) {
                    references_.push_back({owner, instruction.rva, instruction.RelativeTarget(i), i});
                }
                if (operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && operand.imm.is_relative
                    && (instruction.decoded.meta.category == ZYDIS_CATEGORY_CALL
                        || instruction.decoded.meta.category == ZYDIS_CATEGORY_UNCOND_BR)) {
                    const auto target = instruction.RelativeTarget(i);
                    const auto unwindOwner = image.FindOwner(target);
                    const auto targetOwner = unwindOwner ? unwindOwner : target;
                    if (targetOwner != owner && image.Contains(target, 1, Image::Readable | Image::Executable, Image::Writable)) {
                        calls_.push_back({owner, instruction.rva, targetOwner, i});
                        if (!unwindOwner) addLeaf(target);
                    }
                }
            }
        }
    }
    const auto byTarget = [](const auto& a, const auto& b) { return a.target < b.target; };
    std::sort(references_.begin(), references_.end(), byTarget);
    std::sort(calls_.begin(), calls_.end(), byTarget);
}

std::vector<Instruction> Instructions::Decode(uint32_t owner) const {
    const auto found = ranges_.find(owner);
    Require(found != ranges_.end(), "Requested function has no primary unwind record");
    const auto rangeAt = [&](uint32_t rva) -> const FunctionRange* {
        for (const auto& range : found->second) if (rva >= range.begin && rva < range.end) return &range;
        return nullptr;
    };
    std::map<uint32_t, Instruction> decoded;
    std::vector<std::pair<uint32_t, uint32_t>> jumpTables;
    std::vector<std::pair<uint32_t, uint32_t>> guardedDispatches;
    const bool leaf = image.FindOwner(owner) == 0;
    std::vector<uint32_t> pending{owner};
    while (!pending.empty()) {
        uint32_t cursor = pending.back();
        pending.pop_back();
        for (;;) {
            if (decoded.find(cursor) != decoded.end()) break;
            const auto* range = rangeAt(cursor);
            Require(range != nullptr, "Control flow leaves its unwind ranges");
            const auto previous = decoded.upper_bound(cursor);
            if (previous != decoded.begin()) {
                const auto& before = std::prev(previous)->second;
                Require(before.rva + before.decoded.length <= cursor, "Branch lands inside an instruction");
            }
            Instruction instruction{};
            instruction.rva = cursor;
            const auto available = range->end - cursor;
            Require(ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder_, image.Bytes(cursor, available), available,
                                                       &instruction.decoded, instruction.operands)),
                    "Invalid or truncated instruction at RVA " + std::to_string(cursor));
            Require(instruction.decoded.length > 0 && instruction.decoded.length <= available,
                    "Instruction crosses its unwind range");
            cursor += instruction.decoded.length;
            Require(previous == decoded.end() || previous->first >= cursor, "Overlapping instruction paths");
            decoded.emplace(instruction.rva, instruction);
            const auto category = instruction.decoded.meta.category;
            if (leaf && category != ZYDIS_CATEGORY_RET) {
                Require(category != ZYDIS_CATEGORY_CALL && category != ZYDIS_CATEGORY_PUSH
                        && category != ZYDIS_CATEGORY_POP, "Unwind-free function changes the call stack");
                for (size_t i = 0; i < instruction.decoded.operand_count; ++i) {
                    const auto& operand = instruction.operands[i];
                    if (operand.type != ZYDIS_OPERAND_TYPE_REGISTER
                        || (operand.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) == 0) continue;
                    const auto reg = ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, operand.reg.value);
                    Require(reg != ZYDIS_REGISTER_RSP && reg != ZYDIS_REGISTER_RBP
                            && reg != ZYDIS_REGISTER_RBX && reg != ZYDIS_REGISTER_RSI && reg != ZYDIS_REGISTER_RDI
                            && !(reg >= ZYDIS_REGISTER_R12 && reg <= ZYDIS_REGISTER_R15),
                            "Unwind-free function writes a nonvolatile register");
                    const auto id = ZydisRegisterGetId(operand.reg.value);
                    const auto registerClass = ZydisRegisterGetClass(operand.reg.value);
                    Require(!((registerClass == ZYDIS_REGCLASS_XMM || registerClass == ZYDIS_REGCLASS_YMM
                               || registerClass == ZYDIS_REGCLASS_ZMM) && id >= 6 && id <= 15),
                            "Unwind-free function writes a nonvolatile vector register");
                }
            }
            if (category == ZYDIS_CATEGORY_RET || instruction.decoded.mnemonic == ZYDIS_MNEMONIC_INT3
                || instruction.decoded.mnemonic == ZYDIS_MNEMONIC_UD2) break;
            if (category == ZYDIS_CATEGORY_UNCOND_BR || category == ZYDIS_CATEGORY_COND_BR) {
                const auto& operand = instruction.operands[0];
                if (category == ZYDIS_CATEGORY_UNCOND_BR && operand.type == ZYDIS_OPERAND_TYPE_REGISTER) {
                    auto prior = decoded.find(instruction.rva);
                    std::vector<Instruction*> before;
                    uint32_t nextAddress = instruction.rva;
                    for (size_t n = 0; n < 5 && prior != decoded.begin(); ++n) {
                        --prior;
                        Require(prior->second.rva + prior->second.decoded.length == nextAddress,
                                "Switch dispatch is not contiguous");
                        nextAddress = prior->second.rva;
                        before.push_back(&prior->second);
                    }
                    Require(before.size() == 5, "Switch dispatch has no bounded guard");
                    const auto reg64 = [](const ZydisDecodedOperand& op) {
                        return op.type == ZYDIS_OPERAND_TYPE_REGISTER
                            ? ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, op.reg.value)
                            : ZYDIS_REGISTER_NONE;
                    };
                    const auto& add = *before[0];
                    auto& load = *before[1];
                    const auto& extend = *before[2];
                    const auto& guard = *before[3];
                    const auto& compare = *before[4];
                    const auto base = reg64(add.operands[1]);
                    const auto index = reg64(extend.operands[0]);
                    const auto& memory = load.operands[1];
                    Require(add.decoded.mnemonic == ZYDIS_MNEMONIC_ADD && reg64(add.operands[0]) == operand.reg.value
                            && base != ZYDIS_REGISTER_NONE && load.decoded.mnemonic == ZYDIS_MNEMONIC_MOV
                            && reg64(load.operands[0]) == operand.reg.value && load.operands[0].size == 32
                            && memory.type == ZYDIS_OPERAND_TYPE_MEMORY && memory.size == 32
                            && memory.mem.base == base && memory.mem.index == index && memory.mem.scale == 4
                            && extend.decoded.mnemonic == ZYDIS_MNEMONIC_MOVSXD
                            && reg64(extend.operands[1]) == reg64(compare.operands[0])
                            && guard.decoded.mnemonic == ZYDIS_MNEMONIC_JNBE
                            && compare.decoded.mnemonic == ZYDIS_MNEMONIC_CMP
                            && compare.operands[0].size == 32 && compare.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                            && compare.operands[1].imm.value.u < 256,
                            "Indirect branch is not a guarded image-relative switch");
                    bool baseDefined = false;
                    for (const auto& [at, candidate] : decoded) {
                        if (at >= compare.rva) break;
                        if (candidate.decoded.mnemonic == ZYDIS_MNEMONIC_LEA
                            && reg64(candidate.operands[0]) == base
                            && candidate.operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY
                            && candidate.operands[1].mem.base == ZYDIS_REGISTER_RIP
                            && candidate.RelativeTarget(1) == 0) {
                            Require(!baseDefined, "Switch image base is redefined");
                            baseDefined = true;
                            continue;
                        }
                        for (size_t j = 0; j < candidate.decoded.operand_count; ++j) {
                            const auto& op = candidate.operands[j];
                            Require(!(op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) || reg64(op) != base,
                                    "Switch image base is clobbered");
                        }
                        if (!baseDefined) Require(candidate.decoded.meta.category != ZYDIS_CATEGORY_CALL
                                                  && candidate.decoded.meta.category != ZYDIS_CATEGORY_COND_BR
                                                  && candidate.decoded.meta.category != ZYDIS_CATEGORY_UNCOND_BR,
                                                  "Switch image base does not dominate dispatch");
                    }
                    Require(baseDefined && memory.mem.disp.value >= 0 && memory.mem.disp.value <= UINT32_MAX,
                            "Switch has no image-base proof");
                    const uint32_t table = static_cast<uint32_t>(memory.mem.disp.value);
                    const uint32_t count = static_cast<uint32_t>(compare.operands[1].imm.value.u + 1);
                    Require(table % 4 == 0 && image.Contains(table, count * 4, Image::Readable, Image::Writable),
                            "Switch table is outside immutable image bounds");
                    Require(rangeAt(guard.RelativeTarget(0)), "Switch default leaves its function");
                    auto& dispatch = decoded.at(instruction.rva);
                    for (uint32_t j = 0; j < count; ++j) {
                        const auto target = image.Read<uint32_t>(table + j * 4);
                        Require(rangeAt(target) != nullptr, "Switch case leaves its function");
                        dispatch.indirectTargets.push_back(target);
                        pending.push_back(target);
                    }
                    load.imageRelativeMemory = 1;
                    jumpTables.emplace_back(table, table + count * 4);
                    guardedDispatches.emplace_back(compare.rva + compare.decoded.length,
                                                   instruction.rva + instruction.decoded.length);
                    break;
                }
                if (category == ZYDIS_CATEGORY_UNCOND_BR && operand.type == ZYDIS_OPERAND_TYPE_MEMORY
                    && operand.size == 64 && operand.mem.index == ZYDIS_REGISTER_NONE
                    && operand.mem.disp.value >= 0 && operand.mem.disp.value <= 4096
                    && operand.mem.disp.value % 8 == 0) {
                    auto before = decoded.find(instruction.rva);
                    bool dispatch = false;
                    uint32_t precedingEnd = instruction.rva;
                    auto receiver = ZYDIS_REGISTER_RCX;
                    for (size_t lookback = 0; lookback < 16 && before != decoded.begin(); ++lookback) {
                        --before;
                        const auto& prior = before->second;
                        if (prior.rva + prior.decoded.length != precedingEnd) break;
                        precedingEnd = prior.rva;
                        const auto& destination = prior.operands[0];
                        const auto& source = prior.operands[1];
                        if (prior.decoded.mnemonic == ZYDIS_MNEMONIC_MOV
                            && destination.type == ZYDIS_OPERAND_TYPE_REGISTER && destination.size == 64
                            && destination.reg.value == operand.mem.base && source.type == ZYDIS_OPERAND_TYPE_MEMORY
                            && source.size == 64 && source.mem.index == ZYDIS_REGISTER_NONE
                            && source.mem.disp.value == 0 && source.mem.base == receiver) {
                            dispatch = true;
                            break;
                        }
                        if (prior.decoded.mnemonic == ZYDIS_MNEMONIC_MOV
                            && destination.type == ZYDIS_OPERAND_TYPE_REGISTER && destination.reg.value == receiver
                            && source.type == ZYDIS_OPERAND_TYPE_REGISTER && source.size == 64) {
                            receiver = source.reg.value;
                            continue;
                        }
                        if (prior.decoded.meta.category == ZYDIS_CATEGORY_CALL
                            || prior.decoded.meta.category == ZYDIS_CATEGORY_COND_BR
                            || prior.decoded.meta.category == ZYDIS_CATEGORY_UNCOND_BR
                            || prior.decoded.meta.category == ZYDIS_CATEGORY_RET) break;
                        bool clobbered = false;
                        for (size_t j = 0; j < prior.decoded.operand_count; ++j) {
                            const auto& written = prior.operands[j];
                            if (written.type != ZYDIS_OPERAND_TYPE_REGISTER
                                || !(written.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE)) continue;
                            const auto reg = ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, written.reg.value);
                            if (reg == receiver || reg == operand.mem.base) clobbered = true;
                        }
                        if (clobbered) break;
                    }
                    Require(dispatch, "Indirect jump does not have a bounded virtual-dispatch contract");
                    break;
                }
                Require(operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && operand.imm.is_relative,
                        "Indirect branch requires a validated dispatch contract at RVA " + std::to_string(instruction.rva));
                const uint32_t target = instruction.RelativeTarget(0);
                if (rangeAt(target)) pending.push_back(target);
                else Require(category == ZYDIS_CATEGORY_UNCOND_BR
                             && image.Contains(target, 1, Image::Readable | Image::Executable, Image::Writable),
                             "Branch target leaves executable image");
                if (category == ZYDIS_CATEGORY_UNCOND_BR) break;
            }
        }
    }
    std::vector<Instruction> body;
    for (auto& [address, instruction] : decoded) {
        std::vector<uint32_t> targets = instruction.indirectTargets;
        const auto category = instruction.decoded.meta.category;
        if ((category == ZYDIS_CATEGORY_COND_BR || category == ZYDIS_CATEGORY_UNCOND_BR
             || category == ZYDIS_CATEGORY_CALL)
            && instruction.operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
            && instruction.operands[0].imm.is_relative) targets.push_back(instruction.RelativeTarget(0));
        for (const auto& [begin, end] : guardedDispatches) {
            for (uint32_t target : targets) {
                Require(target < begin || target >= end, "Control flow bypasses a switch bounds check");
            }
        }
        for (const auto& [begin, end] : jumpTables) {
            Require(address >= end || address + instruction.decoded.length <= begin,
                    "Switch data overlaps executable instructions");
        }
        body.push_back(std::move(instruction));
    }
    return body;
}

Fingerprint Instructions::Identify(const std::vector<Instruction>& body,
                                   const std::vector<OperandLocation>& variableConstants) const {
    Fingerprint result;
    result.instructions = static_cast<uint32_t>(body.size());
    std::map<uint32_t, size_t> positions;
    for (size_t i = 0; i < body.size(); ++i) positions.emplace(body[i].rva, i);
    for (size_t index = 0; index < body.size(); ++index) {
        const auto& instruction = body[index];
        Hash(result.shape, instruction.decoded.mnemonic);
        Hash(result.shape, instruction.decoded.operand_count_visible);
        Hash(result.shape, instruction.decoded.attributes & (ZYDIS_ATTRIB_HAS_LOCK | ZYDIS_ATTRIB_HAS_REP
             | ZYDIS_ATTRIB_HAS_REPE | ZYDIS_ATTRIB_HAS_REPNE));
        for (uint32_t target : instruction.indirectTargets) {
            Require(positions.count(target) != 0, "Switch target was not decoded");
            Hash(result.shape, positions.at(target));
        }
        for (uint8_t i = 0; i < instruction.decoded.operand_count_visible; ++i) {
            const auto& operand = instruction.operands[i];
            Hash(result.shape, operand.type);
            Hash(result.shape, operand.size);
            Hash(result.shape, operand.actions);
            switch (operand.type) {
            case ZYDIS_OPERAND_TYPE_REGISTER:
                Hash(result.shape, operand.reg.value);
                break;
            case ZYDIS_OPERAND_TYPE_MEMORY:
                Hash(result.shape, operand.mem.type);
                Hash(result.shape, operand.mem.segment);
                Hash(result.shape, operand.mem.base);
                Hash(result.shape, operand.mem.index);
                Hash(result.shape, operand.mem.scale);
                if (operand.mem.base != ZYDIS_REGISTER_RIP && instruction.imageRelativeMemory != i) {
                    const bool variable = std::any_of(variableConstants.begin(), variableConstants.end(),
                        [&](auto location) { return location.instruction == index && location.operand == i; });
                    Hash(result.constants, variable ? 0 : operand.mem.disp.value);
                }
                break;
            case ZYDIS_OPERAND_TYPE_IMMEDIATE:
                Hash(result.shape, operand.imm.is_relative);
                if (operand.imm.is_relative) {
                    const auto target = instruction.RelativeTarget(i);
                    const auto local = positions.find(target);
                    Hash(result.shape, local == positions.end() ? UINT64_MAX : local->second);
                } else {
                    const bool variable = std::any_of(variableConstants.begin(), variableConstants.end(),
                        [&](auto location) { return location.instruction == index && location.operand == i; });
                    Hash(result.constants, variable ? 0 : operand.imm.value.u);
                }
                break;
            default:
                throw std::runtime_error("Unsupported AMD64 operand kind");
            }
        }
    }
    return result;
}

std::vector<Reference> Instructions::Referencing(uint32_t target) const {
    return ReferencesAt(references_, target);
}

std::vector<Reference> Instructions::Calling(uint32_t target) const {
    return ReferencesAt(calls_, target);
}

}
