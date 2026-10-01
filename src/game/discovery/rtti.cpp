#include "rtti.h"

#include <map>
#include <set>

namespace StarfieldHT::discovery {

std::vector<Vtable> FindVtables(const Image& image, const std::string& decoratedName) {
    const auto names = image.FindString(decoratedName);
    Require(names.size() == 1, "RTTI name is missing or ambiguous: " + decoratedName);
    Require(names[0] >= 16, "RTTI descriptor precedes the image");
    const uint32_t descriptor = names[0] - 16;
    Require(descriptor % 8 == 0 && image.Contains(descriptor, decoratedName.size() + 17, Image::Readable, Image::Executable),
            "Invalid RTTI descriptor: " + decoratedName);
    const uint32_t typeTable = image.Rva(image.Read<uint64_t>(descriptor));
    Require(image.Contains(typeTable, 8, Image::Readable, Image::Executable | Image::Writable), "Invalid type_info vtable");
    const uint32_t typeMethod = image.Rva(image.Read<uint64_t>(typeTable));
    Require(image.Contains(typeMethod, 1, Image::Readable | Image::Executable, Image::Writable), "Invalid type_info method");

    std::map<uint32_t, uint32_t> locators;
    for (const auto& section : image.sections) {
        if ((section.flags & Image::Readable) == 0 || (section.flags & (Image::Executable | Image::Writable)) != 0) continue;
        for (uint32_t at = (section.begin + 3) & ~3u; uint64_t(at) + 24 <= section.end; at += 4) {
            if (image.Read<uint32_t>(at) != 1 || image.Read<uint32_t>(at + 12) != descriptor
                || image.Read<uint32_t>(at + 20) != at) continue;
            Require(image.Read<uint32_t>(at + 8) == 0, "Construction-displaced RTTI is not supported");
            const uint32_t offset = image.Read<uint32_t>(at + 4);
            const uint32_t hierarchy = image.Read<uint32_t>(at + 16);
            Require(image.Contains(hierarchy, 16, Image::Readable, Image::Executable | Image::Writable), "Invalid RTTI hierarchy");
            Require(image.Read<uint32_t>(hierarchy) == 0 && (image.Read<uint32_t>(hierarchy + 4) & ~7u) == 0,
                    "Unknown RTTI hierarchy representation");
            const uint32_t count = image.Read<uint32_t>(hierarchy + 8);
            const uint32_t array = image.Read<uint32_t>(hierarchy + 12);
            Require(count > 0 && count <= 4096 && image.Contains(array, uint64_t(count) * 4, Image::Readable,
                    Image::Executable | Image::Writable), "Invalid RTTI base array");
            bool ownsOffset = false;
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t entry = image.Read<uint32_t>(array + i * 4);
                Require(image.Contains(entry, 24, Image::Readable, Image::Executable | Image::Writable), "Truncated RTTI base descriptor");
                const uint32_t baseType = image.Read<uint32_t>(entry);
                Require(image.Contains(baseType, 17, Image::Readable, Image::Executable), "Invalid RTTI base type");
                Require(image.Read<uint32_t>(entry + 4) < count, "RTTI nested-base count exceeds its hierarchy");
                if (i == 0) Require(baseType == descriptor, "RTTI hierarchy has the wrong complete owner");
                if (image.Read<int32_t>(entry + 12) == -1 && image.Read<uint32_t>(entry + 8) == offset) ownsOffset = true;
            }
            Require(ownsOffset, "RTTI locator offset is not owned by a nonvirtual base");
            locators.emplace(at, offset);
        }
    }
    Require(!locators.empty(), "No validated RTTI locator: " + decoratedName);
    std::vector<Vtable> result;
    std::set<uint32_t> offsets;
    for (const auto& section : image.sections) {
        if ((section.flags & Image::Readable) == 0 || (section.flags & (Image::Executable | Image::Writable)) != 0) continue;
        for (uint32_t at = (section.begin + 7) & ~7u; uint64_t(at) + 16 <= section.end; at += 8) {
            const uint64_t pointer = image.Read<uint64_t>(at);
            if (pointer < image.base || pointer - image.base > UINT32_MAX) continue;
            const auto locator = locators.find(static_cast<uint32_t>(pointer - image.base));
            if (locator == locators.end()) continue;
            Vtable table{at + 8, locator->second, {}};
            for (uint32_t slot = 0; slot < 1024; ++slot) {
                const uint64_t cell = uint64_t(table.rva) + uint64_t(slot) * 8;
                if (!image.Contains(cell, 8, Image::Readable, Image::Executable | Image::Writable)) break;
                const uint64_t method = image.Read<uint64_t>(static_cast<uint32_t>(cell));
                if (method < image.base || method - image.base > UINT32_MAX
                    || !image.Contains(method - image.base, 1, Image::Readable | Image::Executable, Image::Writable)) break;
                table.methods.push_back(static_cast<uint32_t>(method - image.base));
            }
            Require(!table.methods.empty() && table.methods.size() < 1024, "Empty or unbounded RTTI vtable");
            Require(offsets.insert(table.objectOffset).second, "Ambiguous RTTI vtable ownership: " + decoratedName);
            result.push_back(std::move(table));
        }
    }
    Require(!result.empty(), "No validated vtable: " + decoratedName);
    return result;
}

uint32_t FindVirtualSlot(const std::vector<Vtable>& tables, uint32_t method,
                         uint32_t& tableRva, uint32_t& objectOffset) {
    uint32_t slot = 0;
    size_t matches = 0;
    for (const auto& table : tables) {
        for (size_t i = 0; i < table.methods.size(); ++i) {
            if (table.methods[i] != method) continue;
            ++matches;
            slot = static_cast<uint32_t>(i);
            tableRva = table.rva;
            objectOffset = table.objectOffset;
        }
    }
    Require(matches == 1, "Virtual method slot is missing or ambiguous");
    return slot;
}

}
