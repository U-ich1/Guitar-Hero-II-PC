#include <function.h>
#include <vector>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>

void check(const char* name, std::initializer_list<uint32_t> instructions, size_t expected, const IndirectBranchTargets* targets = nullptr) {
    std::vector<uint8_t> bytes;
    for (auto word : instructions) for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(word >> shift);
    auto fn = Function::Analyze(bytes.data(), bytes.size(), 0x1000, targets);
    if (fn.size != expected) {
        fprintf(stderr, "%s: size %zu, expected %zu\n", name, fn.size, expected);
        exit(1);
    }
}
int main() {
    check("empty", {}, 0);
    check("single return bounded read", {0x4E800020}, 4);
    check("call fallthrough", {0x48000005,0x4E800020}, 8);
    check("backward self loop terminates", {0x48000000}, 4);
    check("branch outside section", {0x48000040}, 4);
    check("absolute branch outside section", {0x48000002}, 4);
    check("conditional return falls through", {0x4D820020,0x38600001,0x4E800020}, 12);
    check("two conditional paths", {0x4182000C,0x60000000,0x4E800020,0x38600001,0x4E800020}, 20);
    check("unknown indirect branch", {0x4E800420,0x38600001,0x4E800020}, 4);
    IndirectBranchTargets targets{{0x1000,{0x1004,0x100C,0x2000}}};
    check("switch follows every in-range case", {0x4E800420,0x38600001,0x4E800020,0x38600002,0x4E800020}, 20, &targets);
    check("padding still advances scanner", {0}, 4);
    printf("11 function-analysis regressions passed.\n");
}
