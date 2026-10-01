#include "contracts.h"
#include <set>

namespace StarfieldHT::discovery {
namespace {
void Agree(std::map<std::string,uint32_t>& values, const char* name, uint32_t value) {
    const auto existing = values.emplace(name, value);
    Require(existing.second || existing.first->second == value, std::string("Inconsistent dependency: ") + name);
}
const Instruction& At(const std::vector<Instruction>& body, uint32_t index, uint8_t operand) {
    Require(index < body.size() && operand < body[index].decoded.operand_count_visible,
            "Contract operand exceeds its decoded method");
    return body[index];
}
}

ResolvedContracts ResolveContracts(const Image& image, const ContractSet& contracts) {
    std::map<std::string, std::vector<Vtable>> types;
    std::vector<uint32_t> virtualEntries;
    for (const auto& method : contracts.methods) {
        if (!method.owner || types.count(method.owner)) continue;
        auto tables = FindVtables(image, method.owner);
        for (const auto& table : tables) {
            virtualEntries.insert(virtualEntries.end(), table.methods.begin(), table.methods.end());
        }
        types.emplace(method.owner, std::move(tables));
    }
    Instructions code(image, virtualEntries);
    ResolvedContracts result;
    for (const auto& [name, tables] : types) {
        for (const auto& table : tables) {
            if (table.objectOffset == 0) result.classes.emplace(name, table.rva);
        }
        Require(result.classes.count(name) == 1, "Type has no complete-object vtable: " + name);
    }
    std::map<std::string, std::vector<Instruction>> bodies;
    for (const auto& method : contracts.methods) {
        std::map<std::string, uint32_t> namedLocations;
        for (const auto& anchor : method.names) {
            const auto locations = image.FindString(anchor.text);
            Require(locations.size() == 1, std::string("Missing or ambiguous named anchor: ") + anchor.text);
            namedLocations.emplace(anchor.text, locations.front());
        }
        std::vector<uint32_t> candidates;
        for (const auto& [rva, fingerprint] : code.Fingerprints()) {
            if (!(fingerprint == method.fingerprint)) continue;
            bool matches = true;
            if (method.owner) {
                size_t count = 0;
                for (const auto& table : types.at(method.owner)) {
                    for (const auto entry : table.methods) if (entry == rva) ++count;
                }
                matches = count == 1;
            }
            if (!matches) continue;
            for (const auto& link : contracts.links) {
                if (std::string(link.destination) != method.name || !bodies.count(link.source)) continue;
                if (At(bodies.at(link.source), link.instruction, link.operand).RelativeTarget(link.operand) != rva) {
                    matches = false;
                }
            }
            if (!matches) continue;
            const auto body = code.Decode(rva);
            for (const auto& anchor : method.names) {
                if (At(body, anchor.instruction, anchor.operand).RelativeTarget(anchor.operand) != namedLocations.at(anchor.text)) matches = false;
            }
            if (matches) candidates.push_back(rva);
        }
        Require(candidates.size() == 1, std::string("Missing or ambiguous method contract: ") + method.name
                + " (matches=" + std::to_string(candidates.size()) + ")");
        const auto rva = candidates.front();
        Require(result.methods.emplace(method.name, rva).second, "Duplicate method contract name");
        bodies.emplace(method.name, code.Decode(rva));
        if (method.owner) {
            uint32_t table = 0, offset = 0;
            const auto slot = FindVirtualSlot(types.at(method.owner), rva, table, offset);
            result.tables.emplace(method.name, table);
            result.slots.emplace(method.name, slot);
        }
    }
    std::set<std::string> corroborated;
    for (const auto& method : contracts.methods) {
        if (method.owner || !method.names.empty()) corroborated.insert(method.name);
    }
    for (const auto& link : contracts.links) {
        const auto& instruction = At(bodies.at(link.source), link.instruction, link.operand);
        Require(instruction.decoded.meta.category == ZYDIS_CATEGORY_CALL
                || instruction.decoded.meta.category == ZYDIS_CATEGORY_UNCOND_BR,
                std::string("Dependency is not a call or tail call: ") + link.source);
        Require(instruction.RelativeTarget(link.operand) == result.methods.at(link.destination),
                std::string("Call relationship disagrees: ") + link.source + " -> " + link.destination);
        Agree(result.returns, (std::string(link.source) + "/" + std::to_string(link.instruction) + "/" + link.destination).c_str(),
              instruction.rva + instruction.decoded.length);
    }
    for (const auto& link : contracts.tables) {
        const auto& instruction = At(bodies.at(link.source), link.instruction, link.operand);
        Require(instruction.RelativeTarget(link.operand) == result.tables.at(link.destination),
                std::string("Constructor vtable disagrees: ") + link.source);
        corroborated.insert(link.source);
    }
    for (size_t pass = 0; pass < contracts.methods.size(); ++pass) {
        for (const auto& link : contracts.links) {
            if (corroborated.count(link.source)) corroborated.insert(link.destination);
            if (corroborated.count(link.destination)) corroborated.insert(link.source);
        }
    }
    for (const auto& method : contracts.methods) {
        Require(corroborated.count(method.name) != 0, std::string("Method has no independent owner/name relationship: ") + method.name);
    }
    for (const auto& data : contracts.data) {
        const auto& instruction = At(bodies.at(data.source), data.instruction, data.operand);
        Require(instruction.operands[data.operand].type == ZYDIS_OPERAND_TYPE_MEMORY
                && instruction.operands[data.operand].mem.base == ZYDIS_REGISTER_RIP,
                "Data dependency is not image-relative");
        const auto target = instruction.RelativeTarget(data.operand);
        Require(data.alignment && target % data.alignment == 0
                && image.Contains(target, data.size, data.required, data.forbidden),
                std::string("Data dependency has invalid bounds, alignment or protection: ") + data.destination);
        Agree(result.data, data.destination, target);
    }
    for (const auto& member : contracts.members) {
        const auto& operand = At(bodies.at(member.source), member.instruction, member.operand).operands[member.operand];
        Require(operand.type == ZYDIS_OPERAND_TYPE_MEMORY && operand.size == member.width * 8
                && operand.mem.base != ZYDIS_REGISTER_RIP && operand.mem.base != ZYDIS_REGISTER_NONE
                && operand.mem.index == ZYDIS_REGISTER_NONE, "Member has the wrong addressing or width");
        const auto offset = operand.mem.disp.value + member.adjustment;
        Require(offset >= 0 && member.alignment && offset % member.alignment == 0
                && uint64_t(offset) <= member.containingSize
                && member.width <= member.containingSize - uint64_t(offset),
                std::string("Member exceeds containing bounds or alignment: ") + member.destination);
        Agree(result.members, member.destination, static_cast<uint32_t>(offset));
    }
    return result;
}
}
