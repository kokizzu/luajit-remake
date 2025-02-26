#include "fps_main.h"
#include "read_file.h"
#include "transactional_output_file.h"
#include "llvm/IRReader/IRReader.h"
#include "deegen_process_bytecode_definition_for_interpreter.h"
#include "deegen_ast_return.h"
#include "base64_util.h"
#include "json_parse_dump.h"

#include "json_utils.h"

using namespace dast;

void FPS_ProcessBytecodeDefinitionForInterpreter()
{
    using namespace llvm;
    std::unique_ptr<LLVMContext> llvmCtxHolder = std::make_unique<llvm::LLVMContext>();
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::string inputFileName = cl_irInputFilename;
    ReleaseAssert(inputFileName != "");

    SMDiagnostic llvmErr;
    std::unique_ptr<Module> moduleIn = parseIRFile(inputFileName, llvmErr, ctx);
    if (moduleIn == nullptr)
    {
        fprintf(stderr, "[INTERNAL ERROR] Bitcode for %s cannot be read or parsed.\n", inputFileName.c_str());
        abort();
    }

    ProcessBytecodeDefinitionForInterpreterResult result = ProcessBytecodeDefinitionForInterpreter(std::move(moduleIn));

    TransactionalOutputFile hdrOutFile(cl_headerOutputFilename);
    FPS_EmitHeaderFileCommonHeader(hdrOutFile.fp());

    fprintf(hdrOutFile.fp(), "#include \"drt/bytecode_builder_utils.h\"\n\n");
    fprintf(hdrOutFile.fp(), "namespace DeegenBytecodeBuilder {\n\n");
    hdrOutFile.write(result.m_generatedHeaderFile);
    fprintf(hdrOutFile.fp(), "\n} /*namespace DeegenBytecodeBuilder*/\n");

    TransactionalOutputFile jsonOutFile(cl_jsonOutputFilename);
    json_t j;
    j["header-name"] = FPS_GetFileNameFromAbsolutePath(cl_headerOutputFilename);
    j["class-names"] = result.m_generatedClassNames;
    j["cdecl-names"] = result.m_allExternCDeclarations;
    j["all-bytecode-info"] = std::move(result.m_bytecodeInfoJson);
    j["all-dfg-variant-info"] = std::move(result.m_dfgVariantInfoJson);
    j["return-cont-name-list"] = result.m_returnContinuationNameList;
    j["reference_module"] = base64_encode(DumpLLVMModuleAsString(result.m_referenceModule.get()));
    std::vector<std::string> bytecodeModuleList;
    for (auto& m : result.m_bytecodeModules)
    {
        bytecodeModuleList.push_back(base64_encode(DumpLLVMModuleAsString(m.get())));
    }
    j["bytecode_module_list"] = std::move(bytecodeModuleList);
    j["extra_dfg_func_stubs"] = std::move(result.m_dfgFunctionStubs);

    jsonOutFile.write(SerializeJsonWithIndent(j, 4 /*indent*/));

    for (auto& it : result.m_auditFiles)
    {
        std::string fileName = it.first;
        std::string& content = it.second;
        TransactionalOutputFile auditFile(FPS_GetAuditFilePath(fileName));
        auditFile.write(content);
        auditFile.Commit();
    }

    {
        ReleaseAssert(cl_curSrcFileName != "");
        std::string srcName = cl_curSrcFileName;
        size_t lastDot = srcName.find_last_of('.');
        if (lastDot != std::string::npos)
        {
            srcName = srcName.substr(0, lastDot);
        }
        std::string fileName = "prediction_propagation_" + srcName + ".s";
        std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("dfg_jit" /*dirSuffix*/, fileName);
        TransactionalOutputFile auditFile(auditFilePath);
        auditFile.write(result.m_dfgPredictionPropagationFnAuditFile);
        auditFile.Commit();
    }

    hdrOutFile.Commit();
    jsonOutFile.Commit();
}

void FPS_GenerateBytecodeBuilderAPIHeader()
{
    std::vector<std::string> jsonFileNameList = ParseCommaSeparatedFileList(cl_inputListFilenames);
    std::vector<json_t> jlist;
    for (std::string& filename : jsonFileNameList)
    {
        jlist.push_back(ParseJsonFromFileName(filename));
    }

    TransactionalOutputFile hdrOutFile(cl_headerOutputFilename);
    FPS_EmitHeaderFileCommonHeader(hdrOutFile.fp());

    fprintf(hdrOutFile.fp(), "#include \"drt/bytecode_builder_utils.h\"\n\n");
    for (json_t& j : jlist)
    {
        std::string includeName = JSONCheckedGet<std::string>(j, "header-name");
        fprintf(hdrOutFile.fp(), "#include \"%s\"\n", includeName.c_str());
    }

    fprintf(hdrOutFile.fp(), "\n");

    std::vector<std::string> allClassNames;
    for (json_t& j : jlist)
    {
        ReleaseAssert(j.count("class-names") && j["class-names"].is_array());
        ReleaseAssert(j.count("cdecl-names") && j["cdecl-names"].is_array());
        ReleaseAssert(j["class-names"].size() == j["cdecl-names"].size());

        for (auto& e : j["class-names"])
        {
            ReleaseAssert(e.is_string());
            allClassNames.push_back(e.get<std::string>());
        }
    }

    {
        std::unordered_set<std::string> checkUnique;
        for (auto& name : allClassNames)
        {
            ReleaseAssert(!checkUnique.count(name));
            checkUnique.insert(name);
        }
    }

    fprintf(hdrOutFile.fp(), "#define GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES ");
    for (size_t i = 0; i < allClassNames.size(); i++)
    {
        fprintf(hdrOutFile.fp(), "\\\n");
        if (i > 0)
        {
            fprintf(hdrOutFile.fp(), ", ");
        }
        else
        {
            fprintf(hdrOutFile.fp(), "  ");
        }
        std::string prefixToRemove = "DeegenGenerated_BytecodeBuilder_";
        ReleaseAssert(allClassNames[i].starts_with(prefixToRemove));
        fprintf(hdrOutFile.fp(), "%s ", allClassNames[i].substr(prefixToRemove.length()).c_str());
    }

    fprintf(hdrOutFile.fp(), "\n\n\n");

    TransactionalOutputFile cppOutFile(cl_cppOutputFilename);

    // This is a bit hacky and risky, but it should be fine, since the file itself is generated,
    // and it only depends on the bytecode builder header files, which have been generated by now
    //
    fprintf(cppOutFile.fp(), "#define DEEGEN_POST_FUTAMURA_PROJECTION\n");

    fprintf(cppOutFile.fp(), "#pragma clang diagnostic push\n");
    fprintf(cppOutFile.fp(), "#pragma clang diagnostic ignored \"-Wreserved-identifier\"\n");

    fprintf(cppOutFile.fp(), "#include \"drt/bytecode_builder.h\"\n\n");

    size_t totalSize = 0;
    for (json_t& j : jlist)
    {
        for (auto& arr : j["cdecl-names"])
        {
            ReleaseAssert(arr.is_array());
            for (auto& x : arr)
            {
                ReleaseAssert(x.is_string());
                std::string s = x.get<std::string>();
                fprintf(cppOutFile.fp(), "extern \"C\" void %s();\n", s.c_str());
                totalSize++;
            }
        }
    }

    fprintf(cppOutFile.fp(), "\nnamespace DeegenBytecodeBuilder {\n\n");

    fprintf(cppOutFile.fp(), "class DeegenInterpreterDispatchTableBuilder {\n");
    fprintf(cppOutFile.fp(), "public:\n");
    fprintf(cppOutFile.fp(), "    static constexpr auto get() {\n");
    fprintf(cppOutFile.fp(), "        using FnPtr = void(*)();\n");
    fprintf(cppOutFile.fp(), "        std::array<FnPtr, %u> r;\n", SafeIntegerCast<unsigned int>(totalSize));
    fprintf(cppOutFile.fp(), "        for (size_t i = 0; i < %u; i++) {\n", SafeIntegerCast<unsigned int>(totalSize));
    fprintf(cppOutFile.fp(), "            r[i] = nullptr;\n");
    fprintf(cppOutFile.fp(), "        }\n");

    for (json_t& j : jlist)
    {
        ReleaseAssert(j.count("class-names") && j["class-names"].is_array());
        ReleaseAssert(j.count("cdecl-names") && j["cdecl-names"].is_array());
        ReleaseAssert(j["class-names"].size() == j["cdecl-names"].size());

        size_t len = j["class-names"].size();
        for (size_t i = 0; i < len; i++)
        {
            ReleaseAssert(j["class-names"][i].is_string());
            std::string className = j["class-names"][i].get<std::string>();

            ReleaseAssert(j["cdecl-names"][i].is_array());
            auto& arr = j["cdecl-names"][i];
            std::vector<std::string> lis;
            for (auto& x : arr)
            {
                ReleaseAssert(x.is_string());
                lis.push_back(x.get<std::string>());
            }

            fprintf(cppOutFile.fp(), "        {\n");
            fprintf(cppOutFile.fp(), "            static_assert(BytecodeBuilder::GetNumVariantsOfBytecode<%s>() == %u);\n", className.c_str(), SafeIntegerCast<unsigned int>(lis.size()));
            fprintf(cppOutFile.fp(), "            constexpr size_t base = BytecodeBuilder::GetBytecodeOpcodeBase<%s>();\n", className.c_str());
            for (size_t k = 0; k < lis.size(); k++)
            {
                fprintf(cppOutFile.fp(), "            ReleaseAssert(r[base + %d] == nullptr);\n", static_cast<int>(k));
            }
            for (size_t k = 0; k < lis.size(); k++)
            {
                fprintf(cppOutFile.fp(), "            r[base + %d] = %s;\n", static_cast<int>(k), lis[k].c_str());
            }
            fprintf(cppOutFile.fp(), "        }\n");
        }
    }

    fprintf(cppOutFile.fp(), "        for (size_t i = 0; i < %u; i++) {\n", SafeIntegerCast<unsigned int>(totalSize));
    fprintf(cppOutFile.fp(), "            ReleaseAssert(r[i] != nullptr);\n");
    fprintf(cppOutFile.fp(), "        }\n");
    fprintf(cppOutFile.fp(), "        return r;\n");
    fprintf(cppOutFile.fp(), "    }\n");
    fprintf(cppOutFile.fp(), "};\n");

    fprintf(cppOutFile.fp(), "\n} /*namespace DeegenBytecodeBuilder*/\n\n");

    fprintf(cppOutFile.fp(), "using DispatchTableEntryT = void(*)();\n");
    fprintf(cppOutFile.fp(), "static_assert(sizeof(DispatchTableEntryT) == 8);\n");
    fprintf(cppOutFile.fp(), "constexpr std::array<DispatchTableEntryT, %u> x_tmpDispatchTable = DeegenBytecodeBuilder::DeegenInterpreterDispatchTableBuilder::get();\n", SafeIntegerCast<unsigned int>(totalSize));

    for (size_t i = 0; i < totalSize; i++)
    {
        fprintf(cppOutFile.fp(), "constexpr DispatchTableEntryT x_dispatchTable_entry%u = x_tmpDispatchTable[%u];\n", SafeIntegerCast<unsigned int>(i), SafeIntegerCast<unsigned int>(i));
        fprintf(cppOutFile.fp(), "static_assert(x_dispatchTable_entry%u != nullptr);\n", SafeIntegerCast<unsigned int>(i));
    }

    fprintf(cppOutFile.fp(), "\n");
    fprintf(cppOutFile.fp(), "extern \"C\" const DispatchTableEntryT %s[%u];\n", x_deegen_interpreter_dispatch_table_symbol_name, SafeIntegerCast<unsigned int>(totalSize));
    fprintf(cppOutFile.fp(), "extern \"C\" const DispatchTableEntryT %s[%u] = {\n", x_deegen_interpreter_dispatch_table_symbol_name, SafeIntegerCast<unsigned int>(totalSize));

    for (size_t i = 0; i < totalSize; i++)
    {
        if (i > 0)
        {
            fprintf(cppOutFile.fp(), ",\n");
        }
        fprintf(cppOutFile.fp(), "    x_dispatchTable_entry%u", SafeIntegerCast<unsigned int>(i));
    }
    fprintf(cppOutFile.fp(), "\n};\n");

    fprintf(cppOutFile.fp(), "#pragma clang diagnostic pop\n");

    // This is even more hacky.. We want to generate the list of bytecodes in the same order as the dispatching array.
    // To do this, we generate a CPP file that includes bytecode_builder.h, then compile it into an executable and execute it..
    //
    TransactionalOutputFile cppOutFile2(cl_cppOutputFilename2);

    fprintf(cppOutFile2.fp(), "#define DEEGEN_POST_FUTAMURA_PROJECTION\n");
    fprintf(cppOutFile2.fp(), "#include \"drt/bytecode_builder.h\"\n\n");

    fprintf(cppOutFile2.fp(), "\nnamespace DeegenBytecodeBuilder {\n\n");

    fprintf(cppOutFile2.fp(), "class DeegenInterpreterOpcodeNameTableBuilder {\n");
    fprintf(cppOutFile2.fp(), "public:\n");
    fprintf(cppOutFile2.fp(), "    static constexpr auto get() {\n");
    fprintf(cppOutFile2.fp(), "        std::array<const char*, %u> r;\n", SafeIntegerCast<unsigned int>(totalSize));
    fprintf(cppOutFile2.fp(), "        for (size_t i = 0; i < %u; i++) {\n", SafeIntegerCast<unsigned int>(totalSize));
    fprintf(cppOutFile2.fp(), "            r[i] = nullptr;\n");
    fprintf(cppOutFile2.fp(), "        }\n");

    for (json_t& j : jlist)
    {
        ReleaseAssert(j.count("class-names") && j["class-names"].is_array());
        ReleaseAssert(j.count("cdecl-names") && j["cdecl-names"].is_array());
        ReleaseAssert(j["class-names"].size() == j["cdecl-names"].size());

        size_t len = j["class-names"].size();
        for (size_t i = 0; i < len; i++)
        {
            ReleaseAssert(j["class-names"][i].is_string());
            std::string className = j["class-names"][i].get<std::string>();

            ReleaseAssert(j["cdecl-names"][i].is_array());
            auto& arr = j["cdecl-names"][i];
            std::vector<std::string> lis;
            for (auto& x : arr)
            {
                ReleaseAssert(x.is_string());
                std::string val = x.get<std::string>();
                ReleaseAssert(val.starts_with("__deegen_interpreter_op_"));
                val = val.substr(strlen("__deegen_interpreter_op_"));
                lis.push_back(val);
            }

            fprintf(cppOutFile2.fp(), "        {\n");
            fprintf(cppOutFile2.fp(), "            static_assert(BytecodeBuilder::GetNumVariantsOfBytecode<%s>() == %u);\n", className.c_str(), SafeIntegerCast<unsigned int>(lis.size()));
            fprintf(cppOutFile2.fp(), "            constexpr size_t base = BytecodeBuilder::GetBytecodeOpcodeBase<%s>();\n", className.c_str());
            for (size_t k = 0; k < lis.size(); k++)
            {
                fprintf(cppOutFile2.fp(), "            ReleaseAssert(r[base + %d] == nullptr);\n", static_cast<int>(k));
            }
            for (size_t k = 0; k < lis.size(); k++)
            {
                fprintf(cppOutFile2.fp(), "            r[base + %d] = \"%s\";\n", static_cast<int>(k), lis[k].c_str());
            }
            fprintf(cppOutFile2.fp(), "        }\n");
        }
    }

    fprintf(cppOutFile2.fp(), "        for (size_t i = 0; i < %u; i++) {\n", SafeIntegerCast<unsigned int>(totalSize));
    fprintf(cppOutFile2.fp(), "            ReleaseAssert(r[i] != nullptr);\n");
    fprintf(cppOutFile2.fp(), "        }\n");
    fprintf(cppOutFile2.fp(), "        return r;\n");
    fprintf(cppOutFile2.fp(), "    }\n");
    fprintf(cppOutFile2.fp(), "};\n");

    fprintf(cppOutFile2.fp(), "\n} /*namespace DeegenBytecodeBuilder*/\n\n");

    fprintf(cppOutFile2.fp(), "int main(int argc, char** argv) {\n");
    fprintf(cppOutFile2.fp(), "    ReleaseAssert(argc == 2);\n");
    fprintf(cppOutFile2.fp(), "    FILE* fp = fopen(argv[1], \"w\");\n");
    fprintf(cppOutFile2.fp(), "    ReleaseAssert(fp != nullptr);\n");
    fprintf(cppOutFile2.fp(), "    fprintf(fp, \"[\\n\");\n");
    fprintf(cppOutFile2.fp(), "    auto v = DeegenBytecodeBuilder::DeegenInterpreterOpcodeNameTableBuilder::get();\n");
    fprintf(cppOutFile2.fp(), "    for (size_t i = 0; i < v.size(); i++) {\n");
    fprintf(cppOutFile2.fp(), "%s", "        fprintf(fp, \"\\\"%s\\\"\", v[i]);\n");
    fprintf(cppOutFile2.fp(), "        if (i + 1 < v.size()) fprintf(fp, \",\");\n");
    fprintf(cppOutFile2.fp(), "        fprintf(fp, \"\\n\");\n");
    fprintf(cppOutFile2.fp(), "    }\n");
    fprintf(cppOutFile2.fp(), "    fprintf(fp, \"]\\n\");\n");
    fprintf(cppOutFile2.fp(), "    fclose(fp);\n");
    fprintf(cppOutFile2.fp(), "    return 0;\n");
    fprintf(cppOutFile2.fp(), "}\n");

    hdrOutFile.Commit();
    cppOutFile.Commit();
    cppOutFile2.Commit();
}
