#pragma once

#include "image.h"

namespace StarfieldHT::discovery {

struct Vtable {
    uint32_t rva;
    uint32_t objectOffset;
    std::vector<uint32_t> methods;
};

std::vector<Vtable> FindVtables(const Image& image, const std::string& decoratedName);
uint32_t FindVirtualSlot(const std::vector<Vtable>& tables, uint32_t method,
                         uint32_t& tableRva, uint32_t& objectOffset);

}
