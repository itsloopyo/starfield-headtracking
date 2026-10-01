#include "image.h"

#include <algorithm>
#include <limits>
#include <set>

namespace StarfieldHT::discovery {

void Require(bool condition, const std::string& diagnostic) {
    if (!condition) throw std::runtime_error(diagnostic);
}

const uint8_t* Image::Bytes(uint32_t rva, size_t size) const {
    Require(rva <= data_.size() && size <= data_.size() - rva, "PE read exceeds image bounds");
    return data_.data() + rva;
}

bool Image::Contains(uint64_t rva, size_t size, uint32_t required, uint32_t forbidden) const {
    for (const auto& section : sections) {
        if ((section.flags & required) != required || (section.flags & forbidden) != 0) continue;
        if (rva >= section.begin && rva < section.end && size <= section.end - rva) return true;
    }
    return false;
}

uint32_t Image::Rva(uint64_t address) const {
    Require(address >= base && address - base < data_.size(), "Address is outside the image");
    return static_cast<uint32_t>(address - base);
}

Image::Image(std::vector<uint8_t> mapped) : data_(std::move(mapped)) {
    Require(Read<uint16_t>(0) == 0x5a4d, "Missing DOS signature");
    const uint32_t pe = Read<uint32_t>(0x3c);
    Require(pe >= 0x40 && pe <= 0x100000 && Read<uint32_t>(pe) == 0x4550, "Invalid PE signature");
    Require(Read<uint16_t>(pe + 4) == 0x8664, "Discovery requires an AMD64 image");
    Require(Read<uint16_t>(pe + 24) == 0x20b, "Discovery requires PE32+");
    const uint32_t optional = pe + 24;
    const uint16_t optionalSize = Read<uint16_t>(pe + 20);
    Require(optionalSize >= 144, "PE optional header is truncated");
    Require(Read<uint32_t>(optional + 56) == data_.size(), "Mapped PE size disagrees with header");
    base = Read<uint64_t>(optional + 24);
    Require(base <= std::numeric_limits<uint64_t>::max() - data_.size(), "PE address range overflows");
    timestamp = Read<uint32_t>(pe + 8);
    checksum = Read<uint32_t>(optional + 64);
    const uint16_t count = Read<uint16_t>(pe + 6);
    Require(count > 0 && count <= 96, "Invalid PE section count");
    const uint32_t table = optional + optionalSize;
    const uint32_t headerSize = Read<uint32_t>(optional + 60);
    Require(headerSize <= data_.size() && uint64_t(table) + uint64_t(count) * 40 <= headerSize,
            "Section table exceeds mapped PE headers");
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t at = table + i * 40;
        const uint32_t begin = Read<uint32_t>(at + 12);
        const uint32_t size = Read<uint32_t>(at + 8);
        Require(begin >= headerSize && begin < data_.size() && size <= data_.size() - begin, "PE section exceeds image or overlaps headers");
        sections.push_back({begin, begin + size, Read<uint32_t>(at + 36)});
    }
    std::sort(sections.begin(), sections.end(), [](auto a, auto b) { return a.begin < b.begin; });
    for (size_t i = 1; i < sections.size(); ++i) {
        Require(sections[i - 1].end <= sections[i].begin, "Overlapping PE sections");
    }
    Require(Read<uint32_t>(optional + 108) >= 4, "PE has no exception directory");
    const uint32_t directory = Read<uint32_t>(optional + 112 + 3 * 8);
    const uint32_t size = Read<uint32_t>(optional + 116 + 3 * 8);
    Require(size > 0 && size % 12 == 0 && Contains(directory, size, Readable, Executable | Writable),
            "Invalid exception directory");
    for (uint32_t at = directory; at < directory + size; at += 12) {
        const uint32_t begin = Read<uint32_t>(at);
        const uint32_t end = Read<uint32_t>(at + 4);
        uint32_t unwind = Read<uint32_t>(at + 8);
        uint32_t owner = begin;
        Require(begin < end && Contains(begin, end - begin, Readable | Executable, Writable),
                "Invalid executable function range");
        std::set<uint32_t> seen;
        for (;;) {
            Require(seen.insert(unwind).second && seen.size() <= 32, "Cyclic or excessive unwind chain");
            Require(Contains(unwind, 4, Readable, Executable | Writable), "Invalid unwind header");
            const uint8_t flags = Read<uint8_t>(unwind);
            Require((flags & 0xc0) == 0, "Unknown unwind flags");
            Require((flags & 7) == 1 || (flags & 7) == 2, "Unknown unwind representation");
            const uint32_t codes = (Read<uint8_t>(unwind + 2) + 1u) & ~1u;
            Require(Contains(unwind, 4 + codes * 2, Readable, Executable | Writable), "Truncated unwind codes");
            if ((flags & 0x20) == 0) break;
            Require((flags & 0x18) == 0, "Chained unwind record also declares a handler");
            const uint32_t chain = unwind + 4 + codes * 2;
            Require(Contains(chain, 12, Readable, Executable | Writable), "Truncated unwind chain");
            owner = Read<uint32_t>(chain);
            const uint32_t ownerEnd = Read<uint32_t>(chain + 4);
            Require(owner < ownerEnd && Contains(owner, ownerEnd - owner, Readable | Executable, Writable),
                    "Invalid chained function range");
            unwind = Read<uint32_t>(chain + 8);
        }
        functions.push_back({begin, end, owner});
    }
    for (size_t i = 1; i < functions.size(); ++i) {
        Require(functions[i - 1].end <= functions[i].begin, "Unsorted or overlapping function table");
    }
    for (const auto& function : functions) {
        const auto owner = std::lower_bound(functions.begin(), functions.end(), function.owner,
            [](auto range, uint32_t rva) { return range.begin < rva; });
        Require(owner != functions.end() && owner->begin == function.owner && owner->owner == function.owner,
                "Unwind chain has no primary function");
    }
}

Image Image::FromFile(const std::vector<uint8_t>& bytes) {
    const auto read = [&](size_t offset, size_t width) {
        Require(width <= 8 && offset <= bytes.size() && width <= bytes.size() - offset, "Truncated PE file");
        uint64_t value = 0;
        std::memcpy(&value, bytes.data() + offset, width);
        return value;
    };
    Require(read(0, 2) == 0x5a4d, "Missing file DOS signature");
    const size_t pe = static_cast<size_t>(read(0x3c, 4));
    Require(pe >= 0x40 && pe <= 0x100000 && read(pe, 4) == 0x4550, "Invalid file PE signature");
    const size_t optional = pe + 24;
    const size_t imageSize = static_cast<size_t>(read(optional + 56, 4));
    Require(imageSize > 0 && imageSize <= 512u * 1024 * 1024, "Unreasonable PE image size");
    const size_t headerSize = static_cast<size_t>(read(optional + 60, 4));
    Require(headerSize <= imageSize && headerSize <= bytes.size(), "Invalid PE header size");
    std::vector<uint8_t> mapped(imageSize);
    std::memcpy(mapped.data(), bytes.data(), headerSize);
    const size_t table = optional + static_cast<size_t>(read(pe + 20, 2));
    const size_t count = static_cast<size_t>(read(pe + 6, 2));
    Require(count > 0 && count <= 96 && table + count * 40 <= headerSize, "Section table outside PE headers");
    for (size_t i = 0; i < count; ++i) {
        const size_t at = table + i * 40;
        const size_t rva = static_cast<size_t>(read(at + 12, 4));
        const size_t rawSize = static_cast<size_t>(read(at + 16, 4));
        const size_t raw = static_cast<size_t>(read(at + 20, 4));
        Require(rva >= headerSize && rva <= imageSize && rawSize <= imageSize - rva,
                "Raw section exceeds mapped image");
        Require(raw <= bytes.size() && rawSize <= bytes.size() - raw, "Truncated raw section");
        std::memcpy(mapped.data() + rva, bytes.data() + raw, rawSize);
    }
    return Image(std::move(mapped));
}

uint32_t Image::Owner(uint32_t rva) const {
    const auto owner = FindOwner(rva);
    Require(owner != 0, "Instruction has no unwind range");
    return owner;
}

uint32_t Image::FindOwner(uint32_t rva) const {
    const auto found = std::upper_bound(functions.begin(), functions.end(), rva,
        [](uint32_t address, auto range) { return address < range.begin; });
    if (found == functions.begin()) return 0;
    const auto& range = *(found - 1);
    return rva < range.end ? range.owner : 0;
}

std::string Image::String(uint32_t rva, size_t limit) const {
    std::string result;
    for (size_t i = 0; i < limit; ++i) {
        Require(Contains(uint64_t(rva) + i, 1, Readable, Executable), "String exceeds readable data");
        const char value = Read<char>(rva + static_cast<uint32_t>(i));
        if (value == 0) return result;
        Require(value >= 0x20 && value <= 0x7e, "Invalid metadata string");
        result += value;
    }
    throw std::runtime_error("Unterminated metadata string");
}

std::vector<uint32_t> Image::FindString(const std::string& text) const {
    std::vector<uint32_t> result;
    for (const auto& section : sections) {
        if ((section.flags & Readable) == 0 || (section.flags & Executable) != 0) continue;
        const auto* begin = Bytes(section.begin, section.end - section.begin);
        const auto* end = begin + section.end - section.begin;
        auto* cursor = begin;
        while (cursor < end) {
            cursor = std::search(cursor, end, text.c_str(), text.c_str() + text.size() + 1);
            if (cursor == end) break;
            result.push_back(section.begin + static_cast<uint32_t>(cursor - begin));
            ++cursor;
        }
    }
    return result;
}

}
