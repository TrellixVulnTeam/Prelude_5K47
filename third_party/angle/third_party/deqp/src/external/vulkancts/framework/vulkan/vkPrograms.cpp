/*-------------------------------------------------------------------------
 * Vulkan CTS Framework
 * --------------------
 *
 * Copyright (c) 2015 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *//*!
 * \file
 * \brief Program utilities.
 *//*--------------------------------------------------------------------*/

#if defined(DEQP_HAVE_SPIRV_TOOLS)
#include "spirv-tools/optimizer.hpp"
#endif

#include "qpInfo.h"

#include "vkPrograms.hpp"
#include "vkShaderToSpirV.hpp"
#include "vkSpirVAsm.hpp"
#include "vkRefUtil.hpp"

#include "deMutex.hpp"
#include "deFilePath.hpp"
#include "deArrayUtil.hpp"
#include "deMemory.h"
#include "deInt32.h"

#include "tcuCommandLine.hpp"

#include <map>

namespace vk
{

using std::string;
using std::vector;
using std::map;

#if defined(DE_DEBUG) && defined(DEQP_HAVE_SPIRV_TOOLS)
#	define VALIDATE_BINARIES	true
#else
#	define VALIDATE_BINARIES	false
#endif

#define SPIRV_BINARY_ENDIANNESS DE_LITTLE_ENDIAN

// ProgramBinary

ProgramBinary::ProgramBinary (ProgramFormat format, size_t binarySize, const deUint8* binary)
	: m_format	(format)
	, m_binary	(binary, binary+binarySize)
{
}

// Utils

namespace
{

#if defined(DEQP_HAVE_SPIRV_TOOLS)

void optimizeCompiledBinary (vector<deUint32>& binary, int optimizationRecipe, const SpirvVersion spirvVersion)
{
	spv_target_env targetEnv = SPV_ENV_VULKAN_1_0;

	// Map SpirvVersion with spv_target_env:
	switch (spirvVersion)
	{
		case SPIRV_VERSION_1_0: targetEnv = SPV_ENV_VULKAN_1_0;	break;
		case SPIRV_VERSION_1_1:
		case SPIRV_VERSION_1_2:
		case SPIRV_VERSION_1_3: targetEnv = SPV_ENV_VULKAN_1_1;	break;
		default:
			TCU_THROW(InternalError, "Unexpected SPIR-V version requested");
	}

	spvtools::Optimizer optimizer(targetEnv);

	switch (optimizationRecipe)
	{
		case 1:
			// The example recipe from:
			// https://www.lunarg.com/wp-content/uploads/2017/08/SPIR-V-Shader-Size-Reduction-Using-spirv-opt_v1.0.pdf
			optimizer.RegisterPass(spvtools::CreateInlineExhaustivePass());				// --inline-entry-points-exhaustive
			optimizer.RegisterPass(spvtools::CreateLocalAccessChainConvertPass());		// --convert-local-access-chains
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());// --eliminate-local-single-block
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());			// --eliminate-local-single-store
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());			// --eliminate-insert-extract
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());				// --eliminate-dead-code-aggressive
			optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());				// --eliminate-dead-branches
			optimizer.RegisterPass(spvtools::CreateBlockMergePass());					// --merge-blocks
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());// --eliminate-local-single-block
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());			// --eliminate-local-single-store
			optimizer.RegisterPass(spvtools::CreateLocalMultiStoreElimPass());			// --eliminate-local-multi-store
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());			// --eliminate-insert-extract
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());				// --eliminate-dead-code-aggressive
			optimizer.RegisterPass(spvtools::CreateCommonUniformElimPass());			// --eliminate-common-uniform
			break;
		case 2: // RegisterPerformancePasses from commandline optimizer tool october 2017
			optimizer.RegisterPass(spvtools::CreateInlineExhaustivePass());
			optimizer.RegisterPass(spvtools::CreateLocalAccessChainConvertPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
			optimizer.RegisterPass(spvtools::CreateBlockMergePass());
			optimizer.RegisterPass(spvtools::CreateLocalMultiStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateCommonUniformElimPass());
			break;
		case 3: // RegisterSizePasses from commandline optimizer tool october 2017
			optimizer.RegisterPass(spvtools::CreateInlineExhaustivePass());
			optimizer.RegisterPass(spvtools::CreateLocalAccessChainConvertPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
			optimizer.RegisterPass(spvtools::CreateBlockMergePass());
			optimizer.RegisterPass(spvtools::CreateLocalMultiStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateCommonUniformElimPass());
			break;
		case 4: // RegisterLegalizationPasses from commandline optimizer tool April 2018
			optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
			optimizer.RegisterPass(spvtools::CreateMergeReturnPass());
			optimizer.RegisterPass(spvtools::CreateInlineExhaustivePass());
			optimizer.RegisterPass(spvtools::CreateEliminateDeadFunctionsPass());
			optimizer.RegisterPass(spvtools::CreatePrivateToLocalPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateScalarReplacementPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateLocalMultiStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateCCPPass());
			optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
			optimizer.RegisterPass(spvtools::CreateSimplificationPass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateCopyPropagateArraysPass());
			optimizer.RegisterPass(spvtools::CreateDeadInsertElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			break;
		case 5: // RegisterPerformancePasses from commandline optimizer tool April 2018
			optimizer.RegisterPass(spvtools::CreateRemoveDuplicatesPass());
			optimizer.RegisterPass(spvtools::CreateMergeReturnPass());
			optimizer.RegisterPass(spvtools::CreateInlineExhaustivePass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateScalarReplacementPass());
			optimizer.RegisterPass(spvtools::CreateLocalAccessChainConvertPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateLocalMultiStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateCCPPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateRedundancyEliminationPass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateDeadInsertElimPass());
			optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
			optimizer.RegisterPass(spvtools::CreateSimplificationPass());
			optimizer.RegisterPass(spvtools::CreateIfConversionPass());
			optimizer.RegisterPass(spvtools::CreateCopyPropagateArraysPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateBlockMergePass());
			optimizer.RegisterPass(spvtools::CreateRedundancyEliminationPass());
			optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
			optimizer.RegisterPass(spvtools::CreateBlockMergePass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
				// comment from tool:
			    // Currently exposing driver bugs resulting in crashes (#946)
				// .RegisterPass(CreateCommonUniformElimPass())
			break;
		case 6: // RegisterPerformancePasses from commandline optimizer tool April 2018 with CreateCommonUniformElimPass
			optimizer.RegisterPass(spvtools::CreateRemoveDuplicatesPass());
			optimizer.RegisterPass(spvtools::CreateMergeReturnPass());
			optimizer.RegisterPass(spvtools::CreateInlineExhaustivePass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateScalarReplacementPass());
			optimizer.RegisterPass(spvtools::CreateLocalAccessChainConvertPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateLocalMultiStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateCCPPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateRedundancyEliminationPass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateDeadInsertElimPass());
			optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
			optimizer.RegisterPass(spvtools::CreateSimplificationPass());
			optimizer.RegisterPass(spvtools::CreateIfConversionPass());
			optimizer.RegisterPass(spvtools::CreateCopyPropagateArraysPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateBlockMergePass());
			optimizer.RegisterPass(spvtools::CreateRedundancyEliminationPass());
			optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
			optimizer.RegisterPass(spvtools::CreateBlockMergePass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateCommonUniformElimPass());
			break;
		case 7: // RegisterSizePasses from commandline optimizer tool April 2018
			optimizer.RegisterPass(spvtools::CreateRemoveDuplicatesPass());
			optimizer.RegisterPass(spvtools::CreateMergeReturnPass());
			optimizer.RegisterPass(spvtools::CreateInlineExhaustivePass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateScalarReplacementPass());
			optimizer.RegisterPass(spvtools::CreateLocalAccessChainConvertPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateDeadInsertElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalMultiStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateCCPPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
			optimizer.RegisterPass(spvtools::CreateIfConversionPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateBlockMergePass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateDeadInsertElimPass());
			optimizer.RegisterPass(spvtools::CreateRedundancyEliminationPass());
			optimizer.RegisterPass(spvtools::CreateCFGCleanupPass());
				// comment from tool:
				// Currently exposing driver bugs resulting in crashes (#946)
				// .RegisterPass(CreateCommonUniformElimPass())
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			break;
		case 8: // RegisterSizePasses from commandline optimizer tool April 2018 with CreateCommonUniformElimPass
			optimizer.RegisterPass(spvtools::CreateRemoveDuplicatesPass());
			optimizer.RegisterPass(spvtools::CreateMergeReturnPass());
			optimizer.RegisterPass(spvtools::CreateInlineExhaustivePass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateScalarReplacementPass());
			optimizer.RegisterPass(spvtools::CreateLocalAccessChainConvertPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalSingleStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateDeadInsertElimPass());
			optimizer.RegisterPass(spvtools::CreateLocalMultiStoreElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateCCPPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateDeadBranchElimPass());
			optimizer.RegisterPass(spvtools::CreateIfConversionPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			optimizer.RegisterPass(spvtools::CreateBlockMergePass());
			optimizer.RegisterPass(spvtools::CreateInsertExtractElimPass());
			optimizer.RegisterPass(spvtools::CreateDeadInsertElimPass());
			optimizer.RegisterPass(spvtools::CreateRedundancyEliminationPass());
			optimizer.RegisterPass(spvtools::CreateCFGCleanupPass());
			optimizer.RegisterPass(spvtools::CreateCommonUniformElimPass());
			optimizer.RegisterPass(spvtools::CreateAggressiveDCEPass());
			break;
		default:
			TCU_THROW(InternalError, "Unknown optimization recipe requested");
	}

	const bool ok = optimizer.Run(binary.data(), binary.size(), &binary);

	if (!ok)
		TCU_THROW(InternalError, "Optimizer call failed");
}

#endif // defined(DEQP_HAVE_SPIRV_TOOLS)


bool isNativeSpirVBinaryEndianness (void)
{
#if (DE_ENDIANNESS == SPIRV_BINARY_ENDIANNESS)
	return true;
#else
	return false;
#endif
}

bool isSaneSpirVBinary (const ProgramBinary& binary)
{
	const deUint32	spirvMagicWord	= 0x07230203;
	const deUint32	spirvMagicBytes	= isNativeSpirVBinaryEndianness()
									? spirvMagicWord
									: deReverseBytes32(spirvMagicWord);

	DE_ASSERT(binary.getFormat() == PROGRAM_FORMAT_SPIRV);

	if (binary.getSize() % sizeof(deUint32) != 0)
		return false;

	if (binary.getSize() < sizeof(deUint32))
		return false;

	if (*(const deUint32*)binary.getBinary() != spirvMagicBytes)
		return false;

	return true;
}

ProgramBinary* createProgramBinaryFromSpirV (const vector<deUint32>& binary)
{
	DE_ASSERT(!binary.empty());

	if (isNativeSpirVBinaryEndianness())
		return new ProgramBinary(PROGRAM_FORMAT_SPIRV, binary.size()*sizeof(deUint32), (const deUint8*)&binary[0]);
	else
		TCU_THROW(InternalError, "SPIR-V endianness translation not supported");
}

} // anonymous

void validateCompiledBinary(const vector<deUint32>& binary, glu::ShaderProgramInfo* buildInfo, const SpirvVersion spirvVersion)
{
	std::ostringstream validationLog;

	if (!validateSpirV(binary.size(), &binary[0], &validationLog, spirvVersion))
	{
		buildInfo->program.linkOk	 = false;
		buildInfo->program.infoLog	+= "\n" + validationLog.str();

		TCU_THROW(InternalError, "Validation failed for compiled SPIR-V binary");
	}
}


#if defined(DEQP_HAVE_SPIRV_TOOLS)

de::Mutex							cacheFileMutex;
map<deUint32, vector<deUint32> >	cacheFileIndex;
bool								cacheFileFirstRun = true;

void shaderCacheFirstRunCheck (const char* shaderCacheFile, bool truncate)
{
	cacheFileMutex.lock();
	if (cacheFileFirstRun)
	{
		cacheFileFirstRun = false;
		if (truncate)
		{
			// Open file with "w" access to truncate it
			FILE* f = fopen(shaderCacheFile, "wb");
			if (f)
				fclose(f);
		}
		else
		{
			// Parse chunked shader cache file for hashes and offsets
			FILE* file = fopen(shaderCacheFile, "rb");
			int count = 0;
			if (file)
			{
				deUint32 chunksize	= 0;
				deUint32 hash		= 0;
				deUint32 offset		= 0;
				bool ok				= true;
				while (ok)
				{
					offset = (deUint32)ftell(file);
					if (ok) ok = fread(&chunksize, 1, 4, file)				== 4;
					if (ok) ok = fread(&hash, 1, 4, file)					== 4;
					if (ok) cacheFileIndex[hash].push_back(offset);
					if (ok) ok = fseek(file, offset + chunksize, SEEK_SET)	== 0;
					count++;
				}
				fclose(file);
			}
		}
	}
	cacheFileMutex.unlock();
}

std::string intToString (deUint32 integer)
{
	std::stringstream temp_sstream;

	temp_sstream << integer;

	return temp_sstream.str();
}

vk::ProgramBinary* shadercacheLoad (const std::string& shaderstring, const char* shaderCacheFilename)
{
	deUint32		hash		= deStringHash(shaderstring.c_str());
	deInt32			format;
	deInt32			length;
	deInt32			sourcelength;
	deUint32		i;
	deUint32		temp;
	deUint8*		bin			= 0;
	char*			source		= 0;
	bool			ok			= true;
	cacheFileMutex.lock();

	if (cacheFileIndex.count(hash) == 0)
	{
		cacheFileMutex.unlock();
		return 0;
	}
	FILE*			file		= fopen(shaderCacheFilename, "rb");
	ok				= file											!= 0;

	for (i = 0; i < cacheFileIndex[hash].size(); i++)
	{
		if (ok) ok = fseek(file, cacheFileIndex[hash][i], SEEK_SET)	== 0;
		if (ok) ok = fread(&temp, 1, 4, file)						== 4; // Chunk size (skip)
		if (ok) ok = fread(&temp, 1, 4, file)						== 4; // Stored hash
		if (ok) ok = temp											== hash; // Double check
		if (ok) ok = fread(&format, 1, 4, file)						== 4;
		if (ok) ok = fread(&length, 1, 4, file)						== 4;
		if (ok) ok = length											> 0; // sanity check
		if (ok) bin = new deUint8[length];
		if (ok) ok = fread(bin, 1, length, file)					== (size_t)length;
		if (ok) ok = fread(&sourcelength, 1, 4, file)				== 4;
		if (ok && sourcelength > 0)
		{
			source = new char[sourcelength + 1];
			ok = fread(source, 1, sourcelength, file)				== (size_t)sourcelength;
			source[sourcelength] = 0;
		}
		if (!ok || shaderstring != std::string(source))
		{
			// Mismatch, but may still exist in cache if there were hash collisions
			delete[] source;
			delete[] bin;
		}
		else
		{
			delete[] source;
			if (file) fclose(file);
			cacheFileMutex.unlock();
			return new vk::ProgramBinary((vk::ProgramFormat)format, length, bin);
		}
	}
	if (file) fclose(file);
	cacheFileMutex.unlock();
	return 0;
}

void shadercacheSave (const vk::ProgramBinary* binary, const std::string& shaderstring, const char* shaderCacheFilename)
{
	if (binary == 0)
		return;
	deUint32			hash		= deStringHash(shaderstring.c_str());
	deInt32				format		= binary->getFormat();
	deUint32			length		= (deUint32)binary->getSize();
	deUint32			chunksize;
	deUint32			offset;
	const deUint8*		bin			= binary->getBinary();
	const de::FilePath	filePath	(shaderCacheFilename);

	cacheFileMutex.lock();

	if (!de::FilePath(filePath.getDirName()).exists())
		de::createDirectoryAndParents(filePath.getDirName().c_str());

	FILE*				file		= fopen(shaderCacheFilename, "ab");
	if (!file)
	{
		cacheFileMutex.unlock();
		return;
	}
	// Append mode starts writing from the end of the file,
	// but unless we do a seek, ftell returns 0.
	fseek(file, 0, SEEK_END);
	offset		= (deUint32)ftell(file);
	chunksize	= 4 + 4 + 4 + 4 + length + 4 + (deUint32)shaderstring.length();
	fwrite(&chunksize, 1, 4, file);
	fwrite(&hash, 1, 4, file);
	fwrite(&format, 1, 4, file);
	fwrite(&length, 1, 4, file);
	fwrite(bin, 1, length, file);
	length = (deUint32)shaderstring.length();
	fwrite(&length, 1, 4, file);
	fwrite(shaderstring.c_str(), 1, length, file);
	fclose(file);
	cacheFileIndex[hash].push_back(offset);

	cacheFileMutex.unlock();
}

// Insert any information that may affect compilation into the shader string.
void getCompileEnvironment (std::string& shaderstring)
{
	shaderstring += "GLSL:";
	shaderstring += qpGetReleaseGlslName();
	shaderstring += "\nSpir-v Tools:";
	shaderstring += qpGetReleaseSpirvToolsName();
	shaderstring += "\nSpir-v Headers:";
	shaderstring += qpGetReleaseSpirvHeadersName();
	shaderstring += "\n";
}

// Insert compilation options into the shader string.
void getBuildOptions (std::string& shaderstring, const ShaderBuildOptions& buildOptions, int optimizationRecipe)
{
	shaderstring += "Target Spir-V ";
	shaderstring += getSpirvVersionName(buildOptions.targetVersion);
	shaderstring += "\n";
	if (buildOptions.flags & ShaderBuildOptions::FLAG_ALLOW_RELAXED_OFFSETS)
		shaderstring += "Flag:Allow relaxed offsets\n";
	if (buildOptions.flags & ShaderBuildOptions::FLAG_USE_STORAGE_BUFFER_STORAGE_CLASS)
		shaderstring += "Flag:Use storage buffer storage class\n";
	if (optimizationRecipe != 0)
	{
		shaderstring += "Optimization recipe ";
		shaderstring += optimizationRecipe;
		shaderstring += "\n";
	}
}

ProgramBinary* buildProgram (const GlslSource& program, glu::ShaderProgramInfo* buildInfo, const tcu::CommandLine& commandLine)
{
	const SpirvVersion	spirvVersion		= program.buildOptions.targetVersion;
	const bool			validateBinary		= VALIDATE_BINARIES;
	vector<deUint32>	binary;
	std::string			shaderstring;
	vk::ProgramBinary*	res					= 0;
	const int			optimizationRecipe	= commandLine.getOptimizationRecipe();

	if (commandLine.isShadercacheEnabled())
	{
		shaderCacheFirstRunCheck(commandLine.getShaderCacheFilename(), commandLine.isShaderCacheTruncateEnabled());
		getCompileEnvironment(shaderstring);
		getBuildOptions(shaderstring, program.buildOptions, optimizationRecipe);

		for (int i = 0; i < glu::SHADERTYPE_LAST; i++)
		{
			if (!program.sources[i].empty())
			{
				shaderstring += glu::getShaderTypeName((glu::ShaderType)i);

				for (std::vector<std::string>::const_iterator it = program.sources[i].begin(); it != program.sources[i].end(); ++it)
					shaderstring += *it;
			}
		}

		res = shadercacheLoad(shaderstring, commandLine.getShaderCacheFilename());

		if (res)
		{
			buildInfo->program.infoLog		= "Loaded from cache";
			buildInfo->program.linkOk		= true;
			buildInfo->program.linkTimeUs	= 0;

			for (int shaderType = 0; shaderType < glu::SHADERTYPE_LAST; shaderType++)
			{
				if (!program.sources[shaderType].empty())
				{
					glu::ShaderInfo	shaderBuildInfo;

					shaderBuildInfo.type			= (glu::ShaderType)shaderType;
					shaderBuildInfo.source			= shaderstring;
					shaderBuildInfo.compileTimeUs	= 0;
					shaderBuildInfo.compileOk		= true;

					buildInfo->shaders.push_back(shaderBuildInfo);
				}
			}
		}
	}

	if (!res)
	{
		{
			vector<deUint32> nonStrippedBinary;

			if (!compileGlslToSpirV(program, &nonStrippedBinary, buildInfo))
				TCU_THROW(InternalError, "Compiling GLSL to SPIR-V failed");

			TCU_CHECK_INTERNAL(!nonStrippedBinary.empty());
			stripSpirVDebugInfo(nonStrippedBinary.size(), &nonStrippedBinary[0], &binary);
			TCU_CHECK_INTERNAL(!binary.empty());
		}

		if (validateBinary)
			validateCompiledBinary(binary, buildInfo, spirvVersion);

		if (optimizationRecipe != 0)
			optimizeCompiledBinary(binary, optimizationRecipe, spirvVersion);

		res = createProgramBinaryFromSpirV(binary);
		if (commandLine.isShadercacheEnabled())
			shadercacheSave(res, shaderstring, commandLine.getShaderCacheFilename());
	}
	return res;
}

ProgramBinary* buildProgram (const HlslSource& program, glu::ShaderProgramInfo* buildInfo, const tcu::CommandLine& commandLine)
{
	const SpirvVersion	spirvVersion		= program.buildOptions.targetVersion;
	const bool			validateBinary		= VALIDATE_BINARIES;
	vector<deUint32>	binary;
	std::string			shaderstring;
	vk::ProgramBinary*	res					= 0;
	const int			optimizationRecipe	= commandLine.getOptimizationRecipe();

	if (commandLine.isShadercacheEnabled())
	{
		shaderCacheFirstRunCheck(commandLine.getShaderCacheFilename(), commandLine.isShaderCacheTruncateEnabled());
		getCompileEnvironment(shaderstring);
		getBuildOptions(shaderstring, program.buildOptions, optimizationRecipe);

		for (int i = 0; i < glu::SHADERTYPE_LAST; i++)
		{
			if (!program.sources[i].empty())
			{
				shaderstring += glu::getShaderTypeName((glu::ShaderType)i);

				for (std::vector<std::string>::const_iterator it = program.sources[i].begin(); it != program.sources[i].end(); ++it)
					shaderstring += *it;
			}
		}

		res = shadercacheLoad(shaderstring, commandLine.getShaderCacheFilename());

		if (res)
		{
			buildInfo->program.infoLog		= "Loaded from cache";
			buildInfo->program.linkOk		= true;
			buildInfo->program.linkTimeUs	= 0;

			for (int shaderType = 0; shaderType < glu::SHADERTYPE_LAST; shaderType++)
			{
				if (!program.sources[shaderType].empty())
				{
					glu::ShaderInfo	shaderBuildInfo;

					shaderBuildInfo.type			= (glu::ShaderType)shaderType;
					shaderBuildInfo.source			= shaderstring;
					shaderBuildInfo.compileTimeUs	= 0;
					shaderBuildInfo.compileOk		= true;

					buildInfo->shaders.push_back(shaderBuildInfo);
				}
			}
		}
	}

	if (!res)
	{
		{
			vector<deUint32> nonStrippedBinary;

			if (!compileHlslToSpirV(program, &nonStrippedBinary, buildInfo))
				TCU_THROW(InternalError, "Compiling HLSL to SPIR-V failed");

			TCU_CHECK_INTERNAL(!nonStrippedBinary.empty());
			stripSpirVDebugInfo(nonStrippedBinary.size(), &nonStrippedBinary[0], &binary);
			TCU_CHECK_INTERNAL(!binary.empty());
		}

		if (validateBinary)
			validateCompiledBinary(binary, buildInfo, spirvVersion);

		if (optimizationRecipe != 0)
			optimizeCompiledBinary(binary, optimizationRecipe, spirvVersion);

		res = createProgramBinaryFromSpirV(binary);
		if (commandLine.isShadercacheEnabled())
			shadercacheSave(res, shaderstring, commandLine.getShaderCacheFilename());
	}
	return res;
}

ProgramBinary* assembleProgram (const SpirVAsmSource& program, SpirVProgramInfo* buildInfo, const tcu::CommandLine& commandLine)
{
	const SpirvVersion	spirvVersion		= program.buildOptions.targetVersion;
	const bool			validateBinary		= VALIDATE_BINARIES;
	vector<deUint32>	binary;
	vk::ProgramBinary*	res					= 0;
	std::string			shaderstring;
	const int			optimizationRecipe	= commandLine.isSpirvOptimizationEnabled() ? commandLine.getOptimizationRecipe() : 0;

	if (commandLine.isShadercacheEnabled())
	{
		shaderCacheFirstRunCheck(commandLine.getShaderCacheFilename(), commandLine.isShaderCacheTruncateEnabled());
		getCompileEnvironment(shaderstring);
		shaderstring += "Target Spir-V ";
		shaderstring += getSpirvVersionName(spirvVersion);
		shaderstring += "\n";
		if (optimizationRecipe != 0)
		{
			shaderstring += "Optimization recipe ";
			shaderstring += optimizationRecipe;
			shaderstring += "\n";
		}

		shaderstring += program.source;

		res = shadercacheLoad(shaderstring, commandLine.getShaderCacheFilename());

		if (res)
		{
			buildInfo->source			= shaderstring;
			buildInfo->compileOk		= true;
			buildInfo->compileTimeUs	= 0;
			buildInfo->infoLog			= "Loaded from cache";
		}
	}

	if (!res)
	{

		if (!assembleSpirV(&program, &binary, buildInfo, spirvVersion))
			TCU_THROW(InternalError, "Failed to assemble SPIR-V");

		if (validateBinary)
		{
			std::ostringstream	validationLog;

			if (!validateSpirV(binary.size(), &binary[0], &validationLog, spirvVersion))
			{
				buildInfo->compileOk = false;
				buildInfo->infoLog += "\n" + validationLog.str();

				TCU_THROW(InternalError, "Validation failed for assembled SPIR-V binary");
			}
		}

		if (optimizationRecipe != 0)
			optimizeCompiledBinary(binary, optimizationRecipe, spirvVersion);

		res = createProgramBinaryFromSpirV(binary);
		if (commandLine.isShadercacheEnabled())
			shadercacheSave(res, shaderstring, commandLine.getShaderCacheFilename());
	}
	return res;
}

#else // !DEQP_HAVE_SPIRV_TOOLS

ProgramBinary* buildProgram (const GlslSource&, glu::ShaderProgramInfo*, const tcu::CommandLine&)
{
	TCU_THROW(NotSupportedError, "GLSL to SPIR-V compilation not supported (DEQP_HAVE_GLSLANG not defined)");
}

ProgramBinary* buildProgram (const HlslSource&, glu::ShaderProgramInfo*, const tcu::CommandLine&)
{
	TCU_THROW(NotSupportedError, "HLSL to SPIR-V compilation not supported (DEQP_HAVE_GLSLANG not defined)");
}

ProgramBinary* assembleProgram (const SpirVAsmSource&, SpirVProgramInfo*, const tcu::CommandLine&)
{
	TCU_THROW(NotSupportedError, "SPIR-V assembly not supported (DEQP_HAVE_SPIRV_TOOLS not defined)");
}
#endif

 void disassembleProgram (const ProgramBinary& program, std::ostream* dst, SpirvVersion spirvVersion)
{
	if (program.getFormat() == PROGRAM_FORMAT_SPIRV)
	{
		TCU_CHECK_INTERNAL(isSaneSpirVBinary(program));

		if (isNativeSpirVBinaryEndianness())
			disassembleSpirV(program.getSize()/sizeof(deUint32), (const deUint32*)program.getBinary(), dst, spirvVersion);
		else
			TCU_THROW(InternalError, "SPIR-V endianness translation not supported");
	}
	else
		TCU_THROW(NotSupportedError, "Unsupported program format");
}

bool validateProgram (const ProgramBinary& program, std::ostream* dst, SpirvVersion spirvVersion)
{
	if (program.getFormat() == PROGRAM_FORMAT_SPIRV)
	{
		if (!isSaneSpirVBinary(program))
		{
			*dst << "Binary doesn't look like SPIR-V at all";
			return false;
		}

		if (isNativeSpirVBinaryEndianness())
			return validateSpirV(program.getSize()/sizeof(deUint32), (const deUint32*)program.getBinary(), dst, spirvVersion);
		else
			TCU_THROW(InternalError, "SPIR-V endianness translation not supported");
	}
	else
		TCU_THROW(NotSupportedError, "Unsupported program format");
}

Move<VkShaderModule> createShaderModule (const DeviceInterface& deviceInterface, VkDevice device, const ProgramBinary& binary, VkShaderModuleCreateFlags flags)
{
	if (binary.getFormat() == PROGRAM_FORMAT_SPIRV)
	{
		const struct VkShaderModuleCreateInfo		shaderModuleInfo	=
		{
			VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			DE_NULL,
			flags,
			(deUintptr)binary.getSize(),
			(const deUint32*)binary.getBinary(),
		};

		return createShaderModule(deviceInterface, device, &shaderModuleInfo);
	}
	else
		TCU_THROW(NotSupportedError, "Unsupported program format");
}

glu::ShaderType getGluShaderType (VkShaderStageFlagBits shaderStage)
{
	switch (shaderStage)
	{
		case VK_SHADER_STAGE_VERTEX_BIT:					return glu::SHADERTYPE_VERTEX;
		case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:		return glu::SHADERTYPE_TESSELLATION_CONTROL;
		case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:	return glu::SHADERTYPE_TESSELLATION_EVALUATION;
		case VK_SHADER_STAGE_GEOMETRY_BIT:					return glu::SHADERTYPE_GEOMETRY;
		case VK_SHADER_STAGE_FRAGMENT_BIT:					return glu::SHADERTYPE_FRAGMENT;
		case VK_SHADER_STAGE_COMPUTE_BIT:					return glu::SHADERTYPE_COMPUTE;
		default:
			DE_FATAL("Unknown shader stage");
			return glu::SHADERTYPE_LAST;
	}
}

VkShaderStageFlagBits getVkShaderStage (glu::ShaderType shaderType)
{
	static const VkShaderStageFlagBits s_shaderStages[] =
	{
		VK_SHADER_STAGE_VERTEX_BIT,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		VK_SHADER_STAGE_GEOMETRY_BIT,
		VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
		VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT
	};

	return de::getSizedArrayElement<glu::SHADERTYPE_LAST>(s_shaderStages, shaderType);
}

vk::SpirvVersion getSpirvVersionForAsm (const deUint32 vulkanVersion)
{
	vk::SpirvVersion	result			= vk::SPIRV_VERSION_LAST;

	deUint32 vulkanVersionMajorMinor = VK_MAKE_VERSION(VK_VERSION_MAJOR(vulkanVersion), VK_VERSION_MINOR(vulkanVersion), 0);
	if (vulkanVersionMajorMinor == VK_API_VERSION_1_0)
		result = vk::SPIRV_VERSION_1_0;
	else if (vulkanVersionMajorMinor >= VK_API_VERSION_1_1)
		result = vk::SPIRV_VERSION_1_3;

	DE_ASSERT(result < vk::SPIRV_VERSION_LAST);

	return result;
}

vk::SpirvVersion getSpirvVersionForGlsl (const deUint32 vulkanVersion)
{
	vk::SpirvVersion	result			= vk::SPIRV_VERSION_LAST;

	deUint32 vulkanVersionMajorMinor = VK_MAKE_VERSION(VK_VERSION_MAJOR(vulkanVersion), VK_VERSION_MINOR(vulkanVersion), 0);
	if (vulkanVersionMajorMinor == VK_API_VERSION_1_0)
		result = vk::SPIRV_VERSION_1_0;
	else if (vulkanVersionMajorMinor >= VK_API_VERSION_1_1)
		result = vk::SPIRV_VERSION_1_3;

	DE_ASSERT(result < vk::SPIRV_VERSION_LAST);

	return result;
}

SpirvVersion extractSpirvVersion (const ProgramBinary& binary)
{
	DE_STATIC_ASSERT(SPIRV_VERSION_1_3 + 1 == SPIRV_VERSION_LAST);

	if (binary.getFormat() != PROGRAM_FORMAT_SPIRV)
		TCU_THROW(InternalError, "Binary is not in SPIR-V format");

	if (!isSaneSpirVBinary(binary) || binary.getSize() < sizeof(SpirvBinaryHeader))
		TCU_THROW(InternalError, "Invalid SPIR-V header format");

	const deUint32				spirvBinaryVersion10	= 0x00010000;
	const deUint32				spirvBinaryVersion11	= 0x00010100;
	const deUint32				spirvBinaryVersion12	= 0x00010200;
	const deUint32				spirvBinaryVersion13	= 0x00010300;
	const SpirvBinaryHeader*	header					= reinterpret_cast<const SpirvBinaryHeader*>(binary.getBinary());
	const deUint32				spirvVersion			= isNativeSpirVBinaryEndianness()
														? header->version
														: deReverseBytes32(header->version);
	SpirvVersion				result					= SPIRV_VERSION_LAST;

	switch (spirvVersion)
	{
		case spirvBinaryVersion10:	result = SPIRV_VERSION_1_0; break; //!< SPIR-V 1.0
		case spirvBinaryVersion11:	result = SPIRV_VERSION_1_1; break; //!< SPIR-V 1.1
		case spirvBinaryVersion12:	result = SPIRV_VERSION_1_2; break; //!< SPIR-V 1.2
		case spirvBinaryVersion13:	result = SPIRV_VERSION_1_3; break; //!< SPIR-V 1.3
		default:					TCU_THROW(InternalError, "Unknown SPIR-V version detected in binary");
	}

	return result;
}

std::string getSpirvVersionName (const SpirvVersion spirvVersion)
{
	DE_STATIC_ASSERT(SPIRV_VERSION_1_3 + 1 == SPIRV_VERSION_LAST);
	DE_ASSERT(spirvVersion < SPIRV_VERSION_LAST);

	std::string result;

	switch (spirvVersion)
	{
		case SPIRV_VERSION_1_0: result = "1.0"; break; //!< SPIR-V 1.0
		case SPIRV_VERSION_1_1: result = "1.1"; break; //!< SPIR-V 1.1
		case SPIRV_VERSION_1_2: result = "1.2"; break; //!< SPIR-V 1.2
		case SPIRV_VERSION_1_3: result = "1.3"; break; //!< SPIR-V 1.3
		default:				result = "Unknown";
	}

	return result;
}

SpirvVersion& operator++(SpirvVersion& spirvVersion)
{
	if (spirvVersion == SPIRV_VERSION_LAST)
		spirvVersion = SPIRV_VERSION_1_0;
	else
		spirvVersion = static_cast<SpirvVersion>(static_cast<deUint32>(spirvVersion) + 1);

	return spirvVersion;
}

} // vk
