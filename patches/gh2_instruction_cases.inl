    case PPC_INST_EQV:
        println("\t{}.u64 = ~({}.u64 ^ {}.u64);", r(insn.operands[0]), r(insn.operands[1]), r(insn.operands[2]));
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.compare<int64_t>({}.s64, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;
    case PPC_INST_CROR:
    case PPC_INST_CRORC:
    {
        constexpr const char* fields[] = {"lt", "gt", "eq", "so"};
        auto bit = [&](uint32_t index) { return fmt::format("{}.{}", cr(index / 4), fields[index % 4]); };
        println("\t{} = bool({}) | {}bool({});", bit(insn.operands[0]), bit(insn.operands[1]), id == PPC_INST_CRORC ? "!" : "", bit(insn.operands[2]));
        break;
    }
    case PPC_INST_LHBRX:
        print("\t{}.u64 = __builtin_bswap16(PPC_LOAD_U16(", r(insn.operands[0]));
        if (insn.operands[1] != 0) print("{}.u32 + ", r(insn.operands[1]));
        println("{}.u32));", r(insn.operands[2]));
        break;
    case PPC_INST_MULHD:
        println("\t{}.u64 = gh2::multiply_high_signed({}.s64, {}.s64);", r(insn.operands[0]), r(insn.operands[1]), r(insn.operands[2]));
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.compare<int64_t>({}.s64, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;
    case PPC_INST_VSPLTISH:
        println("\tsimde_mm_store_si128((simde__m128i*){}.u16, simde_mm_set1_epi16(int16_t({})));", v(insn.operands[0]), int32_t(insn.operands[1]));
        break;
    case PPC_INST_VSLH:
    case PPC_INST_VSRH:
    case PPC_INST_VSRAH:
    case PPC_INST_VSRAB:
    case PPC_INST_VRLH:
    {
        const char* operation = id == PPC_INST_VSLH ? "Left" : id == PPC_INST_VSRH ? "LogicalRight" : id == PPC_INST_VRLH ? "RotateLeft" : "ArithmeticRight";
        println("\t{} = gh2::shift<{}, gh2::Shift::{}>({}, {});", v(insn.operands[0]), id == PPC_INST_VSRAB ? "uint8_t" : "uint16_t", operation, v(insn.operands[1]), v(insn.operands[2]));
        break;
    }
    case PPC_INST_VADDSBS:
    case PPC_INST_VADDSWS:
    case PPC_INST_VSUBSHS:
        println("\t{} = gh2::saturating<{}, {}>({}, {}, ctx.vscr);", v(insn.operands[0]), id == PPC_INST_VADDSBS ? "int8_t" : id == PPC_INST_VADDSWS ? "int32_t" : "int16_t", id == PPC_INST_VSUBSHS ? "false" : "true", v(insn.operands[1]), v(insn.operands[2]));
        break;
    case PPC_INST_VPKSWSS:
    case PPC_INST_VPKSWUS:
    case PPC_INST_VPKSHSS:
    case PPC_INST_VPKUHUS:
    {
        const char* outType = id == PPC_INST_VPKSWSS ? "int16_t" : id == PPC_INST_VPKSWUS ? "uint16_t" : id == PPC_INST_VPKSHSS ? "int8_t" : "uint8_t";
        const char* inType = id == PPC_INST_VPKSWSS || id == PPC_INST_VPKSWUS ? "int32_t" : id == PPC_INST_VPKSHSS ? "int16_t" : "uint16_t";
        println("\t{} = gh2::pack<{}, {}>({}, {}, ctx.vscr);", v(insn.operands[0]), outType, inType, v(insn.operands[1]), v(insn.operands[2]));
        break;
    }
    case PPC_INST_VCTUXS:
    case PPC_INST_VCFPUXWS128:
        printSetFlushMode(true);
        println("\t{} = gh2::float_to_unsigned({}, {}, ctx.vscr);", v(insn.operands[0]), v(insn.operands[1]), insn.operands[2]);
        break;
    case PPC_INST_VSUBUBM:
        println("\tsimde_mm_store_si128((simde__m128i*){}.u8, simde_mm_sub_epi8(simde_mm_load_si128((simde__m128i*){}.u8), simde_mm_load_si128((simde__m128i*){}.u8)));", v(insn.operands[0]), v(insn.operands[1]), v(insn.operands[2]));
        break;
    case PPC_INST_VMAXSH:
    case PPC_INST_VMINSH:
        println("\tsimde_mm_store_si128((simde__m128i*){}.u16, simde_mm_{}_epi16(simde_mm_load_si128((simde__m128i*){}.u16), simde_mm_load_si128((simde__m128i*){}.u16)));", v(insn.operands[0]), id == PPC_INST_VMAXSH ? "max" : "min", v(insn.operands[1]), v(insn.operands[2]));
        break;
    case PPC_INST_VAVGUH:
        println("\tsimde_mm_store_si128((simde__m128i*){}.u16, simde_mm_avg_epu16(simde_mm_load_si128((simde__m128i*){}.u16), simde_mm_load_si128((simde__m128i*){}.u16)));", v(insn.operands[0]), v(insn.operands[1]), v(insn.operands[2]));
        break;
    case PPC_INST_VCMPEQUH:
    case PPC_INST_VCMPGTSH:
    case PPC_INST_VCMPGTSW:
        println("\tsimde_mm_store_si128((simde__m128i*){}.u8, simde_mm_{}(simde_mm_load_si128((simde__m128i*){}.u8), simde_mm_load_si128((simde__m128i*){}.u8)));", v(insn.operands[0]), id == PPC_INST_VCMPEQUH ? "cmpeq_epi16" : id == PPC_INST_VCMPGTSH ? "cmpgt_epi16" : "cmpgt_epi32", v(insn.operands[1]), v(insn.operands[2]));
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.setFromMask(simde_mm_load_si128((simde__m128i*){}.u8), 0xFFFF);", cr(6), v(insn.operands[0]));
        break;
