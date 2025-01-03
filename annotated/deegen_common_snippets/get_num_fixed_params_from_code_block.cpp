#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t DeegenSnippet_GetNumFixedParamsFromCodeBlock(CodeBlock* cb)
{
    return cb->m_numFixedArguments;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetNumFixedParamsFromCodeBlock", DeegenSnippet_GetNumFixedParamsFromCodeBlock)

