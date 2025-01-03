#include "bytecode_builder.h"
#include "runtime_utils.h"

namespace DeegenBytecodeBuilder {

uint32_t WARN_UNUSED PatchBytecodeMetadataFields(RestrictPtr<uint8_t> bytecodeStart, size_t bytecodeLen, const uint16_t* numOfEachMetadataKind, const std::vector<MetadataFieldPatchRecord>& patchList)
{
    // Note that the logic below that computes the offset for each metadata struct must
    // agree with the logic that iterates the metadata structs from the CodeBlock
    //
    uint32_t cbTrailingArrayOffset = static_cast<uint32_t>(CodeBlock::GetTrailingArrayOffset());
    Assert(cbTrailingArrayOffset % 8 == 0);
    uint32_t curOffset = cbTrailingArrayOffset + RoundUpToMultipleOf<8>(static_cast<uint32_t>(bytecodeLen));

    // TODO: ideally we should store the metadata structs in increasing order of alignment to minimize padding
    //
    uint32_t baseOffset[x_num_bytecode_metadata_struct_kinds];
    for (size_t msKind = 0; msKind < x_num_bytecode_metadata_struct_kinds; msKind++)
    {
        size_t log2Align = x_bytecode_metadata_struct_log_2_alignment_list[msKind];
        // Currently, since the CodeBlock is aligned by 8 bytes and the metadata is stored as a trailing array after the CodeBlock,
        // the largest alignment we can support for the metadata struct is also 8 bytes
        //
        Assert(log2Align <= 3);

        // Round up the offset so that it is aligned
        //
        uint32_t alignment = 1U << log2Align;
        curOffset = (curOffset + alignment - 1) >> log2Align << log2Align;
        Assert(curOffset % alignment == 0);

        baseOffset[msKind] = curOffset;
        curOffset += x_bytecode_metadata_struct_size_list[msKind] * numOfEachMetadataKind[msKind];
    }

    uint32_t trailingArraySize = RoundUpToMultipleOf<8>(curOffset) - cbTrailingArrayOffset;
    Assert(trailingArraySize % 8 == 0);

    for (auto& patch : patchList)
    {
        uint8_t* patchLoc = bytecodeStart + patch.bytecodeOffset;
        Assert(patch.typeOrd < x_num_bytecode_metadata_struct_kinds);
        Assert(patch.index < numOfEachMetadataKind[patch.typeOrd]);
        uint32_t offset = baseOffset[patch.typeOrd] + x_bytecode_metadata_struct_size_list[patch.typeOrd] * patch.index;
        Assert(offset % (1U << x_bytecode_metadata_struct_log_2_alignment_list[patch.typeOrd]) == 0);
        UnalignedStore<uint32_t>(patchLoc, offset);
    }
    return trailingArraySize;
}

BytecodeDecoder::BytecodeDecoder(CodeBlock* cb)
    : BytecodeAccessor<true /*isDecodingMode*/>(
          cb->GetBytecodeStream(),
          cb->GetBytecodeLength(),
          cb->GetConstantTableEnd())
{ }

}   // DeegenBytecodeBuilder
