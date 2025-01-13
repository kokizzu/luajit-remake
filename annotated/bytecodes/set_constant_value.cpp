#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN SetConstInt16Impl(int16_t value)
{
    // TODO: think about supporting int32 later
    //
    Return(TValue::Create<tDouble>(value));
}

DEEGEN_DEFINE_BYTECODE(SetConstInt16)
{
    Operands(
        Literal<int16_t>("value")
    );
    Result(BytecodeValue);
    Implementation(SetConstInt16Impl);
    Variant();
    DfgVariant();
    TypeDeductionRule(AlwaysOutput<tDouble>);
    RegAllocHint(Op("output").RegHint(RegHint::FPR));
}

DEEGEN_END_BYTECODE_DEFINITIONS
