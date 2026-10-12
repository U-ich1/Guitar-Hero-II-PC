#include <disasm.h>
#include <image.h>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
    auto image = Image::ParseImage(data.data(), data.size());
    size_t address = strtoull(argv[2], nullptr, 16);
    size_t count = strtoull(argv[3], nullptr, 10);
    for (size_t i = 0; i < count; ++i, address += 4) {
        const auto* bytes = static_cast<const uint8_t*>(image.Find(address));
        if (!bytes) return 3;
        ppc_insn insn{};
        ppc::Disassemble(bytes, address, insn);
        printf("%08llX: %02X%02X%02X%02X  %s %s\n", (unsigned long long)address,
               bytes[0], bytes[1], bytes[2], bytes[3], insn.opcode ? insn.opcode->name : "?", insn.op_str);
    }
}
