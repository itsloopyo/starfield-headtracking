#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

namespace StarfieldHT::discovery {

struct Section {
    uint32_t begin;
    uint32_t end;
    uint32_t flags;
};

struct FunctionRange {
    uint32_t begin;
    uint32_t end;
    uint32_t owner;
};

class Image {
public:
    explicit Image(std::vector<uint8_t> mapped);
    static Image FromFile(const std::vector<uint8_t>& bytes);

    template<class T> T Read(uint32_t rva) const {
        T value;
        std::memcpy(&value, Bytes(rva, sizeof(T)), sizeof(T));
        return value;
    }

    const uint8_t* Bytes(uint32_t rva, size_t size) const;
    bool Contains(uint64_t rva, size_t size, uint32_t required, uint32_t forbidden = 0) const;
    uint32_t Rva(uint64_t address) const;
    uint32_t Owner(uint32_t rva) const;
    uint32_t FindOwner(uint32_t rva) const;
    std::string String(uint32_t rva, size_t limit) const;
    std::vector<uint32_t> FindString(const std::string& text) const;

    uint64_t base = 0;
    uint32_t timestamp = 0;
    uint32_t checksum = 0;
    std::vector<Section> sections;
    std::vector<FunctionRange> functions;
    static constexpr uint32_t Readable = 0x40000000;
    static constexpr uint32_t Writable = 0x80000000;
    static constexpr uint32_t Executable = 0x20000000;

private:
    std::vector<uint8_t> data_;
};

void Require(bool condition, const std::string& diagnostic);

}
