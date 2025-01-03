#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

// Returns the new total number of args
//
static uint64_t DeegenSnippet_CopyVariadicResultsToArgumentsForVariadicNotInPlaceTailCall(bool alreadyMoved, size_t totalNumArgs, uint64_t* stackBase, CoroutineRuntimeContext* coroCtx)
{
    uint32_t num = coroCtx->m_numVariadicRets;
    if (!alreadyMoved)
    {
        uint64_t* dst = stackBase + totalNumArgs;
        uint64_t* src = reinterpret_cast<uint64_t*>(coroCtx->m_variadicRetStart);
        memmove(dst, src, sizeof(uint64_t) * num);
    }
    return totalNumArgs + num;
}

DEFINE_DEEGEN_COMMON_SNIPPET("CopyVariadicResultsToArgumentsForVariadicNotInPlaceTailCall", DeegenSnippet_CopyVariadicResultsToArgumentsForVariadicNotInPlaceTailCall)

