#include "game/discovery/instructions.h"
#include "game/build_selection.h"
#include <cstdio>
#include <functional>

using namespace StarfieldHT::discovery;

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) { std::printf("FAIL: %s\n", message); ++failures; }
}

template<class T> void Put(std::vector<uint8_t>& bytes, size_t at, T value) {
    std::memcpy(bytes.data() + at, &value, sizeof(value));
}

void Code(std::vector<uint8_t>& bytes, size_t at, std::initializer_list<uint8_t> code) {
    std::copy(code.begin(), code.end(), bytes.begin() + at);
}

std::vector<uint8_t> Fixture(uint32_t shift = 0, uint32_t field = 0x80, uint64_t base = 0x140000000ull) {
    std::vector<uint8_t> bytes(0x6000);
    Put<uint16_t>(bytes, 0, 0x5a4d);
    Put<uint32_t>(bytes, 0x3c, 0x80);
    Put<uint32_t>(bytes, 0x80, 0x4550);
    Put<uint16_t>(bytes, 0x84, 0x8664);
    Put<uint16_t>(bytes, 0x86, 4);
    Put<uint16_t>(bytes, 0x94, 240);
    Put<uint16_t>(bytes, 0x98, 0x20b);
    Put<uint64_t>(bytes, 0xb0, base);
    Put<uint32_t>(bytes, 0xd0, static_cast<uint32_t>(bytes.size()));
    Put<uint32_t>(bytes, 0xd4, 0x400);
    Put<uint32_t>(bytes, 0x104, 16);
    Put<uint32_t>(bytes, 0x120, 0x3000);
    Put<uint32_t>(bytes, 0x124, 12);
    for (uint32_t i = 0; i < 4; ++i) {
        const auto header = 0x188 + i * 40;
        const auto rva = 0x1000 + i * 0x1000;
        Put<uint32_t>(bytes, header + 8, 0x1000);
        Put<uint32_t>(bytes, header + 12, rva);
        Put<uint32_t>(bytes, header + 16, 0x1000);
        Put<uint32_t>(bytes, header + 20, rva);
        Put<uint32_t>(bytes, header + 36, Image::Readable | (i == 0 ? Image::Executable : i == 3 ? Image::Writable : 0));
    }
    const auto start = 0x1000 + shift;
    Code(bytes, start, {0x48,0x8b,0x81,0,0,0,0, 0x48,0x8b,0x15,0,0,0,0, 0xe8,0,0,0,0,0xc3});
    Put<uint32_t>(bytes, start + 3, field);
    Put<uint32_t>(bytes, start + 10, 0x4000 + shift - (start + 14));
    Put<uint32_t>(bytes, start + 15, 0x1200 + shift - (start + 19));
    Code(bytes, 0x1200 + shift, {0x8b,0x41,0x10,0xc3});
    Put<uint32_t>(bytes, 0x3000, start);
    Put<uint32_t>(bytes, 0x3004, start + 20);
    Put<uint32_t>(bytes, 0x3008, 0x2000);
    bytes[0x2000] = 1;
    Code(bytes, 0x2100, {'F','i','x','t','u','r','e',0});
    return bytes;
}

void RejectImage(std::vector<uint8_t> bytes, const char* message) {
    try { Image image(std::move(bytes)); Check(false, message); }
    catch (const std::runtime_error&) {}
}

std::vector<uint8_t> TypedFixture(uint32_t shift = 0, unsigned slot = 0) {
    auto bytes = Fixture(shift);
    const uint64_t base = 0x140000000ull;
    const char name[] = ".?AVFixture@@";
    std::memcpy(bytes.data() + 0x2200, name, sizeof(name));
    Put<uint64_t>(bytes, 0x21f0, base + 0x2300);
    Put<uint64_t>(bytes, 0x2300, base + 0x1200 + shift);
    Put<uint32_t>(bytes, 0x2400, 1);
    Put<uint32_t>(bytes, 0x240c, 0x21f0);
    Put<uint32_t>(bytes, 0x2410, 0x2440);
    Put<uint32_t>(bytes, 0x2414, 0x2400);
    Put<uint32_t>(bytes, 0x2448, 1);
    Put<uint32_t>(bytes, 0x244c, 0x2460);
    Put<uint32_t>(bytes, 0x2460, 0x2480);
    Put<uint32_t>(bytes, 0x2480, 0x21f0);
    Put<int32_t>(bytes, 0x248c, -1);
    Put<uint64_t>(bytes, 0x2500, base + 0x2400);
    for (unsigned i = 0; i < slot; ++i) Put<uint64_t>(bytes, 0x2508 + 8 * i, base + 0x1200 + shift);
    Put<uint64_t>(bytes, 0x2508 + 8 * slot, base + 0x1000 + shift);
    return bytes;
}

void RejectContracts(std::vector<uint8_t> bytes, const ContractSet& contracts, const char* message) {
    try { ResolveContracts(Image(std::move(bytes)), contracts); Check(false, message); }
    catch (const std::runtime_error&) {}
}
}

int main() {
    Image original(Fixture());
    Instructions first(original);
    Check(first.Rejected().empty(), "authored functions decode completely");
    Check(first.Fingerprints().size() == 2, "direct leaf call is indexed without invented unwind metadata");
    Check(first.Calling(0x1200).size() == 1 && first.Calling(0x1200)[0].owner == 0x1000, "leaf call relationship");
    Check(first.Referencing(0x4000).size() == 1, "RIP-relative global relationship");
    Check(original.FindString("Fixture") == std::vector<uint32_t>{0x2100}, "complete named anchor");
    Check(original.FindString("Fixtur").empty(), "anchor prefix is not a match");
    Image moved(Fixture(0x100, 0x80, 0x180000000ull));
    Instructions second(moved);
    Check(first.Fingerprints().at(0x1000) == second.Fingerprints().at(0x1100),
          "image relocation and function/global movement preserve instruction contract");
    Image changedField(Fixture(0x100, 0x2a0, 0x180000000ull));
    Instructions fields(changedField);
    Check(!(first.Fingerprints().at(0x1000) == fields.Fingerprints().at(0x1100)),
          "unvalidated field movement rejects a fixed layout contract");
    Check(first.Identify(first.Decode(0x1000), {{0, 1}})
          == fields.Identify(fields.Decode(0x1100), {{0, 1}}),
          "only explicitly derived operands may vary");
    Check(second.Referencing(0x4100).size() == 1 && second.Calling(0x1300).size() == 1,
          "relocated references follow the new image");
    auto wrongWidth = Fixture();
    wrongWidth[0x1000] = 0x90;
    Image narrow(std::move(wrongWidth));
    Instructions third(narrow);
    Check(!(first.Fingerprints().at(0x1000) == third.Fingerprints().at(0x1000)),
          "32-bit field load cannot satisfy a 64-bit contract");

    auto duplicate = Fixture();
    Code(duplicate, 0x2200, {'F','i','x','t','u','r','e',0});
    Check(Image(duplicate).FindString("Fixture").size() == 2, "duplicate anchors remain visible to uniqueness check");
    duplicate[0x2100] = 'X';
    duplicate[0x2200] = 'X';
    Check(Image(duplicate).FindString("Fixture").empty(), "missing anchor cannot reuse a previous result");

    auto malformed = Fixture();
    malformed[0] = 0;
    RejectImage(malformed, "bad DOS signature");
    malformed = Fixture();
    Put<uint32_t>(malformed, 0x3c, 0xfffffff0);
    RejectImage(malformed, "overflowed PE header offset");
    malformed = Fixture();
    Put<uint16_t>(malformed, 0x84, 0x14c);
    RejectImage(malformed, "wrong architecture");
    malformed = Fixture();
    Put<uint32_t>(malformed, 0x188 + 36, Image::Readable | Image::Writable);
    RejectImage(malformed, "function outside executable section");
    malformed = Fixture();
    Put<uint32_t>(malformed, 0x188 + 40 + 12, 0x1800);
    RejectImage(malformed, "overlapping sections");
    malformed = Fixture();
    Put<uint32_t>(malformed, 0x124, 11);
    RejectImage(malformed, "partial runtime-function record");
    malformed = Fixture();
    Put<uint32_t>(malformed, 0xd4, 0x190);
    RejectImage(malformed, "section table extends beyond declared headers");
    malformed = Fixture();
    malformed[0x2000] = 0x41;
    RejectImage(malformed, "unknown unwind flags");
    malformed = Fixture();
    malformed[0x2000] = 0x21;
    Put<uint32_t>(malformed, 0x2004, 0x1000);
    Put<uint32_t>(malformed, 0x2008, 0x1014);
    Put<uint32_t>(malformed, 0x200c, 0x2000);
    RejectImage(malformed, "cyclic chained unwind record");

    auto truncated = Fixture();
    Put<uint32_t>(truncated, 0x3004, 0x1011);
    Image truncatedImage(truncated);
    Instructions rejected(truncatedImage);
    Check(rejected.Fingerprints().empty() && !rejected.Rejected().empty(),
          "truncated instruction cannot produce an active fingerprint after earlier success");

    auto branch = Fixture();
    Code(branch, 0x1000, {0x74,0x02,0x48,0x8b,0x81,0,0,0,0,0xc3});
    Put<uint32_t>(branch, 0x3004, 0x100a);
    Image branchImage(branch);
    Instructions badBranch(branchImage);
    Check(badBranch.Fingerprints().empty(), "branch into an instruction rejects the function");

    auto chained = Fixture();
    Code(chained, 0x1000, {0xe9,0xfb,0,0,0});
    Code(chained, 0x1100, {0x8b,0x41,0x20,0xc3});
    Put<uint32_t>(chained, 0x3004, 0x1005);
    Put<uint32_t>(chained, 0x124, 24);
    Put<uint32_t>(chained, 0x300c, 0x1100);
    Put<uint32_t>(chained, 0x3010, 0x1104);
    Put<uint32_t>(chained, 0x3014, 0x2010);
    Code(chained, 0x2010, {0x21,0,0,0});
    Put<uint32_t>(chained, 0x2014, 0x1000);
    Put<uint32_t>(chained, 0x2018, 0x1005);
    Put<uint32_t>(chained, 0x201c, 0x2000);
    Image chainedImage(chained);
    Instructions chains(chainedImage);
    Check(chainedImage.Owner(0x1100) == 0x1000 && chains.Decode(0x1000).size() == 3,
          "chained unwind body resolves to its primary entry");

    auto switchBytes = Fixture();
    Code(switchBytes, 0x1000, {0x4c,0x8d,0x05,0,0,0,0,
                              0x83,0xf9,0x01,0x77,0x24,
                              0x48,0x63,0xc1,0x41,0x8b,0x84,0x80,0,0,0,0,
                              0x4c,0x01,0xc0,0xff,0xe0});
    Put<uint32_t>(switchBytes, 0x1003, 0u - 0x1007);
    Put<uint32_t>(switchBytes, 0x1013, 0x2300);
    Code(switchBytes, 0x1030, {0xb8,0x01,0,0,0,0xc3});
    Code(switchBytes, 0x1040, {0xb8,0x02,0,0,0,0xc3});
    Put<uint32_t>(switchBytes, 0x2300, 0x1030);
    Put<uint32_t>(switchBytes, 0x2304, 0x1040);
    Put<uint32_t>(switchBytes, 0x3004, 0x1046);
    Image switchImage(switchBytes);
    Instructions switchCode(switchImage);
    Check(switchCode.Fingerprints().count(0x1000) == 1, "bounded image-relative switch decodes both cases");
    auto invalidSwitch = switchBytes;
    Put<uint32_t>(invalidSwitch, 0x2304, 0x5000);
    Image invalidSwitchImage(invalidSwitch);
    Check(Instructions(invalidSwitchImage).Fingerprints().empty(), "switch case outside function rejects");
    invalidSwitch = switchBytes;
    Put<uint32_t>(invalidSwitch, 0x2304, 0x100c);
    Image bypassImage(invalidSwitch);
    Check(Instructions(bypassImage).Fingerprints().empty(), "switch target cannot bypass its range guard");
    invalidSwitch = switchBytes;
    invalidSwitch[0x100a] = 0x76;
    Image reversedGuard(invalidSwitch);
    Check(Instructions(reversedGuard).Fingerprints().empty(), "wrong switch guard condition rejects");

    auto tailBytes = Fixture();
    Code(tailBytes, 0x1000, {0x48,0x8b,0x01,0xff,0x60,0x18});
    Put<uint32_t>(tailBytes, 0x3004, 0x1006);
    Image tailImage(tailBytes);
    Check(Instructions(tailImage).Fingerprints().count(0x1000) == 1, "receiver-owned virtual tail call decodes");
    tailBytes[0x1002] = 0x02;
    Image wrongReceiver(tailBytes);
    Check(Instructions(wrongReceiver).Fingerprints().empty(), "virtual tail call from unrelated argument rejects");

    auto file = Fixture();
    const auto fromFile = Image::FromFile(file);
    Check(fromFile.FindString("Fixture") == original.FindString("Fixture"), "file-to-image mapping");
    file.resize(0x4100);
    try { auto bad = Image::FromFile(file); Check(false, "truncated file section"); }
    catch (const std::runtime_error&) {}

    using namespace StarfieldHT;
    const ContractSet contracts{
        {{"Camera", first.Fingerprints().at(0x1000), ".?AVFixture@@", {}},
         {"Helper", first.Fingerprints().at(0x1200), nullptr, {}}},
        {{"Camera", 2, 0, "Helper"}},
        {{"Camera", 1, 1, "Global", 8, 8, Image::Readable | Image::Writable, Image::Executable}},
        {{"Camera", 0, 1, "Field", 8, 8, 0x400, 0}}, {}
    };
    auto resolved = ResolveContracts(Image(TypedFixture()), contracts);
    Check(resolved.slots.at("Camera") == 0 && resolved.members.at("Field") == 0x80
          && resolved.data.at("Global") == 0x4000, "independent RTTI owner, field and call relationships resolve together");
    auto sharedShape = TypedFixture();
    Code(sharedShape, 0x1300, {0x8b,0x41,0x10,0xc3});
    Put<uint32_t>(sharedShape, 0x124, 24);
    Put<uint32_t>(sharedShape, 0x300c, 0x1300);
    Put<uint32_t>(sharedShape, 0x3010, 0x1304);
    Put<uint32_t>(sharedShape, 0x3014, 0x2000);
    resolved = ResolveContracts(Image(sharedShape), contracts);
    Check(resolved.methods.at("Helper") == 0x1200, "owned caller disambiguates identical helper bodies");
    auto disconnected = contracts;
    disconnected.links.clear();
    RejectContracts(sharedShape, disconnected, "identical helpers without call evidence remain ambiguous");
    resolved = ResolveContracts(Image(TypedFixture(0x100, 3)), contracts);
    Check(resolved.slots.at("Camera") == 3 && resolved.methods.at("Camera") == 0x1100
          && resolved.data.at("Global") == 0x4100, "moved virtual slot and addresses are derived from current ownership");
    auto invalidType = TypedFixture();
    invalidType[0x2204] = 'X';
    RejectContracts(invalidType, contracts, "missing type owner rejects discovery");
    invalidType = TypedFixture();
    std::memcpy(invalidType.data() + 0x2600, invalidType.data() + 0x2200, 14);
    RejectContracts(invalidType, contracts, "duplicate type names reject discovery");
    invalidType = TypedFixture();
    Put<uint32_t>(invalidType, 0x2480, 0x2100);
    RejectContracts(invalidType, contracts, "wrong complete owner rejects discovery");
    invalidType = TypedFixture();
    Put<uint64_t>(invalidType, 0x2510, 0x140001000ull);
    RejectContracts(invalidType, contracts, "duplicated virtual slot rejects discovery");
    invalidType = TypedFixture();
    Put<uint32_t>(invalidType, 0x1003, 0x88);
    RejectContracts(invalidType, contracts, "changed unvalidated field rejects complete selection");
    auto wrongMember = contracts;
    wrongMember.members[0].width = 4;
    RejectContracts(TypedFixture(), wrongMember, "member contract checks actual operand width");
    wrongMember = contracts;
    wrongMember.members[0].containingSize = 0x84;
    RejectContracts(TypedFixture(), wrongMember, "member span must fit containing object");
    invalidType = TypedFixture();
    Put<uint32_t>(invalidType, 0x100a, 0x2000 - 0x100e);
    RejectContracts(invalidType, contracts, "global moved into read-only metadata rejects writable contract");
    const cameraunlock::memory::PeFingerprint unknown{1, 0x6000, 2};
    auto complete = [] {
        DiscoveredBuild build;
        build.profile = kGdkProfile_20251129;
        return build;
    };
    auto selected = SelectBuild(unknown, complete);
    Check(selected.discovered.has_value() && !selected.exact && selected.Profile(),
          "real registry accepts a complete result for an unlisted fingerprint");
    selected = SelectBuild(unknown, []() -> DiscoveredBuild { throw std::runtime_error("missing anchor"); });
    Check(!selected.discovered && !selected.Profile() && selected.diagnostic.find("missing anchor") != std::string::npos,
          "failure after successful selection has no stale profile and preserves the diagnostic");
    selected = SelectBuild(unknown, [] { return DiscoveredBuild{}; });
    Check(!selected.Profile(), "unlisted build cannot publish a zero-filled result");
    selected = SelectBuild(kGdkProfile_20251129.fingerprint, complete);
    Check(selected.discovered.has_value(), "known fingerprint still exercises discovery");
    selected = SelectBuild(kGdkProfile_20251129.fingerprint, [] {
        auto build = DiscoveredBuild{};
        build.profile = kGdkProfile_20251129;
        ++build.profile.playerAimPointOffset;
        return build;
    });
    Check(!selected.Profile(), "disagreement with independent historical measurements rejects both routes");
    selected = SelectBuild(kSteamProfile_20251129.fingerprint,
                          []() -> DiscoveredBuild { throw std::runtime_error("unsupported compiler structure"); });
    Check(selected.exact == &kSteamProfile_20251129 && !selected.discovered,
          "exact historical compatibility remains available when discovery cannot resolve");
    std::printf("discovery: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
