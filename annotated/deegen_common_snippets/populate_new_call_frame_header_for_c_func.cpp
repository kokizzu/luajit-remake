#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_PopulateNewCallFrameHeaderForCallFromCFunc(void* newStackBase, void* oldStackBase, void* onReturn)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(newStackBase);
    hdr->m_retAddr = onReturn;
    hdr->m_caller = oldStackBase;
    hdr->m_callerBytecodePtr = 0;
    hdr->m_numVariadicArguments = 0;
}

DEFINE_DEEGEN_COMMON_SNIPPET("PopulateNewCallFrameHeaderForCallFromCFunc", DeegenSnippet_PopulateNewCallFrameHeaderForCallFromCFunc)
