#pragma once

#include "common_utils.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

namespace dast {

// Inlining makes analysis and optimization easier,
// but since our API constructs are expressed as function calls,
// inlining them would also lose those high-level structural information.
//
// So we introduce this DesugaringLevel thing. Each API construct is associated with a level.
// An API function may be inlined only if the level of the inliner >= the level of the construct.
//
// We will run the inliner multiple times from low DesugaringLevel to high DesugaringLevel to gradually desugar our API constructs,
// so each analysis/optimization pass can be run at its best abstraction level.
//
enum class DesugaringLevel
{
    // Nothing is inlined
    //
    Bottom,
    // AlwaysInline functions are inlined
    //
    AlwaysInline,
    // General (non-API) functions may be inlined
    //
    GeneralFunctions,
    // TValue-related APIs are inlined
    //
    TypeSpecialization,
    // Everything is inlined
    // Note that this does not inline any more stuffs than the level right above it,
    // but having this value allows us to uniformly specify the highest level
    //
    Top
};

inline DesugaringLevel DesugarUpToExcluding(DesugaringLevel target)
{
    ReleaseAssert(target != DesugaringLevel::Bottom && target != DesugaringLevel::Top);
    return static_cast<DesugaringLevel>(static_cast<int>(target) - 1);
}

enum class DesugarDecision
{
    MustInline,
    DontCare,
    MustNotInline
};

using DesugarDecisionFn = DesugarDecision(*)(llvm::Function* func, DesugaringLevel level);

void AddLLVMInliningAttributesForDesugaringLevel(llvm::Module* module, DesugaringLevel level);

}   // namespace dast