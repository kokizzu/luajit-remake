#pragma once

#include "misc_llvm_helper.h"
#include "deegen_bytecode_ir_components.h"
#include "deegen_bytecode_impl_creator_base.h"

namespace dast {

class BytecodeVariantDefinition;

class InterpreterBytecodeImplCreator final : public DeegenBytecodeImplCreatorBase
{
public:
    // This clones the input module, so that the input module is untouched
    //
    InterpreterBytecodeImplCreator(BytecodeIrComponent& bic);

    virtual DeegenEngineTier WARN_UNUSED GetTier() const override { return DeegenEngineTier::Interpreter; }

    // Inline 'impl' into the wrapper logic, then lower APIs like 'Return', 'MakeCall', 'Error', etc.
    //
    void DoLowering();

    static std::unique_ptr<InterpreterBytecodeImplCreator> WARN_UNUSED LowerOneComponent(BytecodeIrComponent& bic);
    static std::unique_ptr<llvm::Module> WARN_UNUSED DoLoweringForAll(BytecodeIrInfo& bi);

    llvm::Value* GetCondBrDest() const { return m_valuePreserver.Get(x_condBrDest); }
    llvm::Value* GetBytecodeMetadataPtr() const { return m_valuePreserver.Get(x_metadataPtr); }
    llvm::Value* GetInterpreterCodeBlock() const { return m_valuePreserver.Get(x_interpreterCodeBlock); }

    std::string WARN_UNUSED GetResultFunctionName() { return m_resultFuncName; }

    static constexpr const char* x_hot_code_section_name = "deegen_interpreter_code_section_hot";
    static constexpr const char* x_cold_code_section_name = "deegen_interpreter_code_section_cold";

private:
    void CreateWrapperFunction();
    void LowerGetBytecodeMetadataPtrAPI();

    llvm::Function* m_impl;
    llvm::Function* m_wrapper;

    // The name of the wrapper function (i.e., the final product)
    //
    std::string m_resultFuncName;

    bool m_generated;

    // True if this component may fallthrough to the next bytecode
    //
    bool m_mayFallthroughToNextBytecode;

    // True if this component may make a tail call
    //
    bool m_mayMakeTailCall;

    static constexpr const char* x_condBrDest = "condBrDest";
    static constexpr const char* x_metadataPtr = "metadataPtr";
    static constexpr const char* x_interpreterCodeBlock = "codeBlock";
};

}   // namespace dast
