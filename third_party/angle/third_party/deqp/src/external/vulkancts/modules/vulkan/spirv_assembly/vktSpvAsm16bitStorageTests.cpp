/*-------------------------------------------------------------------------
 * Vulkan Conformance Tests
 * ------------------------
 *
 * Copyright (c) 2017 Google Inc.
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
 * \brief SPIR-V Assembly Tests for the VK_KHR_16bit_storage
 *//*--------------------------------------------------------------------*/

// VK_KHR_16bit_storage
//
// \todo [2017-02-08 antiagainst] Additional corner cases to check:
//
// * Test {StorageInputOutput16} 16-to-16 one value to two:
//     Like the current 16-to-16 tests, but write X16 to two different output variables.
//     (Checks that the 16-bit intermediate value can be used twice.)
//     Note: The current framework allows only one interface to be used.
//     Maybe the best solution is to add custom shaders that manually use two interfaces.

#include "vktSpvAsm16bitStorageTests.hpp"

#include "tcuFloat.hpp"
#include "tcuRGBA.hpp"
#include "tcuStringTemplate.hpp"
#include "tcuTestLog.hpp"
#include "tcuVectorUtil.hpp"

#include "vkDefs.hpp"
#include "vkDeviceUtil.hpp"
#include "vkMemUtil.hpp"
#include "vkPlatform.hpp"
#include "vkPrograms.hpp"
#include "vkQueryUtil.hpp"
#include "vkRef.hpp"
#include "vkRefUtil.hpp"
#include "vkStrUtil.hpp"
#include "vkTypeUtil.hpp"

#include "deRandom.hpp"
#include "deStringUtil.hpp"
#include "deUniquePtr.hpp"
#include "deMath.h"

#include "vktSpvAsmComputeShaderCase.hpp"
#include "vktSpvAsmComputeShaderTestUtil.hpp"
#include "vktSpvAsmGraphicsShaderTestUtil.hpp"
#include "vktTestCaseUtil.hpp"
#include "vktTestGroupUtil.hpp"

#include <limits>
#include <map>
#include <string>
#include <sstream>
#include <utility>

namespace vkt
{
namespace SpirVAssembly
{

using namespace vk;
using std::map;
using std::string;
using std::vector;
using tcu::Float16;
using tcu::IVec3;
using tcu::IVec4;
using tcu::RGBA;
using tcu::TestLog;
using tcu::TestStatus;
using tcu::Vec4;
using de::UniquePtr;
using tcu::StringTemplate;
using tcu::Vec4;

namespace
{

enum ShaderTemplate
{
	SHADERTEMPLATE_TYPES = 0,
	SHADERTEMPLATE_STRIDE32BIT_STD140,
	SHADERTEMPLATE_STRIDE32BIT_STD430,
	SHADERTEMPLATE_STRIDE16BIT_STD140,
	SHADERTEMPLATE_STRIDE16BIT_STD430,
	SHADERTEMPLATE_STRIDEMIX_STD140,
	SHADERTEMPLATE_STRIDEMIX_STD430
};

bool compare16Bit (float original, deUint16 returned, RoundingModeFlags flags, tcu::TestLog& log)
{
	return compare16BitFloat (original, returned, flags, log);
}

bool compare16Bit (deFloat16 returned, float original, RoundingModeFlags flags, tcu::TestLog& log)
{
	return compare16BitFloat (original, returned, flags, log);
}

bool compare16Bit (deInt16 returned, deInt16 original, RoundingModeFlags flags, tcu::TestLog& log)
{
	DE_UNREF(flags);
	DE_UNREF(log);
	return (returned == original);
}

struct StructTestData
{
	const int structArraySize; //Size of Struct Array
	const int nestedArraySize; //Max size of any nested arrays
};

struct Capability
{
	const char*				name;
	const char*				cap;
	const char*				decor;
	vk::VkDescriptorType	dtype;
};

static const Capability	CAPABILITIES[]	=
{
	{"uniform_buffer_block",	"StorageUniformBufferBlock16",	"BufferBlock",	VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
	{"uniform",					"StorageUniform16",				"Block",		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
};

static const StructTestData structData = {7, 11};

VulkanFeatures	get16BitStorageFeatures	(const char* cap)
{
	VulkanFeatures features;
	if (string(cap) == "uniform_buffer_block")
		features.ext16BitStorage = EXT16BITSTORAGEFEATURES_UNIFORM_BUFFER_BLOCK;
	else if (string(cap) == "uniform")
		features.ext16BitStorage = EXT16BITSTORAGEFEATURES_UNIFORM;
	else
		DE_ASSERT(false && "not supported");

	return features;
}

int getStructSize(const ShaderTemplate  shaderTemplate)
{
	switch (shaderTemplate)
	{
	case SHADERTEMPLATE_STRIDE16BIT_STD140:
		return 600 * structData.structArraySize;		//size of struct in f16 with offsets
	case SHADERTEMPLATE_STRIDE16BIT_STD430:
		return 184 * structData.structArraySize;		//size of struct in f16 with offsets
	case SHADERTEMPLATE_STRIDE32BIT_STD140:
		return 304 * structData.structArraySize;		//size of struct in f32 with offsets
	case SHADERTEMPLATE_STRIDE32BIT_STD430:
		return 184 * structData.structArraySize;		//size of struct in f32 with offset
	case SHADERTEMPLATE_STRIDEMIX_STD140:
		return 4480 * structData.structArraySize / 2;	//size of struct in 16b with offset
	case SHADERTEMPLATE_STRIDEMIX_STD430:
		return 1216 * structData.structArraySize / 2;	//size of struct in 16b with offset
	default:
		DE_ASSERT(0);
	}
	return 0;
}

// Batch function to check arrays of 16-bit floats.
//
// For comparing 16-bit floats, we need to consider both RTZ and RTE. So we can only recalculate
// the expected values here instead of get the expected values directly from the test case.
// Thus we need original floats here but not expected outputs.
template<RoundingModeFlags RoundingMode>
bool graphicsCheck16BitFloats (const std::vector<Resource>&	originalFloats,
							   const vector<AllocationSp>&	outputAllocs,
							   const std::vector<Resource>&	/* expectedOutputs */,
							   tcu::TestLog&				log)
{
	if (outputAllocs.size() != originalFloats.size())
		return false;

	for (deUint32 outputNdx = 0; outputNdx < outputAllocs.size(); ++outputNdx)
	{
		vector<deUint8>	originalBytes;
		originalFloats[outputNdx].second->getBytes(originalBytes);

		const deUint16*	returned	= static_cast<const deUint16*>(outputAllocs[outputNdx]->getHostPtr());
		const float*	original	= reinterpret_cast<const float*>(&originalBytes.front());
		const deUint32	count		= static_cast<deUint32>(originalBytes.size() / sizeof(float));

		for (deUint32 numNdx = 0; numNdx < count; ++numNdx)
			if (!compare16BitFloat(original[numNdx], returned[numNdx], RoundingMode, log))
				return false;
	}

	return true;
}

bool computeCheckBuffersFloats (const std::vector<BufferSp>&	originalFloats,
								const vector<AllocationSp>&		outputAllocs,
								const std::vector<BufferSp>&	/*expectedOutputs*/,
								tcu::TestLog&					/*log*/)
{
	std::vector<deUint8> result;
	originalFloats.front()->getBytes(result);

	const deUint16 * results = reinterpret_cast<const deUint16 *>(&result[0]);
	const deUint16 * expected = reinterpret_cast<const deUint16 *>(outputAllocs.front()->getHostPtr());

	for (size_t i = 0; i < result.size() / sizeof (deUint16); ++i)
	{
		if (results[i] == expected[i])
			continue;

		if (Float16(results[i]).isNaN() && Float16(expected[i]).isNaN())
			continue;

		return false;
	}

	return true;
}

template<RoundingModeFlags RoundingMode>
bool computeCheck16BitFloats (const std::vector<BufferSp>&	originalFloats,
							  const vector<AllocationSp>&	outputAllocs,
							  const std::vector<BufferSp>&	/* expectedOutputs */,
							  tcu::TestLog&					log)
{
	if (outputAllocs.size() != originalFloats.size())
		return false;

	for (deUint32 outputNdx = 0; outputNdx < outputAllocs.size(); ++outputNdx)
	{
		vector<deUint8>	originalBytes;
		originalFloats[outputNdx]->getBytes(originalBytes);

		const deUint16*	returned	= static_cast<const deUint16*>(outputAllocs[outputNdx]->getHostPtr());
		const float*	original	= reinterpret_cast<const float*>(&originalBytes.front());
		const deUint32	count		= static_cast<deUint32>(originalBytes.size() / sizeof(float));

		for (deUint32 numNdx = 0; numNdx < count; ++numNdx)
			if (!compare16BitFloat(original[numNdx], returned[numNdx], RoundingMode, log))
				return false;
	}

	return true;
}

// Batch function to check arrays of 32-bit floats.
//
// For comparing 32-bit floats, we just need the expected value precomputed in the test case.
// So we need expected outputs here but not original floats.
bool check32BitFloats (const std::vector<Resource>&		/* originalFloats */,
					   const std::vector<AllocationSp>& outputAllocs,
					   const std::vector<Resource>&		expectedOutputs,
					   tcu::TestLog&					log)
{
	if (outputAllocs.size() != expectedOutputs.size())
		return false;

	for (deUint32 outputNdx = 0; outputNdx < outputAllocs.size(); ++outputNdx)
	{
		vector<deUint8>	expectedBytes;
		expectedOutputs[outputNdx].second->getBytes(expectedBytes);

		const float*	returnedAsFloat	= static_cast<const float*>(outputAllocs[outputNdx]->getHostPtr());
		const float*	expectedAsFloat	= reinterpret_cast<const float*>(&expectedBytes.front());
		const deUint32	count			= static_cast<deUint32>(expectedBytes.size() / sizeof(float));

		for (deUint32 numNdx = 0; numNdx < count; ++numNdx)
			if (!compare32BitFloat(expectedAsFloat[numNdx], returnedAsFloat[numNdx], log))
				return false;
	}

	return true;
}

// Overload for compute pipeline
bool check32BitFloats (const std::vector<BufferSp>&		/* originalFloats */,
					   const std::vector<AllocationSp>& outputAllocs,
					   const std::vector<BufferSp>&		expectedOutputs,
					   tcu::TestLog&					log)
{
	if (outputAllocs.size() != expectedOutputs.size())
		return false;

	for (deUint32 outputNdx = 0; outputNdx < outputAllocs.size(); ++outputNdx)
	{
		vector<deUint8>	expectedBytes;
		expectedOutputs[outputNdx]->getBytes(expectedBytes);

		const float*	returnedAsFloat	= static_cast<const float*>(outputAllocs[outputNdx]->getHostPtr());
		const float*	expectedAsFloat	= reinterpret_cast<const float*>(&expectedBytes.front());
		const deUint32	count			= static_cast<deUint32>(expectedBytes.size() / sizeof(float));

		for (deUint32 numNdx = 0; numNdx < count; ++numNdx)
			if (!compare32BitFloat(expectedAsFloat[numNdx], returnedAsFloat[numNdx], log))
				return false;
	}

	return true;
}

// Generate and return 32-bit integers.
//
// Expected count to be at least 16.
vector<deInt32> getInt32s (de::Random& rnd, const deUint32 count)
{
	vector<deInt32>		data;

	data.reserve(count);

	// Make sure we have boundary numbers.
	data.push_back(deInt32(0x00000000));  // 0
	data.push_back(deInt32(0x00000001));  // 1
	data.push_back(deInt32(0x0000002a));  // 42
	data.push_back(deInt32(0x00007fff));  // 32767
	data.push_back(deInt32(0x00008000));  // 32768
	data.push_back(deInt32(0x0000ffff));  // 65535
	data.push_back(deInt32(0x00010000));  // 65536
	data.push_back(deInt32(0x7fffffff));  // 2147483647
	data.push_back(deInt32(0x80000000));  // -2147483648
	data.push_back(deInt32(0x80000001));  // -2147483647
	data.push_back(deInt32(0xffff0000));  // -65536
	data.push_back(deInt32(0xffff0001));  // -65535
	data.push_back(deInt32(0xffff8000));  // -32768
	data.push_back(deInt32(0xffff8001));  // -32767
	data.push_back(deInt32(0xffffffd6));  // -42
	data.push_back(deInt32(0xffffffff));  // -1

	DE_ASSERT(count >= data.size());

	for (deUint32 numNdx = static_cast<deUint32>(data.size()); numNdx < count; ++numNdx)
		data.push_back(static_cast<deInt32>(rnd.getUint32()));

	return data;
}

// Generate and return 16-bit integers.
//
// Expected count to be at least 8.
vector<deInt16> getInt16s (de::Random& rnd, const deUint32 count)
{
	vector<deInt16>		data;

	data.reserve(count);

	// Make sure we have boundary numbers.
	data.push_back(deInt16(0x0000));  // 0
	data.push_back(deInt16(0x0001));  // 1
	data.push_back(deInt16(0x002a));  // 42
	data.push_back(deInt16(0x7fff));  // 32767
	data.push_back(deInt16(0x8000));  // -32868
	data.push_back(deInt16(0x8001));  // -32767
	data.push_back(deInt16(0xffd6));  // -42
	data.push_back(deInt16(0xffff));  // -1

	DE_ASSERT(count >= data.size());

	for (deUint32 numNdx = static_cast<deUint32>(data.size()); numNdx < count; ++numNdx)
		data.push_back(static_cast<deInt16>(rnd.getUint16()));

	return data;
}

// IEEE-754 floating point numbers:
// +--------+------+----------+-------------+
// | binary | sign | exponent | significand |
// +--------+------+----------+-------------+
// | 16-bit |  1   |    5     |     10      |
// +--------+------+----------+-------------+
// | 32-bit |  1   |    8     |     23      |
// +--------+------+----------+-------------+
//
// 16-bit floats:
//
// 0   000 00   00 0000 0001 (0x0001: 2e-24:         minimum positive denormalized)
// 0   000 00   11 1111 1111 (0x03ff: 2e-14 - 2e-24: maximum positive denormalized)
// 0   000 01   00 0000 0000 (0x0400: 2e-14:         minimum positive normalized)
//
// 32-bit floats:
//
// 0   011 1110 1   001 0000 0000 0000 0000 0000 (0x3e900000: 0.28125: with exact match in 16-bit normalized)
// 0   011 1000 1   000 0000 0011 0000 0000 0000 (0x38803000: exact half way within two 16-bit normalized; round to zero: 0x0401)
// 1   011 1000 1   000 0000 0011 0000 0000 0000 (0xb8803000: exact half way within two 16-bit normalized; round to zero: 0x8402)
// 0   011 1000 1   000 0000 1111 1111 0000 0000 (0x3880ff00: not exact half way within two 16-bit normalized; round to zero: 0x0403)
// 1   011 1000 1   000 0000 1111 1111 0000 0000 (0xb880ff00: not exact half way within two 16-bit normalized; round to zero: 0x8404)

// Generate and return 32-bit floats
//
// The first 24 number pairs are manually picked, while the rest are randomly generated.
// Expected count to be at least 24 (numPicks).
vector<float> getFloat32s (de::Random& rnd, deUint32 count)
{
	vector<float>		float32;

	float32.reserve(count);

	// Zero
	float32.push_back(0.f);
	float32.push_back(-0.f);
	// Infinity
	float32.push_back(std::numeric_limits<float>::infinity());
	float32.push_back(-std::numeric_limits<float>::infinity());
	// SNaN
	float32.push_back(std::numeric_limits<float>::signaling_NaN());
	float32.push_back(-std::numeric_limits<float>::signaling_NaN());
	// QNaN
	float32.push_back(std::numeric_limits<float>::quiet_NaN());
	float32.push_back(-std::numeric_limits<float>::quiet_NaN());

	// Denormalized 32-bit float matching 0 in 16-bit
	float32.push_back(deFloatLdExp(1.f, -127));
	float32.push_back(-deFloatLdExp(1.f, -127));

	// Normalized 32-bit float matching 0 in 16-bit
	float32.push_back(deFloatLdExp(1.f, -100));
	float32.push_back(-deFloatLdExp(1.f, -100));
	// Normalized 32-bit float with exact denormalized match in 16-bit
	float32.push_back(deFloatLdExp(1.f, -24));  // 2e-24: minimum 16-bit positive denormalized
	float32.push_back(-deFloatLdExp(1.f, -24)); // 2e-24: maximum 16-bit negative denormalized
	// Normalized 32-bit float with exact normalized match in 16-bit
	float32.push_back(deFloatLdExp(1.f, -14));  // 2e-14: minimum 16-bit positive normalized
	float32.push_back(-deFloatLdExp(1.f, -14)); // 2e-14: maximum 16-bit negative normalized
	// Normalized 32-bit float falling above half way within two 16-bit normalized
	float32.push_back(bitwiseCast<float>(deUint32(0x3880ff00)));
	float32.push_back(bitwiseCast<float>(deUint32(0xb880ff00)));
	// Normalized 32-bit float falling exact half way within two 16-bit normalized
	float32.push_back(bitwiseCast<float>(deUint32(0x38803000)));
	float32.push_back(bitwiseCast<float>(deUint32(0xb8803000)));
	// Some number
	float32.push_back(0.28125f);
	float32.push_back(-0.28125f);
	// Normalized 32-bit float matching infinity in 16-bit
	float32.push_back(deFloatLdExp(1.f, 100));
	float32.push_back(-deFloatLdExp(1.f, 100));

	const deUint32		numPicks	= static_cast<deUint32>(float32.size());

	DE_ASSERT(count >= numPicks);
	count -= numPicks;

	for (deUint32 numNdx = 0; numNdx < count; ++numNdx)
		float32.push_back(rnd.getFloat());

	return float32;
}

// IEEE-754 floating point numbers:
// +--------+------+----------+-------------+
// | binary | sign | exponent | significand |
// +--------+------+----------+-------------+
// | 16-bit |  1   |    5     |     10      |
// +--------+------+----------+-------------+
// | 32-bit |  1   |    8     |     23      |
// +--------+------+----------+-------------+
//
// 16-bit floats:
//
// 0   000 00   00 0000 0001 (0x0001: 2e-24:         minimum positive denormalized)
// 0   000 00   11 1111 1111 (0x03ff: 2e-14 - 2e-24: maximum positive denormalized)
// 0   000 01   00 0000 0000 (0x0400: 2e-14:         minimum positive normalized)
//
// 0   000 00   00 0000 0000 (0x0000: +0)
// 0   111 11   00 0000 0000 (0x7c00: +Inf)
// 0   000 00   11 1111 0000 (0x03f0: +Denorm)
// 0   000 01   00 0000 0001 (0x0401: +Norm)
// 0   111 11   00 0000 1111 (0x7c0f: +SNaN)
// 0   111 11   00 1111 0000 (0x7c0f: +QNaN)


// Generate and return 16-bit floats and their corresponding 32-bit values.
//
// The first 14 number pairs are manually picked, while the rest are randomly generated.
// Expected count to be at least 14 (numPicks).
vector<deFloat16> getFloat16s (de::Random& rnd, deUint32 count)
{
	vector<deFloat16>	float16;

	float16.reserve(count);

	// Zero
	float16.push_back(deUint16(0x0000));
	float16.push_back(deUint16(0x8000));
	// Infinity
	float16.push_back(deUint16(0x7c00));
	float16.push_back(deUint16(0xfc00));
	// SNaN
	float16.push_back(deUint16(0x7c0f));
	float16.push_back(deUint16(0xfc0f));
	// QNaN
	float16.push_back(deUint16(0x7cf0));
	float16.push_back(deUint16(0xfcf0));

	// Denormalized
	float16.push_back(deUint16(0x03f0));
	float16.push_back(deUint16(0x83f0));
	// Normalized
	float16.push_back(deUint16(0x0401));
	float16.push_back(deUint16(0x8401));
	// Some normal number
	float16.push_back(deUint16(0x14cb));
	float16.push_back(deUint16(0x94cb));

	const deUint32		numPicks	= static_cast<deUint32>(float16.size());

	DE_ASSERT(count >= numPicks);
	count -= numPicks;

	for (deUint32 numIdx = 0; numIdx < count; ++numIdx)
		float16.push_back(rnd.getUint16());

	return float16;
}

void addInfo(vector<bool>& info, int& ndx, const int count, bool isData)
{
	for (int index = 0; index < count; ++index)
		info[ndx++] = isData;
}

vector<deFloat16> data16bitStd140 (de::Random& rnd)
{
	return getFloat16s(rnd, getStructSize(SHADERTEMPLATE_STRIDE16BIT_STD140));
}

vector<bool> info16bitStd140 (void)
{
	int				ndx			= 0u;
	vector<bool>	infoData	(getStructSize(SHADERTEMPLATE_STRIDE16BIT_STD140));

	for(int elementNdx = 0; elementNdx < structData.structArraySize; ++elementNdx)
	{
		infoData[ndx++] = true;						//f16
		infoData[ndx++] = false;					//offset

		infoData[ndx++] = true;						//v2f16
		infoData[ndx++] = true;						//v2f16

		addInfo(infoData, ndx, 3, true);			//v3f16
		infoData[ndx++] = false;					//offset

		addInfo(infoData, ndx, 4, true);			//v4f16
		addInfo(infoData, ndx, 4, false);			//offset

		//f16[3];
		for (int i = 0; i < 3; ++i)
		{
			infoData[ndx++] = true;					//f16[0];
			addInfo(infoData, ndx, 7, false);		//offset
		}

		//struct {f16, v2f16[3]} [11]
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			//struct.f16
			infoData[ndx++] = true;					//f16
			addInfo(infoData, ndx, 7, false);		//offset
			//struct.f16.v2f16[3]
			for (int j = 0; j < 3; ++j)
			{
				infoData[ndx++] = true;				//v2f16
				infoData[ndx++] = true;				//v2f16
				addInfo(infoData, ndx, 6, false);	//offset
			}
		}

		//vec2[11];
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			infoData[ndx++] = true;					//v2f16
			infoData[ndx++] = true;					//v2f16
			addInfo(infoData, ndx, 6, false);		//offset
		}

		//f16
		infoData[ndx++] = true;						//f16
		addInfo(infoData, ndx, 7, false);			//offset

		//vec3[11]
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			addInfo(infoData, ndx, 3, true);		//vec3
			addInfo(infoData, ndx, 5, false);		//offset
		}

		//vec4[3]
		for (int i = 0; i < 3; ++i)
		{
			addInfo(infoData, ndx, 4, true);		//vec4
			addInfo(infoData, ndx, 4, false);		//offset
		}
	}

	//Please check the data and offset
	DE_ASSERT(ndx == static_cast<int>(infoData.size()));

	return infoData;
}

vector<deFloat16> data16bitStd430 (de::Random& rnd)
{
	return getFloat16s(rnd, getStructSize(SHADERTEMPLATE_STRIDE16BIT_STD430));
}

vector<bool> info16bitStd430 (void)
{
	int				ndx			= 0u;
	vector<bool>	infoData	(getStructSize(SHADERTEMPLATE_STRIDE16BIT_STD430));

	for(int elementNdx = 0; elementNdx < structData.structArraySize; ++elementNdx)
	{
		infoData[ndx++] = true;					//f16
		infoData[ndx++] = false;				//offset

		infoData[ndx++] = true;					//v2f16
		infoData[ndx++] = true;					//v2f16

		addInfo(infoData, ndx, 3, true);		//v3f16
		infoData[ndx++] = false;				//offset

		addInfo(infoData, ndx, 4, true);		//v4f16

		//f16[3];
		for (int i = 0; i < 3; ++i)
		{
			infoData[ndx++] = true;				//f16;
		}
		addInfo(infoData, ndx, 1, false);		//offset

		//struct {f16, v2f16[3]} [11]
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			//struct.f16
			infoData[ndx++] = true;				//f16
			infoData[ndx++] = false;			//offset
			//struct.f16.v2f16[3]
			for (int j = 0; j < 3; ++j)
			{
				infoData[ndx++] = true;			//v2f16
				infoData[ndx++] = true;			//v2f16
			}
		}

		//vec2[11];
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			infoData[ndx++] = true;				//v2f16
			infoData[ndx++] = true;				//v2f16
		}

		//f16
		infoData[ndx++] = true;					//f16
		infoData[ndx++] = false;				//offset

		//vec3[11]
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			addInfo(infoData, ndx, 3, true);	//vec3
			infoData[ndx++] = false;			//offset
		}

		//vec4[3]
		for (int i = 0; i < 3; ++i)
		{
			addInfo(infoData, ndx, 4, true);	//vec4
		}
	}

	//Please check the data and offset
	DE_ASSERT(ndx == static_cast<int>(infoData.size()));
	return infoData;
}

vector<float> data32bitStd140 (de::Random& rnd)
{
	return getFloat32s(rnd, getStructSize(SHADERTEMPLATE_STRIDE32BIT_STD140));
}

vector<bool> info32bitStd140 (void)
{
	int				ndx			= 0u;
	vector<bool>	infoData	(getStructSize(SHADERTEMPLATE_STRIDE32BIT_STD140));

	for(int elementNdx = 0; elementNdx < structData.structArraySize; ++elementNdx)
	{
		infoData[ndx++] = true;					//f32
		infoData[ndx++] = false;				//offset

		infoData[ndx++] = true;					//v2f32
		infoData[ndx++] = true;					//v2f32

		addInfo(infoData, ndx, 3, true);		//v3f32
		infoData[ndx++] = false;				//offset

		addInfo(infoData, ndx, 4, true);		//v4f16

		//f32[3];
		for (int i = 0; i < 3; ++i)
		{
			infoData[ndx++] = true;				//f32;
			addInfo(infoData, ndx, 3, false);	//offset
		}

		//struct {f32, v2f32[3]} [11]
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			//struct.f32
			infoData[ndx++] = true;				//f32
			addInfo(infoData, ndx, 3, false);	//offset
			//struct.f32.v2f16[3]
			for (int j = 0; j < 3; ++j)
			{
				infoData[ndx++] = true;			//v2f32
				infoData[ndx++] = true;			//v2f32
				infoData[ndx++] = false;		//offset
				infoData[ndx++] = false;		//offset
			}
		}

		//v2f32[11];
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			infoData[ndx++] = true;				//v2f32
			infoData[ndx++] = true;				//v2f32
			infoData[ndx++] = false;			//offset
			infoData[ndx++] = false;			//offset
		}

		//f16
		infoData[ndx++] = true;					//f16
		addInfo(infoData, ndx, 3, false);		//offset

		//vec3[11]
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			addInfo(infoData, ndx, 3, true);	//v3f32
			infoData[ndx++] = false;			//offset
		}

		//vec4[3]
		for (int i = 0; i < 3; ++i)
		{
			addInfo(infoData, ndx, 4, true);	//vec4
		}
	}

	//Please check the data and offset
	DE_ASSERT(ndx == static_cast<int>(infoData.size()));
	return infoData;
}

vector<float> data32bitStd430 (de::Random& rnd)
{
	return getFloat32s(rnd, getStructSize(SHADERTEMPLATE_STRIDE32BIT_STD430));
}

vector<bool> info32bitStd430 (void)
{
	int				ndx			= 0u;
	vector<bool>	infoData	(getStructSize(SHADERTEMPLATE_STRIDE32BIT_STD430));

	for(int elementNdx = 0; elementNdx < structData.structArraySize; ++elementNdx)
	{
		infoData[ndx++] = true;					//f32
		infoData[ndx++] = false;				//offset

		infoData[ndx++] = true;					//v2f32
		infoData[ndx++] = true;					//v2f32

		addInfo(infoData, ndx, 3, true);		//v3f32
		infoData[ndx++] = false;				//offset

		addInfo(infoData, ndx, 4, true);		//v4f16

		//f32[3];
		for (int i = 0; i < 3; ++i)
		{
			infoData[ndx++] = true;				//f32;
		}
		infoData[ndx++] = false;				//offset

		//struct {f32, v2f32[3]} [11]
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			//struct.f32
			infoData[ndx++] = true;				//f32
			infoData[ndx++] = false;			//offset
			//struct.f32.v2f16[3]
			for (int j = 0; j < 3; ++j)
			{
				infoData[ndx++] = true;			//v2f32
				infoData[ndx++] = true;			//v2f32
			}
		}

		//v2f32[11];
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			infoData[ndx++] = true;				//v2f32
			infoData[ndx++] = true;				//v2f32
		}

		//f32
		infoData[ndx++] = true;					//f32
		infoData[ndx++] = false;				//offset

		//vec3[11]
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			addInfo(infoData, ndx, 3, true);	//v3f32
			infoData[ndx++] = false;			//offset
		}

		//vec4[3]
		for (int i = 0; i < 3; ++i)
		{
			addInfo(infoData, ndx, 4, true);	//vec4
		}
	}

	//Please check the data and offset
	DE_ASSERT(ndx == static_cast<int>(infoData.size()));
	return infoData;
}

vector<deInt16> dataMixStd140 (de::Random& rnd)
{
	return getInt16s(rnd, getStructSize(SHADERTEMPLATE_STRIDEMIX_STD140));
}

vector<bool> infoMixStd140 (void)
{
	int				ndx			= 0u;
	vector<bool>	infoData	(getStructSize(SHADERTEMPLATE_STRIDEMIX_STD140));
	for(int elementNdx = 0; elementNdx < structData.structArraySize; ++elementNdx)
	{
		infoData[ndx++] = true;				//16b
		addInfo(infoData, ndx, 1, false);		//offset

		addInfo(infoData, ndx, 2, true);		//32b

		addInfo(infoData, ndx, 2, true);		//v2b16
		addInfo(infoData, ndx, 2, false);		//offset

		addInfo(infoData, ndx, 4, true);		//v2b32

		addInfo(infoData, ndx, 3, true);		//v3b16
		addInfo(infoData, ndx, 1, false);		//offset

		addInfo(infoData, ndx, 6, true);		//v3b32
		addInfo(infoData, ndx, 2, false);		//offset

		addInfo(infoData, ndx, 4, true);		//v4b16
		addInfo(infoData, ndx, 4, false);		//offset

		addInfo(infoData, ndx, 8, true);		//v4b32

		//strut {b16, b32, v2b16[11], b32[11]}
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			infoData[ndx++] = true;				//16b
			addInfo(infoData, ndx, 1, false);	//offset

			addInfo(infoData, ndx, 2, true);	//32b
			addInfo(infoData, ndx, 4, false);	//offset

			for (int j = 0; j < structData.nestedArraySize; ++j)
			{
				addInfo(infoData, ndx, 2, true);	//v2b16[11]
				addInfo(infoData, ndx, 6, false);	//offset
			}

			for (int j = 0; j < structData.nestedArraySize; ++j)
			{
				addInfo(infoData, ndx, 2, true);	//b32[11]
				addInfo(infoData, ndx, 6, false);	//offset
			}
		}

		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			infoData[ndx++] = true;				//16b[11]
			addInfo(infoData, ndx, 7, false);		//offset
		}

		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			addInfo(infoData, ndx, 2, true);	//b32bIn[11]
			addInfo(infoData, ndx, 6, false);	//offset
		}
	}

	//Please check the data and offset
	DE_ASSERT(ndx == static_cast<int>(infoData.size()));
	return infoData;
}

vector<deInt16> dataMixStd430 (de::Random& rnd)
{
	return getInt16s(rnd, getStructSize(SHADERTEMPLATE_STRIDEMIX_STD430));
}

vector<bool> infoMixStd430 (void)
{
	int				ndx			= 0u;
	vector<bool>	infoData	(getStructSize(SHADERTEMPLATE_STRIDEMIX_STD430));
	for(int elementNdx = 0; elementNdx < structData.structArraySize; ++elementNdx)
	{
		infoData[ndx++] = true;				//16b
		addInfo(infoData, ndx, 1, false);		//offset

		addInfo(infoData, ndx, 2, true);		//32b

		addInfo(infoData, ndx, 2, true);		//v2b16
		addInfo(infoData, ndx, 2, false);		//offset

		addInfo(infoData, ndx, 4, true);		//v2b32

		addInfo(infoData, ndx, 3, true);		//v3b16
		addInfo(infoData, ndx, 1, false);		//offset

		addInfo(infoData, ndx, 6, true);		//v3b32
		addInfo(infoData, ndx, 2, false);		//offset

		addInfo(infoData, ndx, 4, true);		//v4b16
		addInfo(infoData, ndx, 4, false);		//offset

		addInfo(infoData, ndx, 8, true);		//v4b32

		//strut {b16, b32, v2b16[11], b32[11]}
		for (int i = 0; i < structData.nestedArraySize; ++i)
		{
			infoData[ndx++] = true;			//16b
			addInfo(infoData, ndx, 1, false);	//offset

			addInfo(infoData, ndx, 2, true);	//32b

			addInfo(infoData, ndx, 22, true);	//v2b16[11]

			addInfo(infoData, ndx, 22, true);	//b32[11]
		}

		addInfo(infoData, ndx, 11, true);		//16b[11]
		infoData[ndx++] = false;				//offset

		addInfo(infoData, ndx, 22, true);		//32b[11]
		addInfo(infoData, ndx, 6, false);		//offset
	}

	//Please check the data and offset
	DE_ASSERT(ndx == static_cast<int>(infoData.size()));
	return infoData;
}

template<typename originType, typename resultType, ShaderTemplate funcOrigin, ShaderTemplate funcResult>
bool compareStruct(const resultType* returned, const originType* original, tcu::TestLog& log)
{
		vector<bool>		resultInfo;
		vector<bool>		originInfo;
		vector<resultType>	resultToCompare;
		vector<originType>	originToCompare;

		switch(funcOrigin)
		{
		case SHADERTEMPLATE_STRIDE16BIT_STD140:
			originInfo = info16bitStd140();
			break;
		case SHADERTEMPLATE_STRIDE16BIT_STD430:
			originInfo = info16bitStd430();
			break;
		case SHADERTEMPLATE_STRIDE32BIT_STD140:
			originInfo = info32bitStd140();
			break;
		case SHADERTEMPLATE_STRIDE32BIT_STD430:
			originInfo = info32bitStd430();
			break;
		case SHADERTEMPLATE_STRIDEMIX_STD140:
			originInfo = infoMixStd140();
			break;
		case SHADERTEMPLATE_STRIDEMIX_STD430:
			originInfo = infoMixStd430();
			break;
		default:
			DE_ASSERT(0);
		}

		switch(funcResult)
		{
		case SHADERTEMPLATE_STRIDE16BIT_STD140:
			resultInfo = info16bitStd140();
			break;
		case SHADERTEMPLATE_STRIDE16BIT_STD430:
			resultInfo = info16bitStd430();
			break;
		case SHADERTEMPLATE_STRIDE32BIT_STD140:
			resultInfo = info32bitStd140();
			break;
		case SHADERTEMPLATE_STRIDE32BIT_STD430:
			resultInfo = info32bitStd430();
			break;
		case SHADERTEMPLATE_STRIDEMIX_STD140:
			resultInfo = infoMixStd140();
			break;
		case SHADERTEMPLATE_STRIDEMIX_STD430:
			resultInfo = infoMixStd430();
			break;
		default:
			DE_ASSERT(0);
		}

		for (unsigned int ndx = 0; ndx < static_cast<unsigned int>(resultInfo.size()); ++ndx)
		{
			if (resultInfo[ndx])
				resultToCompare.push_back(returned[ndx]);
		}

		for (unsigned int ndx = 0; ndx < static_cast<unsigned int>(originInfo.size()); ++ndx)
		{
			if (originInfo[ndx])
				originToCompare.push_back(original[ndx]);
		}

		//Different offset but that same amount of data
		DE_ASSERT(originToCompare.size() == resultToCompare.size());
		for (unsigned int ndx = 0; ndx < static_cast<unsigned int>(originToCompare.size()); ++ndx)
		{
			if (!compare16Bit(originToCompare[ndx], resultToCompare[ndx], RoundingModeFlags(ROUNDINGMODE_RTE | ROUNDINGMODE_RTZ), log))
				return false;
		}
		return true;
}

template<typename originType, typename resultType, ShaderTemplate funcOrigin, ShaderTemplate funcResult>
bool computeCheckStruct (const std::vector<BufferSp>&	originalFloats,
						 const vector<AllocationSp>&	outputAllocs,
						 const std::vector<BufferSp>&	/* expectedOutputs */,
						 tcu::TestLog&					log)
{
	for (deUint32 outputNdx = 0; outputNdx < outputAllocs.size(); ++outputNdx)
	{
		vector<deUint8>	originalBytes;
		originalFloats[outputNdx]->getBytes(originalBytes);

		const resultType*	returned	= static_cast<const resultType*>(outputAllocs[outputNdx]->getHostPtr());
		const originType*	original	= reinterpret_cast<const originType*>(&originalBytes.front());

		if(!compareStruct<originType, resultType, funcOrigin, funcResult>(returned, original, log))
			return false;
	}
	return true;
}

template<typename originType, typename resultType, ShaderTemplate funcOrigin, ShaderTemplate funcResult>
bool graphicsCheckStruct (const std::vector<Resource>&	originalFloats,
							   const vector<AllocationSp>&	outputAllocs,
							   const std::vector<Resource>&	/* expectedOutputs */,
							   tcu::TestLog&				log)
{
	for (deUint32 outputNdx = 0; outputNdx < static_cast<deUint32>(outputAllocs.size()); ++outputNdx)
	{
		vector<deUint8>	originalBytes;
		originalFloats[outputNdx].second->getBytes(originalBytes);

		const resultType*	returned	= static_cast<const resultType*>(outputAllocs[outputNdx]->getHostPtr());
		const originType*	original	= reinterpret_cast<const originType*>(&originalBytes.front());

		if(!compareStruct<originType, resultType, funcOrigin, funcResult>(returned, original, log))
			return false;
	}
	return true;
}

string getStructShaderComponet (const ShaderTemplate component)
{
	switch(component)
	{
	case SHADERTEMPLATE_TYPES:
		return string(
		"%f16       = OpTypeFloat 16\n"
		"%v2f16     = OpTypeVector %f16 2\n"
		"%v3f16     = OpTypeVector %f16 3\n"
		"%v4f16     = OpTypeVector %f16 4\n"
		"%f16ptr    = OpTypePointer Uniform %f16\n"
		"%v2f16ptr  = OpTypePointer Uniform %v2f16\n"
		"%v3f16ptr  = OpTypePointer Uniform %v3f16\n"
		"%v4f16ptr  = OpTypePointer Uniform %v4f16\n"
		"\n"
		"%f32ptr   = OpTypePointer Uniform %f32\n"
		"%v2f32ptr = OpTypePointer Uniform %v2f32\n"
		"%v3f32ptr = OpTypePointer Uniform %v3f32\n"
		"%v4f32ptr = OpTypePointer Uniform %v4f32\n");
	case SHADERTEMPLATE_STRIDE16BIT_STD140:
		return string(
		//struct {f16, v2f16[3]} [11]
		"OpDecorate %v2f16arr3 ArrayStride 16\n"
		"OpMemberDecorate %struct16 0 Offset 0\n"
		"OpMemberDecorate %struct16 1 Offset 16\n"
		"OpDecorate %struct16arr11 ArrayStride 64\n"

		"OpDecorate %f16arr3       ArrayStride 16\n"
		"OpDecorate %v2f16arr11    ArrayStride 16\n"
		"OpDecorate %v3f16arr11    ArrayStride 16\n"
		"OpDecorate %v4f16arr3     ArrayStride 16\n"
		"OpDecorate %f16StructArr7 ArrayStride 1200\n"
		"\n"
		"OpMemberDecorate %f16Struct 0 Offset 0\n"		//f16
		"OpMemberDecorate %f16Struct 1 Offset 4\n"		//v2f16
		"OpMemberDecorate %f16Struct 2 Offset 8\n"		//v3f16
		"OpMemberDecorate %f16Struct 3 Offset 16\n"		//v4f16
		"OpMemberDecorate %f16Struct 4 Offset 32\n"		//f16[3]
		"OpMemberDecorate %f16Struct 5 Offset 80\n"		//struct {f16, v2f16[3]} [11]
		"OpMemberDecorate %f16Struct 6 Offset 784\n"	//v2f16[11]
		"OpMemberDecorate %f16Struct 7 Offset 960\n"	//f16
		"OpMemberDecorate %f16Struct 8 Offset 976\n"	//v3f16[11]
		"OpMemberDecorate %f16Struct 9 Offset 1152\n");	//v4f16[3]

	case SHADERTEMPLATE_STRIDE16BIT_STD430:
		return string(
		//struct {f16, v2f16[3]} [11]
		"OpDecorate %v2f16arr3 ArrayStride 4\n"
		"OpMemberDecorate %struct16 0 Offset 0\n"
		"OpMemberDecorate %struct16 1 Offset 4\n"
		"OpDecorate %struct16arr11 ArrayStride 16\n"

		"OpDecorate %f16arr3    ArrayStride 2\n"
		"OpDecorate %v2f16arr11 ArrayStride 4\n"
		"OpDecorate %v3f16arr11 ArrayStride 8\n"
		"OpDecorate %v4f16arr3  ArrayStride 8\n"
		"OpDecorate %f16StructArr7 ArrayStride 368\n"
		"\n"
		"OpMemberDecorate %f16Struct 0 Offset 0\n"		//f16
		"OpMemberDecorate %f16Struct 1 Offset 4\n"		//v2f16
		"OpMemberDecorate %f16Struct 2 Offset 8\n"		//v3f16
		"OpMemberDecorate %f16Struct 3 Offset 16\n"		//v4f16
		"OpMemberDecorate %f16Struct 4 Offset 24\n"		//f16[3]
		"OpMemberDecorate %f16Struct 5 Offset 32\n"		//struct {f16, v2f16[3]} [11]
		"OpMemberDecorate %f16Struct 6 Offset 208\n"	//v2f16[11]
		"OpMemberDecorate %f16Struct 7 Offset 252\n"	//f16
		"OpMemberDecorate %f16Struct 8 Offset 256\n"	//v3f16[11]
		"OpMemberDecorate %f16Struct 9 Offset 344\n");	//v4f16[3]
	case SHADERTEMPLATE_STRIDE32BIT_STD140:
		return string (
		//struct {f32, v2f32[3]} [11]
		"OpDecorate %v2f32arr3 ArrayStride 16\n"
		"OpMemberDecorate %struct32 0 Offset 0\n"
		"OpMemberDecorate %struct32 1 Offset 16\n"
		"OpDecorate %struct32arr11 ArrayStride 64\n"

		"OpDecorate %f32arr3   ArrayStride 16\n"
		"OpDecorate %v2f32arr11 ArrayStride 16\n"
		"OpDecorate %v3f32arr11 ArrayStride 16\n"
		"OpDecorate %v4f32arr3 ArrayStride 16\n"
		"OpDecorate %f32StructArr7 ArrayStride 1216\n"
		"\n"

		"OpMemberDecorate %f32Struct 0 Offset 0\n"		//f32
		"OpMemberDecorate %f32Struct 1 Offset 8\n"		//v2f32
		"OpMemberDecorate %f32Struct 2 Offset 16\n"		//v3f32
		"OpMemberDecorate %f32Struct 3 Offset 32\n"		//v4f32
		"OpMemberDecorate %f32Struct 4 Offset 48\n"		//f32[3]
		"OpMemberDecorate %f32Struct 5 Offset 96\n"		//struct {f32, v2f32[3]} [11]
		"OpMemberDecorate %f32Struct 6 Offset 800\n"	//v2f32[11]
		"OpMemberDecorate %f32Struct 7 Offset 976\n"	//f32
		"OpMemberDecorate %f32Struct 8 Offset 992\n"	//v3f32[11]
		"OpMemberDecorate %f32Struct 9 Offset 1168\n");	//v4f32[3]

	case SHADERTEMPLATE_STRIDE32BIT_STD430:
		return string(
		//struct {f32, v2f32[3]} [11]
		"OpDecorate %v2f32arr3 ArrayStride 8\n"
		"OpMemberDecorate %struct32 0 Offset 0\n"
		"OpMemberDecorate %struct32 1 Offset 8\n"
		"OpDecorate %struct32arr11 ArrayStride 32\n"

		"OpDecorate %f32arr3    ArrayStride 4\n"
		"OpDecorate %v2f32arr11 ArrayStride 8\n"
		"OpDecorate %v3f32arr11 ArrayStride 16\n"
		"OpDecorate %v4f32arr3  ArrayStride 16\n"
		"OpDecorate %f32StructArr7 ArrayStride 736\n"
		"\n"

		"OpMemberDecorate %f32Struct 0 Offset 0\n"		//f32
		"OpMemberDecorate %f32Struct 1 Offset 8\n"		//v2f32
		"OpMemberDecorate %f32Struct 2 Offset 16\n"		//v3f32
		"OpMemberDecorate %f32Struct 3 Offset 32\n"		//v4f32
		"OpMemberDecorate %f32Struct 4 Offset 48\n"		//f32[3]
		"OpMemberDecorate %f32Struct 5 Offset 64\n"		//struct {f32, v2f32[3]}[11]
		"OpMemberDecorate %f32Struct 6 Offset 416\n"	//v2f32[11]
		"OpMemberDecorate %f32Struct 7 Offset 504\n"	//f32
		"OpMemberDecorate %f32Struct 8 Offset 512\n"	//v3f32[11]
		"OpMemberDecorate %f32Struct 9 Offset 688\n");	//v4f32[3]
	case SHADERTEMPLATE_STRIDEMIX_STD140:
		return string(
		"\n"//strutNestedIn {b16, b32, v2b16[11], b32[11]}
		"OpDecorate %v2b16NestedArr11${InOut} ArrayStride 16\n"	//v2b16[11]
		"OpDecorate %b32NestedArr11${InOut} ArrayStride 16\n"	//b32[11]
		"OpMemberDecorate %sNested${InOut} 0 Offset 0\n"		//b16
		"OpMemberDecorate %sNested${InOut} 1 Offset 4\n"		//b32
		"OpMemberDecorate %sNested${InOut} 2 Offset 16\n"		//v2b16[11]
		"OpMemberDecorate %sNested${InOut} 3 Offset 192\n"		//b32[11]
		"OpDecorate %sNestedArr11${InOut} ArrayStride 368\n"	//strutNestedIn[11]
		"\n"//strutIn {b16, b32, v2b16, v2b32, v3b16, v3b32, v4b16, v4b32, strutNestedIn[11], b16In[11], b32bIn[11]}
		"OpDecorate %sb16Arr11${InOut} ArrayStride 16\n"		//b16In[11]
		"OpDecorate %sb32Arr11${InOut} ArrayStride 16\n"		//b32bIn[11]
		"OpMemberDecorate %struct${InOut} 0 Offset 0\n"			//b16
		"OpMemberDecorate %struct${InOut} 1 Offset 4\n"			//b32
		"OpMemberDecorate %struct${InOut} 2 Offset 8\n"			//v2b16
		"OpMemberDecorate %struct${InOut} 3 Offset 16\n"		//v2b32
		"OpMemberDecorate %struct${InOut} 4 Offset 24\n"		//v3b16
		"OpMemberDecorate %struct${InOut} 5 Offset 32\n"		//v3b32
		"OpMemberDecorate %struct${InOut} 6 Offset 48\n"		//v4b16
		"OpMemberDecorate %struct${InOut} 7 Offset 64\n"		//v4b32
		"OpMemberDecorate %struct${InOut} 8 Offset 80\n"		//strutNestedIn[11]
		"OpMemberDecorate %struct${InOut} 9 Offset 4128\n"		//b16In[11]
		"OpMemberDecorate %struct${InOut} 10 Offset 4304\n"		//b32bIn[11]
		"OpDecorate %structArr7${InOut} ArrayStride 4480\n");	//strutIn[7]
	case SHADERTEMPLATE_STRIDEMIX_STD430:
		return string(
		"\n"//strutNestedOut {b16, b32, v2b16[11], b32[11]}
		"OpDecorate %v2b16NestedArr11${InOut} ArrayStride 4\n"	//v2b16[11]
		"OpDecorate %b32NestedArr11${InOut}  ArrayStride 4\n"	//b32[11]
		"OpMemberDecorate %sNested${InOut} 0 Offset 0\n"		//b16
		"OpMemberDecorate %sNested${InOut} 1 Offset 4\n"		//b32
		"OpMemberDecorate %sNested${InOut} 2 Offset 8\n"		//v2b16[11]
		"OpMemberDecorate %sNested${InOut} 3 Offset 52\n"		//b32[11]
		"OpDecorate %sNestedArr11${InOut} ArrayStride 96\n"		//strutNestedOut[11]
		"\n"//strutOut {b16, b32, v2b16, v2b32, v3b16, v3b32, v4b16, v4b32, strutNestedOut[11], b16Out[11], b32bOut[11]}
		"OpDecorate %sb16Arr11${InOut} ArrayStride 2\n"			//b16Out[11]
		"OpDecorate %sb32Arr11${InOut} ArrayStride 4\n"			//b32bOut[11]
		"OpMemberDecorate %struct${InOut} 0 Offset 0\n"			//b16
		"OpMemberDecorate %struct${InOut} 1 Offset 4\n"			//b32
		"OpMemberDecorate %struct${InOut} 2 Offset 8\n"			//v2b16
		"OpMemberDecorate %struct${InOut} 3 Offset 16\n"		//v2b32
		"OpMemberDecorate %struct${InOut} 4 Offset 24\n"		//v3b16
		"OpMemberDecorate %struct${InOut} 5 Offset 32\n"		//v3b32
		"OpMemberDecorate %struct${InOut} 6 Offset 48\n"		//v4b16
		"OpMemberDecorate %struct${InOut} 7 Offset 64\n"		//v4b32
		"OpMemberDecorate %struct${InOut} 8 Offset 80\n"		//strutNestedOut[11]
		"OpMemberDecorate %struct${InOut} 9 Offset 1136\n"		//b16Out[11]
		"OpMemberDecorate %struct${InOut} 10 Offset 1160\n"		//b32bOut[11]
		"OpDecorate %structArr7${InOut} ArrayStride 1216\n");	//strutOut[7]

	default:
		return string("");
	}
}

/*Return string contains spirv loop begin.
 the spec should contains "exeCount" - with name of const i32, it is number of executions
 the spec should contains "loopName" - suffix for all local names
 %Val${loopName} - index which can be used inside loop
 "%ndxArr${loopName}   = OpVariable %fp_i32  Function\n" - has to be defined outside
 The function should be always use with endLoop function*/
std::string beginLoop(const std::map<std::string, std::string>& spec)
{
	const tcu::StringTemplate	loopBegin	(
	"OpStore %ndxArr${loopName} %zero\n"
	"OpBranch %Loop${loopName}\n"
	"%Loop${loopName} = OpLabel\n"
	"OpLoopMerge %MergeLabel1${loopName} %MergeLabel2${loopName} None\n"
	"OpBranch %Label1${loopName}\n"
	"%Label1${loopName} = OpLabel\n"
	"%Val${loopName} = OpLoad %i32 %ndxArr${loopName}\n"
	"%LessThan${loopName} = OpSLessThan %bool %Val${loopName} %${exeCount}\n"
	"OpBranchConditional %LessThan${loopName} %ifLabel${loopName} %MergeLabel1${loopName}\n"
	"%ifLabel${loopName} = OpLabel\n");
	return loopBegin.specialize(spec);
}
/*Return string contains spirv loop end.
 the spec should contains "loopName" - suffix for all local names, suffix should be the same in beginLoop
The function should be always use with beginLoop function*/
std::string endLoop(const std::map<std::string, std::string>& spec)
{
	const tcu::StringTemplate	loopEnd	(
	"OpBranch %MergeLabel2${loopName}\n"
	"%MergeLabel2${loopName} = OpLabel\n"
	"%plusOne${loopName} = OpIAdd %i32 %Val${loopName} %c_i32_1\n"
	"OpStore %ndxArr${loopName} %plusOne${loopName}\n"
	"OpBranch %Loop${loopName}\n"
	"%MergeLabel1${loopName} = OpLabel\n");
	return loopEnd.specialize(spec);
}

void addCompute16bitStorageUniform16To32Group (tcu::TestCaseGroup* group)
{
	tcu::TestContext&				testCtx			= group->getTestContext();
	de::Random						rnd				(deStringHash(group->getName()));
	const int						numElements		= 128;

	const StringTemplate			shaderTemplate	(
		"OpCapability Shader\n"
		"OpCapability ${capability}\n"
		"OpExtension \"SPV_KHR_16bit_storage\"\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\" %id\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"OpDecorate %id BuiltIn GlobalInvocationId\n"

		"${stride}"

		"OpMemberDecorate %SSBO32 0 Offset 0\n"
		"OpMemberDecorate %SSBO16 0 Offset 0\n"
		"OpDecorate %SSBO32 BufferBlock\n"
		"OpDecorate %SSBO16 ${storage}\n"
		"OpDecorate %ssbo32 DescriptorSet 0\n"
		"OpDecorate %ssbo16 DescriptorSet 0\n"
		"OpDecorate %ssbo32 Binding 1\n"
		"OpDecorate %ssbo16 Binding 0\n"

		"${matrix_decor:opt}\n"

		"%bool      = OpTypeBool\n"
		"%void      = OpTypeVoid\n"
		"%voidf     = OpTypeFunction %void\n"
		"%u32       = OpTypeInt 32 0\n"
		"%i32       = OpTypeInt 32 1\n"
		"%f32       = OpTypeFloat 32\n"
		"%v3u32     = OpTypeVector %u32 3\n"
		"%uvec3ptr  = OpTypePointer Input %v3u32\n"
		"%i32ptr    = OpTypePointer Uniform %i32\n"
		"%f32ptr    = OpTypePointer Uniform %f32\n"

		"%zero      = OpConstant %i32 0\n"
		"%c_i32_1   = OpConstant %i32 1\n"
		"%c_i32_2   = OpConstant %i32 2\n"
		"%c_i32_3   = OpConstant %i32 3\n"
		"%c_i32_16  = OpConstant %i32 16\n"
		"%c_i32_32  = OpConstant %i32 32\n"
		"%c_i32_64  = OpConstant %i32 64\n"
		"%c_i32_128 = OpConstant %i32 128\n"
		"%c_i32_ci  = OpConstant %i32 ${constarrayidx}\n"

		"%i32arr    = OpTypeArray %i32 %c_i32_128\n"
		"%f32arr    = OpTypeArray %f32 %c_i32_128\n"

		"${types}\n"
		"${matrix_types:opt}\n"

		"%SSBO32    = OpTypeStruct %${matrix_prefix:opt}${base32}arr\n"
		"%SSBO16    = OpTypeStruct %${matrix_prefix:opt}${base16}arr\n"
		"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
		"%up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
		"%ssbo32    = OpVariable %up_SSBO32 Uniform\n"
		"%ssbo16    = OpVariable %up_SSBO16 Uniform\n"

		"%id        = OpVariable %uvec3ptr Input\n"

		"%main      = OpFunction %void None %voidf\n"
		"%label     = OpLabel\n"
		"%idval     = OpLoad %v3u32 %id\n"
		"%x         = OpCompositeExtract %u32 %idval 0\n"
		"%inloc     = OpAccessChain %${base16}ptr %ssbo16 %zero %${arrayindex} ${index0:opt}\n"
		"%val16     = OpLoad %${base16} %inloc\n"
		"%val32     = ${convert} %${base32} %val16\n"
		"%outloc    = OpAccessChain %${base32}ptr %ssbo32 %zero %x ${index0:opt}\n"
		"             OpStore %outloc %val32\n"
		"${matrix_store:opt}\n"
		"             OpReturn\n"
		"             OpFunctionEnd\n");

	{  // floats
		const char										floatTypes[]	=
			"%f16       = OpTypeFloat 16\n"
			"%f16ptr    = OpTypePointer Uniform %f16\n"
			"%f16arr    = OpTypeArray %f16 %c_i32_128\n"
			"%v2f16     = OpTypeVector %f16 2\n"
			"%v2f32     = OpTypeVector %f32 2\n"
			"%v2f16ptr  = OpTypePointer Uniform %v2f16\n"
			"%v2f32ptr  = OpTypePointer Uniform %v2f32\n"
			"%v2f16arr  = OpTypeArray %v2f16 %c_i32_64\n"
			"%v2f32arr  = OpTypeArray %v2f32 %c_i32_64\n";

		struct CompositeType
		{
			const char*	name;
			const char*	base32;
			const char*	base16;
			const char*	stride;
			bool		useConstantIndex;
			unsigned	constantIndex;
			unsigned	count;
		};

		const CompositeType	cTypes[]	=
		{
			{"scalar",				"f32",		"f16",		"OpDecorate %f32arr ArrayStride 4\nOpDecorate %f16arr ArrayStride 2\n",				false,	0,	numElements},
			{"scalar_const_idx_5",	"f32",		"f16",		"OpDecorate %f32arr ArrayStride 4\nOpDecorate %f16arr ArrayStride 2\n",				true,	5,	numElements},
			{"scalar_const_idx_8",	"f32",		"f16",		"OpDecorate %f32arr ArrayStride 4\nOpDecorate %f16arr ArrayStride 2\n",				true,	8,	numElements},
			{"vector",				"v2f32",	"v2f16",	"OpDecorate %v2f32arr ArrayStride 8\nOpDecorate %v2f16arr ArrayStride 4\n",			false,	0,	numElements / 2},
			{"matrix",				"v2f32",	"v2f16",	"OpDecorate %m4v2f32arr ArrayStride 32\nOpDecorate %m4v2f16arr ArrayStride 16\n",	false,	0,	numElements / 8},
		};

		vector<deFloat16>	float16Data			= getFloat16s(rnd, numElements);
		vector<float>		float32Data;

		float32Data.reserve(numElements);
		for (deUint32 numIdx = 0; numIdx < numElements; ++numIdx)
			float32Data.push_back(deFloat16To32(float16Data[numIdx]));

		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			for (deUint32 tyIdx = 0; tyIdx < DE_LENGTH_OF_ARRAY(cTypes); ++tyIdx)
			{
				ComputeShaderSpec		spec;
				map<string, string>		specs;
				string					testName	= string(CAPABILITIES[capIdx].name) + "_" + cTypes[tyIdx].name + "_float";

				specs["capability"]		= CAPABILITIES[capIdx].cap;
				specs["storage"]		= CAPABILITIES[capIdx].decor;
				specs["stride"]			= cTypes[tyIdx].stride;
				specs["base32"]			= cTypes[tyIdx].base32;
				specs["base16"]			= cTypes[tyIdx].base16;
				specs["types"]			= floatTypes;
				specs["convert"]		= "OpFConvert";
				specs["constarrayidx"]	= de::toString(cTypes[tyIdx].constantIndex);
				if (cTypes[tyIdx].useConstantIndex)
					specs["arrayindex"] = "c_i32_ci";
				else
					specs["arrayindex"] = "x";

				vector<float>			float32DataConstIdx;
				if (cTypes[tyIdx].useConstantIndex)
				{
					const deUint32 numFloats = numElements / cTypes[tyIdx].count;
					for (deUint32 numIdx = 0; numIdx < numElements; ++numIdx)
						float32DataConstIdx.push_back(float32Data[cTypes[tyIdx].constantIndex * numFloats + numIdx % numFloats]);
				}

				if (strcmp(cTypes[tyIdx].name, "matrix") == 0)
				{
					specs["index0"]			= "%zero";
					specs["matrix_prefix"]	= "m4";
					specs["matrix_types"]	=
						"%m4v2f16 = OpTypeMatrix %v2f16 4\n"
						"%m4v2f32 = OpTypeMatrix %v2f32 4\n"
						"%m4v2f16arr = OpTypeArray %m4v2f16 %c_i32_16\n"
						"%m4v2f32arr = OpTypeArray %m4v2f32 %c_i32_16\n";
					specs["matrix_decor"]	=
						"OpMemberDecorate %SSBO32 0 ColMajor\n"
						"OpMemberDecorate %SSBO32 0 MatrixStride 8\n"
						"OpMemberDecorate %SSBO16 0 ColMajor\n"
						"OpMemberDecorate %SSBO16 0 MatrixStride 4\n";
					specs["matrix_store"]	=
						"%inloc_1  = OpAccessChain %v2f16ptr %ssbo16 %zero %x %c_i32_1\n"
						"%val16_1  = OpLoad %v2f16 %inloc_1\n"
						"%val32_1  = OpFConvert %v2f32 %val16_1\n"
						"%outloc_1 = OpAccessChain %v2f32ptr %ssbo32 %zero %x %c_i32_1\n"
						"            OpStore %outloc_1 %val32_1\n"

						"%inloc_2  = OpAccessChain %v2f16ptr %ssbo16 %zero %x %c_i32_2\n"
						"%val16_2  = OpLoad %v2f16 %inloc_2\n"
						"%val32_2  = OpFConvert %v2f32 %val16_2\n"
						"%outloc_2 = OpAccessChain %v2f32ptr %ssbo32 %zero %x %c_i32_2\n"
						"            OpStore %outloc_2 %val32_2\n"

						"%inloc_3  = OpAccessChain %v2f16ptr %ssbo16 %zero %x %c_i32_3\n"
						"%val16_3  = OpLoad %v2f16 %inloc_3\n"
						"%val32_3  = OpFConvert %v2f32 %val16_3\n"
						"%outloc_3 = OpAccessChain %v2f32ptr %ssbo32 %zero %x %c_i32_3\n"
						"            OpStore %outloc_3 %val32_3\n";
				}

				spec.assembly			= shaderTemplate.specialize(specs);
				spec.numWorkGroups		= IVec3(cTypes[tyIdx].count, 1, 1);
				spec.verifyIO			= check32BitFloats;
				spec.inputTypes[0]		= CAPABILITIES[capIdx].dtype;

				spec.inputs.push_back(BufferSp(new Float16Buffer(float16Data)));
				spec.outputs.push_back(BufferSp(new Float32Buffer(cTypes[tyIdx].useConstantIndex ? float32DataConstIdx : float32Data)));
				spec.extensions.push_back("VK_KHR_16bit_storage");
				spec.requestedVulkanFeatures = get16BitStorageFeatures(CAPABILITIES[capIdx].name);

				group->addChild(new SpvAsmComputeShaderCase(testCtx, testName.c_str(), testName.c_str(), spec));
			}
	}

	{  // Integers
		const char		sintTypes[]		=
			"%i16       = OpTypeInt 16 1\n"
			"%i16ptr    = OpTypePointer Uniform %i16\n"
			"%i16arr    = OpTypeArray %i16 %c_i32_128\n"
			"%v4i16     = OpTypeVector %i16 4\n"
			"%v4i32     = OpTypeVector %i32 4\n"
			"%v4i16ptr  = OpTypePointer Uniform %v4i16\n"
			"%v4i32ptr  = OpTypePointer Uniform %v4i32\n"
			"%v4i16arr  = OpTypeArray %v4i16 %c_i32_32\n"
			"%v4i32arr  = OpTypeArray %v4i32 %c_i32_32\n";

		const char		uintTypes[]		=
			"%u16       = OpTypeInt 16 0\n"
			"%u16ptr    = OpTypePointer Uniform %u16\n"
			"%u32ptr    = OpTypePointer Uniform %u32\n"
			"%u16arr    = OpTypeArray %u16 %c_i32_128\n"
			"%u32arr    = OpTypeArray %u32 %c_i32_128\n"
			"%v4u16     = OpTypeVector %u16 4\n"
			"%v4u32     = OpTypeVector %u32 4\n"
			"%v4u16ptr  = OpTypePointer Uniform %v4u16\n"
			"%v4u32ptr  = OpTypePointer Uniform %v4u32\n"
			"%v4u16arr  = OpTypeArray %v4u16 %c_i32_32\n"
			"%v4u32arr  = OpTypeArray %v4u32 %c_i32_32\n";

		struct CompositeType
		{
			const char*	name;
			bool		isSigned;
			const char* types;
			const char*	base32;
			const char*	base16;
			const char* opcode;
			const char*	stride;
			bool		useConstantIndex;
			unsigned	constantIndex;
			unsigned	count;
		};

		const CompositeType	cTypes[]	=
		{
			{"scalar_sint",				true,	sintTypes,	"i32",		"i16",		"OpSConvert",	"OpDecorate %i32arr ArrayStride 4\nOpDecorate %i16arr ArrayStride 2\n",			false,	0,	numElements},
			{"scalar_sint_const_idx_5",	true,	sintTypes,	"i32",		"i16",		"OpSConvert",	"OpDecorate %i32arr ArrayStride 4\nOpDecorate %i16arr ArrayStride 2\n",			true,	5,	numElements},
			{"scalar_sint_const_idx_8",	true,	sintTypes,	"i32",		"i16",		"OpSConvert",	"OpDecorate %i32arr ArrayStride 4\nOpDecorate %i16arr ArrayStride 2\n",			true,	8,	numElements},
			{"scalar_uint",				false,	uintTypes,	"u32",		"u16",		"OpUConvert",	"OpDecorate %u32arr ArrayStride 4\nOpDecorate %u16arr ArrayStride 2\n",			false,	0,	numElements},
			{"scalar_uint_const_idx_5",	false,	uintTypes,	"u32",		"u16",		"OpUConvert",	"OpDecorate %u32arr ArrayStride 4\nOpDecorate %u16arr ArrayStride 2\n",			true,	5,	numElements},
			{"scalar_uint_const_idx_8",	false,	uintTypes,	"u32",		"u16",		"OpUConvert",	"OpDecorate %u32arr ArrayStride 4\nOpDecorate %u16arr ArrayStride 2\n",			true,	8,	numElements},
			{"vector_sint",				true,	sintTypes,	"v4i32",	"v4i16",	"OpSConvert",	"OpDecorate %v4i32arr ArrayStride 16\nOpDecorate %v4i16arr ArrayStride 8\n",	false,	0,	numElements / 4},
			{"vector_uint",				false,	uintTypes,	"v4u32",	"v4u16",	"OpUConvert",	"OpDecorate %v4u32arr ArrayStride 16\nOpDecorate %v4u16arr ArrayStride 8\n",		false,	0,	numElements / 4}
		};

		vector<deInt16>	inputs			= getInt16s(rnd, numElements);
		vector<deInt32> sOutputs;
		vector<deInt32> uOutputs;
		const deUint16	signBitMask		= 0x8000;
		const deUint32	signExtendMask	= 0xffff0000;

		sOutputs.reserve(inputs.size());
		uOutputs.reserve(inputs.size());

		for (deUint32 numNdx = 0; numNdx < inputs.size(); ++numNdx)
		{
			uOutputs.push_back(static_cast<deUint16>(inputs[numNdx]));
			if (inputs[numNdx] & signBitMask)
				sOutputs.push_back(static_cast<deInt32>(inputs[numNdx] | signExtendMask));
			else
				sOutputs.push_back(static_cast<deInt32>(inputs[numNdx]));
		}

		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			for (deUint32 tyIdx = 0; tyIdx < DE_LENGTH_OF_ARRAY(cTypes); ++tyIdx)
			{
				ComputeShaderSpec	spec;
				map<string, string>	specs;
				string				testName	= string(CAPABILITIES[capIdx].name) + "_" + cTypes[tyIdx].name;
				vector<deInt32>		intDataConstIdx;

				if (cTypes[tyIdx].useConstantIndex)
				{
					const deUint32 numInts = numElements / cTypes[tyIdx].count;

					for (deUint32 numIdx = 0; numIdx < numElements; ++numIdx)
					{
						const deInt32 idx = cTypes[tyIdx].constantIndex * numInts + numIdx % numInts;

						if (cTypes[tyIdx].isSigned)
							intDataConstIdx.push_back(sOutputs[idx]);
						else
							intDataConstIdx.push_back(uOutputs[idx]);
					}
				}

				specs["capability"]		= CAPABILITIES[capIdx].cap;
				specs["storage"]		= CAPABILITIES[capIdx].decor;
				specs["stride"]			= cTypes[tyIdx].stride;
				specs["base32"]			= cTypes[tyIdx].base32;
				specs["base16"]			= cTypes[tyIdx].base16;
				specs["types"]			= cTypes[tyIdx].types;
				specs["convert"]		= cTypes[tyIdx].opcode;
				specs["constarrayidx"]	= de::toString(cTypes[tyIdx].constantIndex);
				if (cTypes[tyIdx].useConstantIndex)
					specs["arrayindex"] = "c_i32_ci";
				else
					specs["arrayindex"] = "x";

				spec.assembly			= shaderTemplate.specialize(specs);
				spec.numWorkGroups		= IVec3(cTypes[tyIdx].count, 1, 1);
				spec.inputTypes[0]		= CAPABILITIES[capIdx].dtype;

				spec.inputs.push_back(BufferSp(new Int16Buffer(inputs)));
				if (cTypes[tyIdx].useConstantIndex)
					spec.outputs.push_back(BufferSp(new Int32Buffer(intDataConstIdx)));
				else if (cTypes[tyIdx].isSigned)
					spec.outputs.push_back(BufferSp(new Int32Buffer(sOutputs)));
				else
					spec.outputs.push_back(BufferSp(new Int32Buffer(uOutputs)));
				spec.extensions.push_back("VK_KHR_16bit_storage");
				spec.requestedVulkanFeatures = get16BitStorageFeatures(CAPABILITIES[capIdx].name);

				group->addChild(new SpvAsmComputeShaderCase(testCtx, testName.c_str(), testName.c_str(), spec));
			}
	}
}

void addCompute16bitStorageUniform16To32ChainAccessGroup (tcu::TestCaseGroup* group)
{
	tcu::TestContext&				testCtx			= group->getTestContext();
	de::Random						rnd				(deStringHash(group->getName()));
	const deUint32					structSize		= 24; // In number of 16bit items. Includes padding.
	vector<deFloat16>				inputDataFloat	= getFloat16s(rnd, structSize * 4);
	vector<deInt16>					inputDataInt	= getInt16s(rnd, structSize * 4);
	vector<float>					outputDataFloat;
	vector<deInt32>					outputDataSInt;
	vector<deInt32>					outputDataUInt;
	vector<tcu::UVec4>				indices;

	// Input is an array of a struct that varies on 16bit data type being tested:
	//
	// Float:
	//
	// float16 scalars[3]
	// mat4x3  matrix
	// vec3    vector
	//
	// Int:
	//
	// int16 scalars[3]
	// int16 array2D[4][3]
	// ivec3 vector
	//
	// UInt:
	//
	// uint16 scalars[3]
	// uint16 array2D[4][3]
	// uvec3  vector

	const StringTemplate			shaderTemplate	(
		"                              OpCapability Shader\n"
		"                              OpCapability ${capability}\n"
		"                              OpExtension \"SPV_KHR_16bit_storage\"\n"
		"                         %1 = OpExtInstImport \"GLSL.std.450\"\n"
		"                              OpMemoryModel Logical GLSL450\n"
		"                              OpEntryPoint GLCompute %main \"main\"\n"
		"                              OpExecutionMode %main LocalSize 1 1 1\n"
		"                              OpSource GLSL 430\n"
		"                              OpDecorate %Output BufferBlock\n"
		"                              OpDecorate %dataOutput DescriptorSet 0\n"
		"                              OpDecorate %dataOutput Binding 1\n"
		"                              OpDecorate %scalarArray ArrayStride 2\n"
		"                              OpDecorate %scalarArray2D ArrayStride 8\n"
		"                              OpMemberDecorate %S 0 Offset 0\n"
		"                              OpMemberDecorate %S 1 Offset 8\n"
		"                              ${decoration:opt}\n"
		"                              OpMemberDecorate %S 2 Offset 40\n"
		"                              OpDecorate %_arr_S_uint_4 ArrayStride 48\n"
		"                              OpMemberDecorate %Input 0 Offset 0\n"
		"                              OpMemberDecorate %Output 0 Offset 0\n"
		"                              OpDecorate %Input ${storage}\n"
		"                              OpDecorate %dataInput DescriptorSet 0\n"
		"                              OpDecorate %dataInput Binding 0\n"
		"                       %f16 = OpTypeFloat 16\n"
		"                       %f32 = OpTypeFloat 32\n"
		"                       %i16 = OpTypeInt 16 1\n"
		"                       %i32 = OpTypeInt 32 1\n"
		"                       %u16 = OpTypeInt 16 0\n"
		"                       %u32 = OpTypeInt 32 0\n"
		"                      %void = OpTypeVoid\n"
		"                  %voidFunc = OpTypeFunction %void\n"
		"        %_ptr_Function_uint = OpTypePointer Function %u32\n"
		"                     %v3u32 = OpTypeVector %u32 3\n"
		"          %_ptr_Input_v3u32 = OpTypePointer Input %v3u32\n"
		"                     %int_0 = OpConstant %i32 0\n"
		"                    %uint_3 = OpConstant %u32 3\n"
		"                    %uint_4 = OpConstant %u32 4\n"
		"                        %s0 = OpConstant %u32 ${s0}\n"
		"                        %s1 = OpConstant %u32 ${s1}\n"
		"                        %s2 = OpConstant %u32 ${s2}\n"
		"                        %s3 = OpConstant %u32 ${s3}\n"
		"                    %Output = OpTypeStruct %${type}32\n"
		"       %_ptr_Uniform_Output = OpTypePointer Uniform %Output\n"
		"                %dataOutput = OpVariable %_ptr_Uniform_Output Uniform\n"
		"               %scalarArray = OpTypeArray %${type}16 %uint_3\n"
		"                     %v3f16 = OpTypeVector %f16 3\n"
		"                     %v3i16 = OpTypeVector %i16 3\n"
		"                     %v3u16 = OpTypeVector %u16 3\n"
		"                    %matrix = OpTypeMatrix %v3f16 4\n"
		"             %scalarArray2D = OpTypeArray %scalarArray %uint_4\n"
		"                         %S = OpTypeStruct %scalarArray %${type2D} %v3${type}16\n"
		"             %_arr_S_uint_4 = OpTypeArray %S %uint_4\n"
		"                     %Input = OpTypeStruct %_arr_S_uint_4\n"
		"        %_ptr_Uniform_Input = OpTypePointer Uniform %Input\n"
		"                 %dataInput = OpVariable %_ptr_Uniform_Input Uniform\n"
		"   %_ptr_Uniform_16bit_data = OpTypePointer Uniform %${type}16\n"
		"   %_ptr_Uniform_32bit_data = OpTypePointer Uniform %${type}32\n"
		"                      %main = OpFunction %void None %voidFunc\n"
		"                     %entry = OpLabel\n"
		"                   %dataPtr = ${accessChain}\n"
		"                      %data = OpLoad %${type}16 %dataPtr\n"
		"                 %converted = ${convert}\n"
		"                    %outPtr = OpAccessChain %_ptr_Uniform_32bit_data %dataOutput %int_0\n"
		"                              OpStore %outPtr %converted\n"
		"                              OpReturn\n"
		"                              OpFunctionEnd\n");

	// Generate constant indices for OpChainAccess. We need to use constant values
	// when indexing into structures. This loop generates all permutations.
	for (deUint32 idx0 = 0; idx0 < 4; ++idx0)
		for (deUint32 idx1 = 0; idx1 < 3; ++idx1)
			for (deUint32 idx2 = 0; idx2 < (idx1 == 1u ? 4u : 3u); ++idx2)
				for (deUint32 idx3 = 0; idx3 < (idx1 == 1u ? 3u : 1u); ++idx3)
					indices.push_back(tcu::UVec4(idx0, idx1, idx2, idx3));


	for (deUint32 numIdx = 0; numIdx < (deUint32)indices.size(); ++numIdx)
	{
		const deUint16		signBitMask			= 0x8000;
		const deUint32		signExtendMask		= 0xffff0000;
		// Determine the selected output float for the selected indices.
		const tcu::UVec4	vec					= indices[numIdx];
		// Offsets are in multiples of 16bits.
		const deUint32		fieldOffsets[3][3]	=
		{
			{0u,	1u,	0u},
			{4u,	4u,	1u},
			{20u,	1u,	0u}
		};
		const deUint32		offset				= vec.x() * structSize + fieldOffsets[vec.y()][0] + fieldOffsets[vec.y()][1] * vec.z() + fieldOffsets[vec.y()][2] * vec.w();
		const bool			hasSign				= inputDataInt[offset] & signBitMask;

		outputDataFloat.push_back(deFloat16To32(inputDataFloat[offset]));
		outputDataUInt.push_back((deUint16)inputDataInt[offset]);
		outputDataSInt.push_back((deInt32)(inputDataInt[offset] | (hasSign ? signExtendMask : 0u)));
	}

	for (deUint32 indicesIdx = 0; indicesIdx < (deUint32)indices.size(); ++indicesIdx)
		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
		{
			string						indexString		= de::toString(indices[indicesIdx].x()) + "_" + de::toString(indices[indicesIdx].y()) + "_" + de::toString(indices[indicesIdx].z());
			if (indices[indicesIdx].y() == 1)
				indexString += string("_") + de::toString(indices[indicesIdx].w());

			const string				testNameBase	= string(CAPABILITIES[capIdx].name) + "_" + indexString + "_";
			const ComputeTestFeatures	features		= COMPUTE_TEST_USES_NONE;

			struct DataType
			{
				string		name;
				string		type;
				string		convert;
				string		type2D; // Matrix when using floats. 2D array otherwise.
				BufferSp	inputs;
				BufferSp	outputs;
			};

			const DataType				dataTypes[]		=
			{
				{ "float",	"f", "OpFConvert %f32 %data",	"matrix",			BufferSp(new Float16Buffer(inputDataFloat)),	BufferSp(new Float32Buffer(vector<float>(1, outputDataFloat[indicesIdx])))	},
				{ "int",	"i", "OpSConvert %i32 %data",	"scalarArray2D",	BufferSp(new Int16Buffer(inputDataInt)),		BufferSp(new Int32Buffer(vector<deInt32>(1, outputDataSInt[indicesIdx])))	},
				{ "uint",	"u", "OpUConvert %u32 %data",	"scalarArray2D",	BufferSp(new Int16Buffer(inputDataInt)),		BufferSp(new Int32Buffer(vector<deInt32>(1, outputDataUInt[indicesIdx])))	}
			};

			for (deUint32 dataTypeIdx = 0; dataTypeIdx < DE_LENGTH_OF_ARRAY(dataTypes); ++dataTypeIdx)
			{
				const string				testName	= testNameBase + dataTypes[dataTypeIdx].name;
				map<string, string>			specs;
				ComputeShaderSpec			spec;

				specs["capability"]						= CAPABILITIES[capIdx].cap;
				specs["storage"]						= CAPABILITIES[capIdx].decor;
				specs["s0"]								= de::toString(indices[indicesIdx].x());
				specs["s1"]								= de::toString(indices[indicesIdx].y());
				specs["s2"]								= de::toString(indices[indicesIdx].z());
				specs["s3"]								= de::toString(indices[indicesIdx].w());
				specs["type"]							= dataTypes[dataTypeIdx].type;
				specs["convert"]						= dataTypes[dataTypeIdx].convert;
				specs["type2D"]							= dataTypes[dataTypeIdx].type2D;

				if (indices[indicesIdx].y() == 1)
					specs["accessChain"]				= "OpAccessChain %_ptr_Uniform_16bit_data %dataInput %int_0 %s0 %s1 %s2 %s3";
				else
					specs["accessChain"]				= "OpAccessChain %_ptr_Uniform_16bit_data %dataInput %int_0 %s0 %s1 %s2";

				if (dataTypeIdx == 0)
				{
					spec.verifyIO		= check32BitFloats;
					specs["decoration"]	= "OpMemberDecorate %S 1 ColMajor\nOpMemberDecorate %S 1 MatrixStride 8\n";
				}

				spec.assembly							= shaderTemplate.specialize(specs);
				spec.numWorkGroups						= IVec3(1, 1, 1);
				spec.extensions.push_back				("VK_KHR_16bit_storage");
				spec.requestedVulkanFeatures			= get16BitStorageFeatures(CAPABILITIES[capIdx].name);
				spec.inputTypes[0]						= CAPABILITIES[capIdx].dtype;
				spec.inputs.push_back(dataTypes[dataTypeIdx].inputs);
				spec.outputs.push_back(dataTypes[dataTypeIdx].outputs);

				group->addChild(new SpvAsmComputeShaderCase(testCtx, testName.c_str(), testName.c_str(), spec, features));
			}
		}
}

void addCompute16bitStoragePushConstant16To32Group (tcu::TestCaseGroup* group)
{
	tcu::TestContext&				testCtx			= group->getTestContext();
	de::Random						rnd				(deStringHash(group->getName()));
	const int						numElements		= 64;

	const StringTemplate			shaderTemplate	(
		"OpCapability Shader\n"
		"OpCapability StoragePushConstant16\n"
		"OpExtension \"SPV_KHR_16bit_storage\"\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\" %id\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"OpDecorate %id BuiltIn GlobalInvocationId\n"

		"${stride}"

		"OpDecorate %PC16 Block\n"
		"OpMemberDecorate %PC16 0 Offset 0\n"
		"OpMemberDecorate %SSBO32 0 Offset 0\n"
		"OpDecorate %SSBO32 BufferBlock\n"
		"OpDecorate %ssbo32 DescriptorSet 0\n"
		"OpDecorate %ssbo32 Binding 0\n"

		"${matrix_decor:opt}\n"

		"%void      = OpTypeVoid\n"
		"%voidf     = OpTypeFunction %void\n"
		"%u32       = OpTypeInt 32 0\n"
		"%i32       = OpTypeInt 32 1\n"
		"%f32       = OpTypeFloat 32\n"
		"%v3u32     = OpTypeVector %u32 3\n"
		"%uvec3ptr  = OpTypePointer Input %v3u32\n"
		"%i32ptr    = OpTypePointer Uniform %i32\n"
		"%f32ptr    = OpTypePointer Uniform %f32\n"

		"%zero      = OpConstant %i32 0\n"
		"%c_i32_1   = OpConstant %i32 1\n"
		"%c_i32_8   = OpConstant %i32 8\n"
		"%c_i32_16  = OpConstant %i32 16\n"
		"%c_i32_32  = OpConstant %i32 32\n"
		"%c_i32_64  = OpConstant %i32 64\n"
		"%c_i32_ci  = OpConstant %i32 ${constarrayidx}\n"

		"%i32arr    = OpTypeArray %i32 %c_i32_64\n"
		"%f32arr    = OpTypeArray %f32 %c_i32_64\n"

		"${types}\n"
		"${matrix_types:opt}\n"

		"%PC16      = OpTypeStruct %${matrix_prefix:opt}${base16}arr\n"
		"%pp_PC16   = OpTypePointer PushConstant %PC16\n"
		"%pc16      = OpVariable %pp_PC16 PushConstant\n"
		"%SSBO32    = OpTypeStruct %${matrix_prefix:opt}${base32}arr\n"
		"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
		"%ssbo32    = OpVariable %up_SSBO32 Uniform\n"

		"%id        = OpVariable %uvec3ptr Input\n"

		"%main      = OpFunction %void None %voidf\n"
		"%label     = OpLabel\n"
		"%idval     = OpLoad %v3u32 %id\n"
		"%x         = OpCompositeExtract %u32 %idval 0\n"
		"%inloc     = OpAccessChain %${base16}ptr %pc16 %zero %${arrayindex} ${index0:opt}\n"
		"%val16     = OpLoad %${base16} %inloc\n"
		"%val32     = ${convert} %${base32} %val16\n"
		"%outloc    = OpAccessChain %${base32}ptr %ssbo32 %zero %x ${index0:opt}\n"
		"             OpStore %outloc %val32\n"
		"${matrix_store:opt}\n"
		"             OpReturn\n"
		"             OpFunctionEnd\n");

	{  // floats
		const char										floatTypes[]	=
			"%f16       = OpTypeFloat 16\n"
			"%f16ptr    = OpTypePointer PushConstant %f16\n"
			"%f16arr    = OpTypeArray %f16 %c_i32_64\n"
			"%v4f16     = OpTypeVector %f16 4\n"
			"%v4f32     = OpTypeVector %f32 4\n"
			"%v4f16ptr  = OpTypePointer PushConstant %v4f16\n"
			"%v4f32ptr  = OpTypePointer Uniform %v4f32\n"
			"%v4f16arr  = OpTypeArray %v4f16 %c_i32_16\n"
			"%v4f32arr  = OpTypeArray %v4f32 %c_i32_16\n";

		struct CompositeType
		{
			const char*	name;
			const char*	base32;
			const char*	base16;
			const char*	stride;
			bool		useConstantIndex;
			unsigned	constantIndex;
			unsigned	count;
		};

		const CompositeType	cTypes[]	=
		{
			{"scalar",				"f32",		"f16",		"OpDecorate %f32arr ArrayStride 4\nOpDecorate %f16arr ArrayStride 2\n",				false,	0,	numElements},
			{"scalar_const_idx_5",	"f32",		"f16",		"OpDecorate %f32arr ArrayStride 4\nOpDecorate %f16arr ArrayStride 2\n",				true,	5,	numElements},
			{"scalar_const_idx_8",	"f32",		"f16",		"OpDecorate %f32arr ArrayStride 4\nOpDecorate %f16arr ArrayStride 2\n",				true,	8,	numElements},
			{"vector",				"v4f32",	"v4f16",	"OpDecorate %v4f32arr ArrayStride 16\nOpDecorate %v4f16arr ArrayStride 8\n",		false,	0,	numElements / 4},
			{"matrix",				"v4f32",	"v4f16",	"OpDecorate %m2v4f32arr ArrayStride 32\nOpDecorate %m2v4f16arr ArrayStride 16\n",	false,	0,	numElements / 8},
		};

		vector<deFloat16>	float16Data			= getFloat16s(rnd, numElements);
		vector<float>		float32Data;

		float32Data.reserve(numElements);
		for (deUint32 numIdx = 0; numIdx < numElements; ++numIdx)
			float32Data.push_back(deFloat16To32(float16Data[numIdx]));

		for (deUint32 tyIdx = 0; tyIdx < DE_LENGTH_OF_ARRAY(cTypes); ++tyIdx)
		{
			ComputeShaderSpec		spec;
			map<string, string>		specs;
			string					testName	= string(cTypes[tyIdx].name) + "_float";

			vector<float>			float32DataConstIdx;
			if (cTypes[tyIdx].useConstantIndex)
			{
				const deUint32 numFloats = numElements / cTypes[tyIdx].count;
				for (deUint32 numIdx = 0; numIdx < numElements; ++numIdx)
					float32DataConstIdx.push_back(float32Data[cTypes[tyIdx].constantIndex * numFloats + numIdx % numFloats]);
			}

			specs["stride"]			= cTypes[tyIdx].stride;
			specs["base32"]			= cTypes[tyIdx].base32;
			specs["base16"]			= cTypes[tyIdx].base16;
			specs["types"]			= floatTypes;
			specs["convert"]		= "OpFConvert";
			specs["constarrayidx"]	= de::toString(cTypes[tyIdx].constantIndex);
			if (cTypes[tyIdx].useConstantIndex)
				specs["arrayindex"] = "c_i32_ci";
			else
				specs["arrayindex"] = "x";

			if (strcmp(cTypes[tyIdx].name, "matrix") == 0)
			{
				specs["index0"]			= "%zero";
				specs["matrix_prefix"]	= "m2";
				specs["matrix_types"]	=
					"%m2v4f16 = OpTypeMatrix %v4f16 2\n"
					"%m2v4f32 = OpTypeMatrix %v4f32 2\n"
					"%m2v4f16arr = OpTypeArray %m2v4f16 %c_i32_8\n"
					"%m2v4f32arr = OpTypeArray %m2v4f32 %c_i32_8\n";
				specs["matrix_decor"]	=
					"OpMemberDecorate %SSBO32 0 ColMajor\n"
					"OpMemberDecorate %SSBO32 0 MatrixStride 16\n"
					"OpMemberDecorate %PC16 0 ColMajor\n"
					"OpMemberDecorate %PC16 0 MatrixStride 8\n";
				specs["matrix_store"]	=
					"%inloc_1  = OpAccessChain %v4f16ptr %pc16 %zero %x %c_i32_1\n"
					"%val16_1  = OpLoad %v4f16 %inloc_1\n"
					"%val32_1  = OpFConvert %v4f32 %val16_1\n"
					"%outloc_1 = OpAccessChain %v4f32ptr %ssbo32 %zero %x %c_i32_1\n"
					"            OpStore %outloc_1 %val32_1\n";
			}

			spec.assembly			= shaderTemplate.specialize(specs);
			spec.numWorkGroups		= IVec3(cTypes[tyIdx].count, 1, 1);
			spec.verifyIO			= check32BitFloats;
			spec.pushConstants		= BufferSp(new Float16Buffer(float16Data));

			spec.outputs.push_back(BufferSp(new Float32Buffer(cTypes[tyIdx].useConstantIndex ? float32DataConstIdx : float32Data)));
			spec.extensions.push_back("VK_KHR_16bit_storage");
			spec.requestedVulkanFeatures.ext16BitStorage = EXT16BITSTORAGEFEATURES_PUSH_CONSTANT;

			group->addChild(new SpvAsmComputeShaderCase(testCtx, testName.c_str(), testName.c_str(), spec));
		}
	}
	{// integers
		const char		sintTypes[]		=
			"%i16       = OpTypeInt 16 1\n"
			"%i16ptr    = OpTypePointer PushConstant %i16\n"
			"%i16arr    = OpTypeArray %i16 %c_i32_64\n"
			"%v2i16     = OpTypeVector %i16 2\n"
			"%v2i32     = OpTypeVector %i32 2\n"
			"%v2i16ptr  = OpTypePointer PushConstant %v2i16\n"
			"%v2i32ptr  = OpTypePointer Uniform %v2i32\n"
			"%v2i16arr  = OpTypeArray %v2i16 %c_i32_32\n"
			"%v2i32arr  = OpTypeArray %v2i32 %c_i32_32\n";

		const char		uintTypes[]		=
			"%u16       = OpTypeInt 16 0\n"
			"%u16ptr    = OpTypePointer PushConstant %u16\n"
			"%u32ptr    = OpTypePointer Uniform %u32\n"
			"%u16arr    = OpTypeArray %u16 %c_i32_64\n"
			"%u32arr    = OpTypeArray %u32 %c_i32_64\n"
			"%v2u16     = OpTypeVector %u16 2\n"
			"%v2u32     = OpTypeVector %u32 2\n"
			"%v2u16ptr  = OpTypePointer PushConstant %v2u16\n"
			"%v2u32ptr  = OpTypePointer Uniform %v2u32\n"
			"%v2u16arr  = OpTypeArray %v2u16 %c_i32_32\n"
			"%v2u32arr  = OpTypeArray %v2u32 %c_i32_32\n";

		struct CompositeType
		{
			const char*	name;
			bool		isSigned;
			const char* types;
			const char*	base32;
			const char*	base16;
			const char* opcode;
			const char*	stride;
			bool		useConstantIndex;
			unsigned	constantIndex;
			unsigned	count;
		};

		const CompositeType	cTypes[]	=
		{
			{"scalar_sint",				true,	sintTypes,	"i32",		"i16",		"OpSConvert",	"OpDecorate %i32arr ArrayStride 4\nOpDecorate %i16arr ArrayStride 2\n",			false,	0,	numElements},
			{"scalar_sint_const_idx_5",	true,	sintTypes,	"i32",		"i16",		"OpSConvert",	"OpDecorate %i32arr ArrayStride 4\nOpDecorate %i16arr ArrayStride 2\n",			true,	5,	numElements},
			{"scalar_sint_const_idx_8",	true,	sintTypes,	"i32",		"i16",		"OpSConvert",	"OpDecorate %i32arr ArrayStride 4\nOpDecorate %i16arr ArrayStride 2\n",			true,	8,	numElements},
			{"scalar_uint",				false,	uintTypes,	"u32",		"u16",		"OpUConvert",	"OpDecorate %u32arr ArrayStride 4\nOpDecorate %u16arr ArrayStride 2\n",			false,	0,	numElements},
			{"scalar_uint_const_idx_5",	false,	uintTypes,	"u32",		"u16",		"OpUConvert",	"OpDecorate %u32arr ArrayStride 4\nOpDecorate %u16arr ArrayStride 2\n",			true,	5,	numElements},
			{"scalar_uint_const_idx_8",	false,	uintTypes,	"u32",		"u16",		"OpUConvert",	"OpDecorate %u32arr ArrayStride 4\nOpDecorate %u16arr ArrayStride 2\n",			true,	8,	numElements},
			{"vector_sint",				true,	sintTypes,	"v2i32",	"v2i16",	"OpSConvert",	"OpDecorate %v2i32arr ArrayStride 8\nOpDecorate %v2i16arr ArrayStride 4\n",		false,	0,	numElements / 2},
			{"vector_uint",				false,	uintTypes,	"v2u32",	"v2u16",	"OpUConvert",	"OpDecorate %v2u32arr ArrayStride 8\nOpDecorate %v2u16arr ArrayStride 4\n",		false,	0,	numElements / 2},
		};

		vector<deInt16>	inputs			= getInt16s(rnd, numElements);
		vector<deInt32> sOutputs;
		vector<deInt32> uOutputs;
		const deUint16	signBitMask		= 0x8000;
		const deUint32	signExtendMask	= 0xffff0000;

		sOutputs.reserve(inputs.size());
		uOutputs.reserve(inputs.size());

		for (deUint32 numNdx = 0; numNdx < inputs.size(); ++numNdx)
		{
			uOutputs.push_back(static_cast<deUint16>(inputs[numNdx]));
			if (inputs[numNdx] & signBitMask)
				sOutputs.push_back(static_cast<deInt32>(inputs[numNdx] | signExtendMask));
			else
				sOutputs.push_back(static_cast<deInt32>(inputs[numNdx]));
		}

		for (deUint32 tyIdx = 0; tyIdx < DE_LENGTH_OF_ARRAY(cTypes); ++tyIdx)
		{
			ComputeShaderSpec		spec;
			map<string, string>		specs;
			const char*				testName	= cTypes[tyIdx].name;
			vector<deInt32>			intDataConstIdx;

			if (cTypes[tyIdx].useConstantIndex)
			{
				const deUint32 numInts = numElements / cTypes[tyIdx].count;

				for (deUint32 numIdx = 0; numIdx < numElements; ++numIdx)
				{
					const deInt32 idx = cTypes[tyIdx].constantIndex * numInts + numIdx % numInts;

					if (cTypes[tyIdx].isSigned)
						intDataConstIdx.push_back(sOutputs[idx]);
					else
						intDataConstIdx.push_back(uOutputs[idx]);
				}
			}

			specs["stride"]			= cTypes[tyIdx].stride;
			specs["base32"]			= cTypes[tyIdx].base32;
			specs["base16"]			= cTypes[tyIdx].base16;
			specs["types"]			= cTypes[tyIdx].types;
			specs["convert"]		= cTypes[tyIdx].opcode;
			specs["constarrayidx"]	= de::toString(cTypes[tyIdx].constantIndex);
			if (cTypes[tyIdx].useConstantIndex)
				specs["arrayindex"] = "c_i32_ci";
			else
				specs["arrayindex"] = "x";

			spec.assembly			= shaderTemplate.specialize(specs);
			spec.numWorkGroups		= IVec3(cTypes[tyIdx].count, 1, 1);
			spec.pushConstants		= BufferSp(new Int16Buffer(inputs));

			if (cTypes[tyIdx].useConstantIndex)
				spec.outputs.push_back(BufferSp(new Int32Buffer(intDataConstIdx)));
			else if (cTypes[tyIdx].isSigned)
				spec.outputs.push_back(BufferSp(new Int32Buffer(sOutputs)));
			else
				spec.outputs.push_back(BufferSp(new Int32Buffer(uOutputs)));
			spec.extensions.push_back("VK_KHR_16bit_storage");
			spec.requestedVulkanFeatures.ext16BitStorage = EXT16BITSTORAGEFEATURES_PUSH_CONSTANT;

			group->addChild(new SpvAsmComputeShaderCase(testCtx, testName, testName, spec));
		}
	}
}

void addGraphics16BitStorageUniformInt32To16Group (tcu::TestCaseGroup* testGroup)
{
	de::Random							rnd					(deStringHash(testGroup->getName()));
	map<string, string>					fragments;
	const deUint32						numDataPoints		= 256;
	RGBA								defaultColors[4];
	GraphicsResources					resources;
	vector<string>						extensions;
	const StringTemplate				capabilities		("OpCapability ${cap}\n");
	// inputs and outputs are declared to be vectors of signed integers.
	// However, depending on the test, they may be interpreted as unsiged
	// integers. That won't be a problem as long as we passed the bits
	// in faithfully to the pipeline.
	vector<deInt32>						inputs				= getInt32s(rnd, numDataPoints);
	vector<deInt16>						outputs;

	outputs.reserve(inputs.size());
	for (deUint32 numNdx = 0; numNdx < inputs.size(); ++numNdx)
		outputs.push_back(static_cast<deInt16>(0xffff & inputs[numNdx]));

	resources.inputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Int32Buffer(inputs))));
	resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Int16Buffer(outputs))));

	extensions.push_back("VK_KHR_16bit_storage");
	fragments["extension"]	= "OpExtension \"SPV_KHR_16bit_storage\"";

	getDefaultColors(defaultColors);

	struct IntegerFacts
	{
		const char*	name;
		const char*	type32;
		const char*	type16;
		const char* opcode;
		const char*	isSigned;
	};

	const IntegerFacts	intFacts[]		=
	{
		{"sint",	"%i32",		"%i16",		"OpSConvert",	"1"},
		{"uint",	"%u32",		"%u16",		"OpUConvert",	"0"},
	};

	const StringTemplate	scalarPreMain(
			"${itype16} = OpTypeInt 16 ${signed}\n"
			"%c_i32_256 = OpConstant %i32 256\n"
			"   %up_i32 = OpTypePointer Uniform ${itype32}\n"
			"   %up_i16 = OpTypePointer Uniform ${itype16}\n"
			"   %ra_i32 = OpTypeArray ${itype32} %c_i32_256\n"
			"   %ra_i16 = OpTypeArray ${itype16} %c_i32_256\n"
			"   %SSBO32 = OpTypeStruct %ra_i32\n"
			"   %SSBO16 = OpTypeStruct %ra_i16\n"
			"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"%up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
			"   %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"   %ssbo16 = OpVariable %up_SSBO16 Uniform\n");

	const StringTemplate	scalarDecoration(
			"OpDecorate %ra_i32 ArrayStride 4\n"
			"OpDecorate %ra_i16 ArrayStride 2\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpMemberDecorate %SSBO16 0 Offset 0\n"
			"OpDecorate %SSBO32 ${indecor}\n"
			"OpDecorate %SSBO16 BufferBlock\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo16 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 0\n"
			"OpDecorate %ssbo16 Binding 1\n");

	const StringTemplate	scalarTestFunc(
			"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
			"    %param = OpFunctionParameter %v4f32\n"

			"%entry = OpLabel\n"
			"    %i = OpVariable %fp_i32 Function\n"
			"         OpStore %i %c_i32_0\n"
			"         OpBranch %loop\n"

			" %loop = OpLabel\n"
			"   %15 = OpLoad %i32 %i\n"
			"   %lt = OpSLessThan %bool %15 %c_i32_256\n"
			"         OpLoopMerge %merge %inc None\n"
			"         OpBranchConditional %lt %write %merge\n"

			"%write = OpLabel\n"
			"   %30 = OpLoad %i32 %i\n"
			"  %src = OpAccessChain %up_i32 %ssbo32 %c_i32_0 %30\n"
			"%val32 = OpLoad ${itype32} %src\n"
			"%val16 = ${convert} ${itype16} %val32\n"
			"  %dst = OpAccessChain %up_i16 %ssbo16 %c_i32_0 %30\n"
			"         OpStore %dst %val16\n"
			"         OpBranch %inc\n"

			"  %inc = OpLabel\n"
			"   %37 = OpLoad %i32 %i\n"
			"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
			"         OpStore %i %39\n"
			"         OpBranch %loop\n"

			"%merge = OpLabel\n"
			"         OpReturnValue %param\n"

			"OpFunctionEnd\n");

	const StringTemplate	vecPreMain(
			"${itype16} = OpTypeInt 16 ${signed}\n"
			" %c_i32_64 = OpConstant %i32 64\n"
			"%v4itype16 = OpTypeVector ${itype16} 4\n"
			" %up_v4i32 = OpTypePointer Uniform ${v4itype32}\n"
			" %up_v4i16 = OpTypePointer Uniform %v4itype16\n"
			" %ra_v4i32 = OpTypeArray ${v4itype32} %c_i32_64\n"
			" %ra_v4i16 = OpTypeArray %v4itype16 %c_i32_64\n"
			"   %SSBO32 = OpTypeStruct %ra_v4i32\n"
			"   %SSBO16 = OpTypeStruct %ra_v4i16\n"
			"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"%up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
			"   %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"   %ssbo16 = OpVariable %up_SSBO16 Uniform\n");

	const StringTemplate	vecDecoration(
			"OpDecorate %ra_v4i32 ArrayStride 16\n"
			"OpDecorate %ra_v4i16 ArrayStride 8\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpMemberDecorate %SSBO16 0 Offset 0\n"
			"OpDecorate %SSBO32 ${indecor}\n"
			"OpDecorate %SSBO16 BufferBlock\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo16 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 0\n"
			"OpDecorate %ssbo16 Binding 1\n");

	const StringTemplate	vecTestFunc(
			"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
			"    %param = OpFunctionParameter %v4f32\n"

			"%entry = OpLabel\n"
			"    %i = OpVariable %fp_i32 Function\n"
			"         OpStore %i %c_i32_0\n"
			"         OpBranch %loop\n"

			" %loop = OpLabel\n"
			"   %15 = OpLoad %i32 %i\n"
			"   %lt = OpSLessThan %bool %15 %c_i32_64\n"
			"         OpLoopMerge %merge %inc None\n"
			"         OpBranchConditional %lt %write %merge\n"

			"%write = OpLabel\n"
			"   %30 = OpLoad %i32 %i\n"
			"  %src = OpAccessChain %up_v4i32 %ssbo32 %c_i32_0 %30\n"
			"%val32 = OpLoad ${v4itype32} %src\n"
			"%val16 = ${convert} %v4itype16 %val32\n"
			"  %dst = OpAccessChain %up_v4i16 %ssbo16 %c_i32_0 %30\n"
			"         OpStore %dst %val16\n"
			"         OpBranch %inc\n"

			"  %inc = OpLabel\n"
			"   %37 = OpLoad %i32 %i\n"
			"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
			"         OpStore %i %39\n"
			"         OpBranch %loop\n"

			"%merge = OpLabel\n"
			"         OpReturnValue %param\n"

			"OpFunctionEnd\n");

	struct Category
	{
		const char*				name;
		const StringTemplate&	preMain;
		const StringTemplate&	decoration;
		const StringTemplate&	testFunction;
	};

	const Category		categories[]	=
	{
		{"scalar",	scalarPreMain,	scalarDecoration,	scalarTestFunc},
		{"vector",	vecPreMain,		vecDecoration,		vecTestFunc},
	};

	for (deUint32 catIdx = 0; catIdx < DE_LENGTH_OF_ARRAY(categories); ++catIdx)
		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			for (deUint32 factIdx = 0; factIdx < DE_LENGTH_OF_ARRAY(intFacts); ++factIdx)
			{
				map<string, string>	specs;
				string				name		= string(CAPABILITIES[capIdx].name) + "_" + categories[catIdx].name + "_" + intFacts[factIdx].name;

				specs["cap"]					= CAPABILITIES[capIdx].cap;
				specs["indecor"]				= CAPABILITIES[capIdx].decor;
				specs["itype32"]				= intFacts[factIdx].type32;
				specs["v4itype32"]				= "%v4" + string(intFacts[factIdx].type32).substr(1);
				specs["itype16"]				= intFacts[factIdx].type16;
				specs["signed"]					= intFacts[factIdx].isSigned;
				specs["convert"]				= intFacts[factIdx].opcode;

				fragments["pre_main"]			= categories[catIdx].preMain.specialize(specs);
				fragments["testfun"]			= categories[catIdx].testFunction.specialize(specs);
				fragments["capability"]			= capabilities.specialize(specs);
				fragments["decoration"]			= categories[catIdx].decoration.specialize(specs);

				resources.inputs.back().first	= CAPABILITIES[capIdx].dtype;

				createTestsForAllStages(name, defaultColors, defaultColors, fragments, resources, extensions, testGroup, get16BitStorageFeatures(CAPABILITIES[capIdx].name));
			}
}

void addCompute16bitStorageUniform16To16Group (tcu::TestCaseGroup* group)
{
	tcu::TestContext&		testCtx				= group->getTestContext();
	de::Random				rnd					(deStringHash(group->getName()));
	const int				numElements			= 128;
	const vector<deFloat16>	float16Data			= getFloat16s(rnd, numElements);
	const vector<deFloat16>	float16DummyData	(numElements, 0);
	ComputeShaderSpec		spec;

	std::ostringstream		shaderTemplate;
		shaderTemplate<<"OpCapability Shader\n"
			<< "OpCapability StorageUniformBufferBlock16\n"
			<< "OpExtension \"SPV_KHR_16bit_storage\"\n"
			<< "OpMemoryModel Logical GLSL450\n"
			<< "OpEntryPoint GLCompute %main \"main\" %id\n"
			<< "OpExecutionMode %main LocalSize 1 1 1\n"
			<< "OpDecorate %id BuiltIn GlobalInvocationId\n"
			<< "OpDecorate %f16arr ArrayStride 2\n"
			<< "OpMemberDecorate %SSBO_IN 0 Coherent\n"
			<< "OpMemberDecorate %SSBO_OUT 0 Coherent\n"
			<< "OpMemberDecorate %SSBO_IN 0 Offset 0\n"
			<< "OpMemberDecorate %SSBO_OUT 0 Offset 0\n"
			<< "OpDecorate %SSBO_IN BufferBlock\n"
			<< "OpDecorate %SSBO_OUT BufferBlock\n"
			<< "OpDecorate %ssboIN DescriptorSet 0\n"
			<< "OpDecorate %ssboOUT DescriptorSet 0\n"
			<< "OpDecorate %ssboIN Binding 0\n"
			<< "OpDecorate %ssboOUT Binding 1\n"
			<< "\n"
			<< "%bool      = OpTypeBool\n"
			<< "%void      = OpTypeVoid\n"
			<< "%voidf     = OpTypeFunction %void\n"
			<< "%u32       = OpTypeInt 32 0\n"
			<< "%i32       = OpTypeInt 32 1\n"
			<< "%uvec3     = OpTypeVector %u32 3\n"
			<< "%uvec3ptr  = OpTypePointer Input %uvec3\n"
			<< "%f16       = OpTypeFloat 16\n"
			<< "%f16ptr    = OpTypePointer Uniform %f16\n"
			<< "\n"
			<< "%zero      = OpConstant %i32 0\n"
			<< "%c_size    = OpConstant %i32 " << numElements << "\n"
			<< "\n"
			<< "%f16arr    = OpTypeArray %f16 %c_size\n"
			<< "%SSBO_IN   = OpTypeStruct %f16arr\n"
			<< "%SSBO_OUT  = OpTypeStruct %f16arr\n"
			<< "%up_SSBOIN = OpTypePointer Uniform %SSBO_IN\n"
			<< "%up_SSBOOUT = OpTypePointer Uniform %SSBO_OUT\n"
			<< "%ssboIN    = OpVariable %up_SSBOIN Uniform\n"
			<< "%ssboOUT   = OpVariable %up_SSBOOUT Uniform\n"
			<< "\n"
			<< "%id        = OpVariable %uvec3ptr Input\n"
			<< "%main      = OpFunction %void None %voidf\n"
			<< "%label     = OpLabel\n"
			<< "%idval     = OpLoad %uvec3 %id\n"
			<< "%x         = OpCompositeExtract %u32 %idval 0\n"
			<< "%y         = OpCompositeExtract %u32 %idval 1\n"
			<< "\n"
			<< "%inlocx     = OpAccessChain %f16ptr %ssboIN %zero %x \n"
			<< "%valx       = OpLoad %f16 %inlocx\n"
			<< "%outlocx    = OpAccessChain %f16ptr %ssboOUT %zero %x \n"
			<< "             OpStore %outlocx %valx\n"

			<< "%inlocy    = OpAccessChain %f16ptr %ssboIN %zero %y \n"
			<< "%valy      = OpLoad %f16 %inlocy\n"
			<< "%outlocy   = OpAccessChain %f16ptr %ssboOUT %zero %y \n"
			<< "             OpStore %outlocy %valy\n"
			<< "\n"
			<< "             OpReturn\n"
			<< "             OpFunctionEnd\n";

	spec.assembly			= shaderTemplate.str();
	spec.numWorkGroups		= IVec3(numElements, numElements, 1);
	spec.verifyIO			= computeCheckBuffersFloats;
	spec.coherentMemory		= true;
	spec.inputs.push_back(BufferSp(new Float16Buffer(float16Data)));
	spec.outputs.push_back(BufferSp(new Float16Buffer(float16DummyData)));
	spec.extensions.push_back("VK_KHR_16bit_storage");
	spec.requestedVulkanFeatures = get16BitStorageFeatures("uniform_buffer_block");

	group->addChild(new SpvAsmComputeShaderCase(testCtx, "stress_test", "Granularity stress test", spec));
}

void addCompute16bitStorageUniform32To16Group (tcu::TestCaseGroup* group)
{
	tcu::TestContext&				testCtx			= group->getTestContext();
	de::Random						rnd				(deStringHash(group->getName()));
	const int						numElements		= 128;

	const StringTemplate			shaderTemplate	(
		"OpCapability Shader\n"
		"OpCapability ${capability}\n"
		"OpExtension \"SPV_KHR_16bit_storage\"\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\" %id\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"OpDecorate %id BuiltIn GlobalInvocationId\n"

		"${stride}"

		"OpMemberDecorate %SSBO32 0 Offset 0\n"
		"OpMemberDecorate %SSBO16 0 Offset 0\n"
		"OpDecorate %SSBO32 ${storage}\n"
		"OpDecorate %SSBO16 BufferBlock\n"
		"OpDecorate %ssbo32 DescriptorSet 0\n"
		"OpDecorate %ssbo16 DescriptorSet 0\n"
		"OpDecorate %ssbo32 Binding 0\n"
		"OpDecorate %ssbo16 Binding 1\n"

		"${matrix_decor:opt}\n"

		"${rounding:opt}\n"

		"%bool      = OpTypeBool\n"
		"%void      = OpTypeVoid\n"
		"%voidf     = OpTypeFunction %void\n"
		"%u32       = OpTypeInt 32 0\n"
		"%i32       = OpTypeInt 32 1\n"
		"%f32       = OpTypeFloat 32\n"
		"%uvec3     = OpTypeVector %u32 3\n"
		"%uvec3ptr  = OpTypePointer Input %uvec3\n"
		"%i32ptr    = OpTypePointer Uniform %i32\n"
		"%f32ptr    = OpTypePointer Uniform %f32\n"

		"%zero      = OpConstant %i32 0\n"
		"%c_i32_1   = OpConstant %i32 1\n"
		"%c_i32_16  = OpConstant %i32 16\n"
		"%c_i32_32  = OpConstant %i32 32\n"
		"%c_i32_64  = OpConstant %i32 64\n"
		"%c_i32_128 = OpConstant %i32 128\n"

		"%i32arr    = OpTypeArray %i32 %c_i32_128\n"
		"%f32arr    = OpTypeArray %f32 %c_i32_128\n"

		"${types}\n"
		"${matrix_types:opt}\n"

		"%SSBO32    = OpTypeStruct %${matrix_prefix:opt}${base32}arr\n"
		"%SSBO16    = OpTypeStruct %${matrix_prefix:opt}${base16}arr\n"
		"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
		"%up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
		"%ssbo32    = OpVariable %up_SSBO32 Uniform\n"
		"%ssbo16    = OpVariable %up_SSBO16 Uniform\n"

		"%id        = OpVariable %uvec3ptr Input\n"

		"%main      = OpFunction %void None %voidf\n"
		"%label     = OpLabel\n"
		"%idval     = OpLoad %uvec3 %id\n"
		"%x         = OpCompositeExtract %u32 %idval 0\n"
		"%inloc     = OpAccessChain %${base32}ptr %ssbo32 %zero %x ${index0:opt}\n"
		"%val32     = OpLoad %${base32} %inloc\n"
		"%val16     = ${convert} %${base16} %val32\n"
		"%outloc    = OpAccessChain %${base16}ptr %ssbo16 %zero %x ${index0:opt}\n"
		"             OpStore %outloc %val16\n"
		"${matrix_store:opt}\n"
		"             OpReturn\n"
		"             OpFunctionEnd\n");

	{  // Floats
		const char					floatTypes[]	=
			"%f16       = OpTypeFloat 16\n"
			"%f16ptr    = OpTypePointer Uniform %f16\n"
			"%f16arr    = OpTypeArray %f16 %c_i32_128\n"
			"%v4f16     = OpTypeVector %f16 4\n"
			"%v4f32     = OpTypeVector %f32 4\n"
			"%v4f16ptr  = OpTypePointer Uniform %v4f16\n"
			"%v4f32ptr  = OpTypePointer Uniform %v4f32\n"
			"%v4f16arr  = OpTypeArray %v4f16 %c_i32_32\n"
			"%v4f32arr  = OpTypeArray %v4f32 %c_i32_32\n";

		struct RndMode
		{
			const char*				name;
			const char*				decor;
			ComputeVerifyIOFunc		func;
		};

		const RndMode		rndModes[]		=
		{
			{"rtz",						"OpDecorate %val16  FPRoundingMode RTZ",	computeCheck16BitFloats<ROUNDINGMODE_RTZ>},
			{"rte",						"OpDecorate %val16  FPRoundingMode RTE",	computeCheck16BitFloats<ROUNDINGMODE_RTE>},
			{"unspecified_rnd_mode",	"",											computeCheck16BitFloats<RoundingModeFlags(ROUNDINGMODE_RTE | ROUNDINGMODE_RTZ)>},
		};

		struct CompositeType
		{
			const char*	name;
			const char*	base32;
			const char*	base16;
			const char*	stride;
			unsigned	count;
		};

		const CompositeType	cTypes[]	=
		{
			{"scalar",	"f32",		"f16",		"OpDecorate %f32arr ArrayStride 4\nOpDecorate %f16arr ArrayStride 2\n",				numElements},
			{"vector",	"v4f32",	"v4f16",	"OpDecorate %v4f32arr ArrayStride 16\nOpDecorate %v4f16arr ArrayStride 8\n",		numElements / 4},
			{"matrix",	"v4f32",	"v4f16",	"OpDecorate %m2v4f32arr ArrayStride 32\nOpDecorate %m2v4f16arr ArrayStride 16\n",	numElements / 8},
		};

		vector<float>		float32Data			= getFloat32s(rnd, numElements);
		vector<deFloat16>	float16DummyData	(numElements, 0);

		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			for (deUint32 tyIdx = 0; tyIdx < DE_LENGTH_OF_ARRAY(cTypes); ++tyIdx)
				for (deUint32 rndModeIdx = 0; rndModeIdx < DE_LENGTH_OF_ARRAY(rndModes); ++rndModeIdx)
				{
					ComputeShaderSpec		spec;
					map<string, string>		specs;
					string					testName	= string(CAPABILITIES[capIdx].name) + "_" + cTypes[tyIdx].name + "_float_" + rndModes[rndModeIdx].name;

					specs["capability"]		= CAPABILITIES[capIdx].cap;
					specs["storage"]		= CAPABILITIES[capIdx].decor;
					specs["stride"]			= cTypes[tyIdx].stride;
					specs["base32"]			= cTypes[tyIdx].base32;
					specs["base16"]			= cTypes[tyIdx].base16;
					specs["rounding"]		= rndModes[rndModeIdx].decor;
					specs["types"]			= floatTypes;
					specs["convert"]		= "OpFConvert";

					if (strcmp(cTypes[tyIdx].name, "matrix") == 0)
					{
						if (strcmp(rndModes[rndModeIdx].name, "rtz") == 0)
							specs["rounding"] += "\nOpDecorate %val16_1  FPRoundingMode RTZ\n";
						else if (strcmp(rndModes[rndModeIdx].name, "rte") == 0)
							specs["rounding"] += "\nOpDecorate %val16_1  FPRoundingMode RTE\n";

						specs["index0"]			= "%zero";
						specs["matrix_prefix"]	= "m2";
						specs["matrix_types"]	=
							"%m2v4f16 = OpTypeMatrix %v4f16 2\n"
							"%m2v4f32 = OpTypeMatrix %v4f32 2\n"
							"%m2v4f16arr = OpTypeArray %m2v4f16 %c_i32_16\n"
							"%m2v4f32arr = OpTypeArray %m2v4f32 %c_i32_16\n";
						specs["matrix_decor"]	=
							"OpMemberDecorate %SSBO32 0 ColMajor\n"
							"OpMemberDecorate %SSBO32 0 MatrixStride 16\n"
							"OpMemberDecorate %SSBO16 0 ColMajor\n"
							"OpMemberDecorate %SSBO16 0 MatrixStride 8\n";
						specs["matrix_store"]	=
							"%inloc_1  = OpAccessChain %v4f32ptr %ssbo32 %zero %x %c_i32_1\n"
							"%val32_1  = OpLoad %v4f32 %inloc_1\n"
							"%val16_1  = OpFConvert %v4f16 %val32_1\n"
							"%outloc_1 = OpAccessChain %v4f16ptr %ssbo16 %zero %x %c_i32_1\n"
							"            OpStore %outloc_1 %val16_1\n";
					}

					spec.assembly			= shaderTemplate.specialize(specs);
					spec.numWorkGroups		= IVec3(cTypes[tyIdx].count, 1, 1);
					spec.verifyIO			= rndModes[rndModeIdx].func;
					spec.inputTypes[0]		= CAPABILITIES[capIdx].dtype;

					spec.inputs.push_back(BufferSp(new Float32Buffer(float32Data)));
					// We provided a custom verifyIO in the above in which inputs will be used for checking.
					// So put dummy data in the expected values.
					spec.outputs.push_back(BufferSp(new Float16Buffer(float16DummyData)));
					spec.extensions.push_back("VK_KHR_16bit_storage");
					spec.requestedVulkanFeatures = get16BitStorageFeatures(CAPABILITIES[capIdx].name);

					group->addChild(new SpvAsmComputeShaderCase(testCtx, testName.c_str(), testName.c_str(), spec));
				}
	}

	{  // Integers
		const char		sintTypes[]	=
			"%i16       = OpTypeInt 16 1\n"
			"%i16ptr    = OpTypePointer Uniform %i16\n"
			"%i16arr    = OpTypeArray %i16 %c_i32_128\n"
			"%v2i16     = OpTypeVector %i16 2\n"
			"%v2i32     = OpTypeVector %i32 2\n"
			"%v2i16ptr  = OpTypePointer Uniform %v2i16\n"
			"%v2i32ptr  = OpTypePointer Uniform %v2i32\n"
			"%v2i16arr  = OpTypeArray %v2i16 %c_i32_64\n"
			"%v2i32arr  = OpTypeArray %v2i32 %c_i32_64\n";

		const char		uintTypes[]	=
			"%u16       = OpTypeInt 16 0\n"
			"%u16ptr    = OpTypePointer Uniform %u16\n"
			"%u32ptr    = OpTypePointer Uniform %u32\n"
			"%u16arr    = OpTypeArray %u16 %c_i32_128\n"
			"%u32arr    = OpTypeArray %u32 %c_i32_128\n"
			"%v2u16     = OpTypeVector %u16 2\n"
			"%v2u32     = OpTypeVector %u32 2\n"
			"%v2u16ptr  = OpTypePointer Uniform %v2u16\n"
			"%v2u32ptr  = OpTypePointer Uniform %v2u32\n"
			"%v2u16arr  = OpTypeArray %v2u16 %c_i32_64\n"
			"%v2u32arr  = OpTypeArray %v2u32 %c_i32_64\n";

		struct CompositeType
		{
			const char*	name;
			const char* types;
			const char*	base32;
			const char*	base16;
			const char* opcode;
			const char*	stride;
			unsigned	count;
		};

		const CompositeType	cTypes[]	=
		{
			{"scalar_sint",	sintTypes,	"i32",		"i16",		"OpSConvert",	"OpDecorate %i32arr ArrayStride 4\nOpDecorate %i16arr ArrayStride 2\n",		numElements},
			{"scalar_uint",	uintTypes,	"u32",		"u16",		"OpUConvert",	"OpDecorate %u32arr ArrayStride 4\nOpDecorate %u16arr ArrayStride 2\n",		numElements},
			{"vector_sint",	sintTypes,	"v2i32",	"v2i16",	"OpSConvert",	"OpDecorate %v2i32arr ArrayStride 8\nOpDecorate %v2i16arr ArrayStride 4\n",	numElements / 2},
			{"vector_uint",	uintTypes,	"v2u32",	"v2u16",	"OpUConvert",	"OpDecorate %v2u32arr ArrayStride 8\nOpDecorate %v2u16arr ArrayStride 4\n",	numElements / 2},
		};

		vector<deInt32>	inputs			= getInt32s(rnd, numElements);
		vector<deInt16> outputs;

		outputs.reserve(inputs.size());
		for (deUint32 numNdx = 0; numNdx < inputs.size(); ++numNdx)
			outputs.push_back(static_cast<deInt16>(0xffff & inputs[numNdx]));

		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			for (deUint32 tyIdx = 0; tyIdx < DE_LENGTH_OF_ARRAY(cTypes); ++tyIdx)
			{
				ComputeShaderSpec		spec;
				map<string, string>		specs;
				string					testName	= string(CAPABILITIES[capIdx].name) + "_" + cTypes[tyIdx].name;

				specs["capability"]		= CAPABILITIES[capIdx].cap;
				specs["storage"]		= CAPABILITIES[capIdx].decor;
				specs["stride"]			= cTypes[tyIdx].stride;
				specs["base32"]			= cTypes[tyIdx].base32;
				specs["base16"]			= cTypes[tyIdx].base16;
				specs["types"]			= cTypes[tyIdx].types;
				specs["convert"]		= cTypes[tyIdx].opcode;

				spec.assembly			= shaderTemplate.specialize(specs);
				spec.numWorkGroups		= IVec3(cTypes[tyIdx].count, 1, 1);
				spec.inputTypes[0]		= CAPABILITIES[capIdx].dtype;

				spec.inputs.push_back(BufferSp(new Int32Buffer(inputs)));
				spec.outputs.push_back(BufferSp(new Int16Buffer(outputs)));
				spec.extensions.push_back("VK_KHR_16bit_storage");
				spec.requestedVulkanFeatures = get16BitStorageFeatures(CAPABILITIES[capIdx].name);

				group->addChild(new SpvAsmComputeShaderCase(testCtx, testName.c_str(), testName.c_str(), spec));
			}
	}
}

void addCompute16bitStorageUniform16StructTo32StructGroup (tcu::TestCaseGroup* group)
{
	tcu::TestContext&				testCtx			= group->getTestContext();
	de::Random						rnd				(deStringHash(group->getName()));
	const StringTemplate			shaderTemplate	(
		"OpCapability Shader\n"
		"OpCapability ${capability}\n"
		"OpExtension \"SPV_KHR_16bit_storage\"\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\" %id\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"OpDecorate %id BuiltIn GlobalInvocationId\n"
		"\n"
		"${strideF16}"
		"\n"
		"${strideF32}"
		"\n"
		"OpMemberDecorate %SSBO_IN 0 Offset 0\n"
		"OpMemberDecorate %SSBO_OUT 0 Offset 0\n"
		"OpDecorate %SSBO_IN ${storage}\n"
		"OpDecorate %SSBO_OUT BufferBlock\n"
		"OpDecorate %ssboIN DescriptorSet 0\n"
		"OpDecorate %ssboOUT DescriptorSet 0\n"
		"OpDecorate %ssboIN Binding 0\n"
		"OpDecorate %ssboOUT Binding 1\n"
		"\n"
		"%bool     = OpTypeBool\n"
		"%void     = OpTypeVoid\n"
		"%voidf    = OpTypeFunction %void\n"
		"%u32      = OpTypeInt 32 0\n"
		"%uvec3    = OpTypeVector %u32 3\n"
		"%uvec3ptr = OpTypePointer Input %uvec3\n"
		"\n"
		"%i32      = OpTypeInt 32 1\n"
		"%v2i32    = OpTypeVector %i32 2\n"
		"%v4i32    = OpTypeVector %i32 4\n"
		"\n"
		"%f32      = OpTypeFloat 32\n"
		"%v2f32    = OpTypeVector %f32 2\n"
		"%v3f32    = OpTypeVector %f32 3\n"
		"%v4f32    = OpTypeVector %f32 4\n"
		"${types}\n"
		"\n"
		"%zero = OpConstant %i32 0\n"
		"%c_i32_1 = OpConstant %i32 1\n"
		"%c_i32_2 = OpConstant %i32 2\n"
		"%c_i32_3 = OpConstant %i32 3\n"
		"%c_i32_4 = OpConstant %i32 4\n"
		"%c_i32_5 = OpConstant %i32 5\n"
		"%c_i32_6 = OpConstant %i32 6\n"
		"%c_i32_7 = OpConstant %i32 7\n"
		"%c_i32_8 = OpConstant %i32 8\n"
		"%c_i32_9 = OpConstant %i32 9\n"
		"\n"
		"%c_u32_1 = OpConstant %u32 1\n"
		"%c_u32_3 = OpConstant %u32 3\n"
		"%c_u32_7 = OpConstant %u32 7\n"
		"%c_u32_11 = OpConstant %u32 11\n"
		"\n"
		"%f16arr3       = OpTypeArray %f16 %c_u32_3\n"
		"%v2f16arr3    = OpTypeArray %v2f16 %c_u32_3\n"
		"%v2f16arr11    = OpTypeArray %v2f16 %c_u32_11\n"
		"%v3f16arr11    = OpTypeArray %v3f16 %c_u32_11\n"
		"%v4f16arr3     = OpTypeArray %v4f16 %c_u32_3\n"
		"%struct16      = OpTypeStruct %f16 %v2f16arr3\n"
		"%struct16arr11 = OpTypeArray %struct16 %c_u32_11\n"
		"%f16Struct = OpTypeStruct %f16 %v2f16 %v3f16 %v4f16 %f16arr3 %struct16arr11 %v2f16arr11 %f16 %v3f16arr11 %v4f16arr3\n"
		"\n"
		"%f32arr3   = OpTypeArray %f32 %c_u32_3\n"
		"%v2f32arr3 = OpTypeArray %v2f32 %c_u32_3\n"
		"%v2f32arr11 = OpTypeArray %v2f32 %c_u32_11\n"
		"%v3f32arr11 = OpTypeArray %v3f32 %c_u32_11\n"
		"%v4f32arr3 = OpTypeArray %v4f32 %c_u32_3\n"
		"%struct32      = OpTypeStruct %f32 %v2f32arr3\n"
		"%struct32arr11 = OpTypeArray %struct32 %c_u32_11\n"
		"%f32Struct = OpTypeStruct %f32 %v2f32 %v3f32 %v4f32 %f32arr3 %struct32arr11 %v2f32arr11 %f32 %v3f32arr11 %v4f32arr3\n"
		"\n"
		"%f16StructArr7      = OpTypeArray %f16Struct %c_u32_7\n"
		"%f32StructArr7      = OpTypeArray %f32Struct %c_u32_7\n"
		"%SSBO_IN            = OpTypeStruct %f16StructArr7\n"
		"%SSBO_OUT           = OpTypeStruct %f32StructArr7\n"
		"%up_SSBOIN          = OpTypePointer Uniform %SSBO_IN\n"
		"%up_SSBOOUT         = OpTypePointer Uniform %SSBO_OUT\n"
		"%ssboIN             = OpVariable %up_SSBOIN Uniform\n"
		"%ssboOUT            = OpVariable %up_SSBOOUT Uniform\n"
		"\n"
		"%id        = OpVariable %uvec3ptr Input\n"
		"%main      = OpFunction %void None %voidf\n"
		"%label     = OpLabel\n"
		"\n"
		"%idval     = OpLoad %uvec3 %id\n"
		"%x         = OpCompositeExtract %u32 %idval 0\n"
		"%y         = OpCompositeExtract %u32 %idval 1\n"
		"\n"
		"%f16src  = OpAccessChain %f16ptr %ssboIN %zero %x %zero\n"
		"%val_f16 = OpLoad %f16 %f16src\n"
		"%val_f32 = OpFConvert %f32 %val_f16\n"
		"%f32dst  = OpAccessChain %f32ptr %ssboOUT %zero %x %zero\n"
		"OpStore %f32dst %val_f32\n"
		"\n"
		"%v2f16src  = OpAccessChain %v2f16ptr %ssboIN %zero %x %c_i32_1\n"
		"%val_v2f16 = OpLoad %v2f16 %v2f16src\n"
		"%val_v2f32 = OpFConvert %v2f32 %val_v2f16\n"
		"%v2f32dst  = OpAccessChain %v2f32ptr %ssboOUT %zero %x %c_i32_1\n"
		"OpStore %v2f32dst %val_v2f32\n"
		"\n"
		"%v3f16src  = OpAccessChain %v3f16ptr %ssboIN %zero %x %c_i32_2\n"
		"%val_v3f16 = OpLoad %v3f16 %v3f16src\n"
		"%val_v3f32 = OpFConvert %v3f32 %val_v3f16\n"
		"%v3f32dst  = OpAccessChain %v3f32ptr %ssboOUT %zero %x %c_i32_2\n"
		"OpStore %v3f32dst %val_v3f32\n"
		"\n"
		"%v4f16src  = OpAccessChain %v4f16ptr %ssboIN %zero %x %c_i32_3\n"
		"%val_v4f16 = OpLoad %v4f16 %v4f16src\n"
		"%val_v4f32 = OpFConvert %v4f32 %val_v4f16\n"
		"%v4f32dst  = OpAccessChain %v4f32ptr %ssboOUT %zero %x %c_i32_3\n"
		"OpStore %v4f32dst %val_v4f32\n"
		"\n"
		//struct {f16, v2f16[3]}
		"%Sf16src  = OpAccessChain %f16ptr %ssboIN %zero %x %c_i32_5 %y %zero\n"
		"%Sval_f16 = OpLoad %f16 %Sf16src\n"
		"%Sval_f32 = OpFConvert %f32 %Sval_f16\n"
		"%Sf32dst2  = OpAccessChain %f32ptr %ssboOUT %zero %x %c_i32_5 %y %zero\n"
		"OpStore %Sf32dst2 %Sval_f32\n"
		"\n"
		"%Sv2f16src0   = OpAccessChain %v2f16ptr %ssboIN %zero %x %c_i32_5 %y %c_i32_1 %zero\n"
		"%Sv2f16_0     = OpLoad %v2f16 %Sv2f16src0\n"
		"%Sv2f32_0     = OpFConvert %v2f32 %Sv2f16_0\n"
		"%Sv2f32dst_0  = OpAccessChain %v2f32ptr %ssboOUT %zero %x %c_i32_5 %y %c_i32_1 %zero\n"
		"OpStore %Sv2f32dst_0 %Sv2f32_0\n"
		"\n"
		"%Sv2f16src1  = OpAccessChain %v2f16ptr %ssboIN %zero %x %c_i32_5 %y %c_i32_1 %c_i32_1\n"
		"%Sv2f16_1 = OpLoad %v2f16 %Sv2f16src1\n"
		"%Sv2f32_1 = OpFConvert %v2f32 %Sv2f16_1\n"
		"%Sv2f32dst_1  = OpAccessChain %v2f32ptr %ssboOUT %zero %x %c_i32_5 %y %c_i32_1 %c_i32_1\n"
		"OpStore %Sv2f32dst_1 %Sv2f32_1\n"
		"\n"
		"%Sv2f16src2  = OpAccessChain %v2f16ptr %ssboIN %zero %x %c_i32_5 %y %c_i32_1 %c_i32_2\n"
		"%Sv2f16_2 = OpLoad %v2f16 %Sv2f16src2\n"
		"%Sv2f32_2 = OpFConvert %v2f32 %Sv2f16_2\n"
		"%Sv2f32dst_2  = OpAccessChain %v2f32ptr %ssboOUT %zero %x %c_i32_5 %y %c_i32_1 %c_i32_2\n"
		"OpStore %Sv2f32dst_2 %Sv2f32_2\n"
		"\n"

		"%v2f16src2  = OpAccessChain %v2f16ptr %ssboIN %zero %x %c_i32_6 %y\n"
		"%val2_v2f16 = OpLoad %v2f16 %v2f16src2\n"
		"%val2_v2f32 = OpFConvert %v2f32 %val2_v2f16\n"
		"%v2f32dst2  = OpAccessChain %v2f32ptr %ssboOUT %zero %x %c_i32_6 %y\n"
		"OpStore %v2f32dst2 %val2_v2f32\n"
		"\n"
		"%f16src2  = OpAccessChain %f16ptr %ssboIN %zero %x %c_i32_7\n"
		"%val2_f16 = OpLoad %f16 %f16src2\n"
		"%val2_f32 = OpFConvert %f32 %val2_f16\n"
		"%f32dst2  = OpAccessChain %f32ptr %ssboOUT %zero %x %c_i32_7\n"
		"OpStore %f32dst2 %val2_f32\n"
		"\n"
		"%v3f16src2  = OpAccessChain %v3f16ptr %ssboIN %zero %x %c_i32_8 %y\n"
		"%val2_v3f16 = OpLoad %v3f16 %v3f16src2\n"
		"%val2_v3f32 = OpFConvert %v3f32 %val2_v3f16\n"
		"%v3f32dst2  = OpAccessChain %v3f32ptr %ssboOUT %zero %x %c_i32_8 %y\n"
		"OpStore %v3f32dst2 %val2_v3f32\n"
		"\n"

		//Array with 3 elements
		"%LessThan3 = OpSLessThan %bool %y %c_i32_3\n"
		"OpSelectionMerge %BlockIf None\n"
		"OpBranchConditional %LessThan3 %LabelIf %BlockIf\n"
		"%LabelIf = OpLabel\n"
		"  %f16src3  = OpAccessChain %f16ptr %ssboIN %zero %x %c_i32_4 %y\n"
		"  %val3_f16 = OpLoad %f16 %f16src3\n"
		"  %val3_f32 = OpFConvert %f32 %val3_f16\n"
		"  %f32dst3  = OpAccessChain %f32ptr %ssboOUT %zero %x %c_i32_4 %y\n"
		"  OpStore %f32dst3 %val3_f32\n"
		"\n"
		"  %v4f16src2  = OpAccessChain %v4f16ptr %ssboIN %zero %x %c_i32_9 %y\n"
		"  %val2_v4f16 = OpLoad %v4f16 %v4f16src2\n"
		"  %val2_v4f32 = OpFConvert %v4f32 %val2_v4f16\n"
		"  %v4f32dst2  = OpAccessChain %v4f32ptr %ssboOUT %zero %x %c_i32_9 %y\n"
		"  OpStore %v4f32dst2 %val2_v4f32\n"
		"OpBranch %BlockIf\n"
		"%BlockIf = OpLabel\n"

		"   OpReturn\n"
		"   OpFunctionEnd\n");

	{  // Floats
		vector<float>			float32Data		(getStructSize(SHADERTEMPLATE_STRIDE32BIT_STD430), 0.0f);

		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
		{
			vector<deFloat16>		float16DData	= (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ? data16bitStd430(rnd) : data16bitStd140(rnd);
			ComputeShaderSpec		spec;
			map<string, string>		specs;
			string					testName		= string(CAPABILITIES[capIdx].name);

			specs["capability"]		= CAPABILITIES[capIdx].cap;
			specs["storage"]		= CAPABILITIES[capIdx].decor;
			specs["strideF16"]		= getStructShaderComponet((VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ? SHADERTEMPLATE_STRIDE16BIT_STD430 : SHADERTEMPLATE_STRIDE16BIT_STD140);
			specs["strideF32"]		= getStructShaderComponet(SHADERTEMPLATE_STRIDE32BIT_STD430);
			specs["types"]			= getStructShaderComponet(SHADERTEMPLATE_TYPES);

			spec.assembly			= shaderTemplate.specialize(specs);
			spec.numWorkGroups		= IVec3(structData.structArraySize, structData.nestedArraySize, 1);
			spec.verifyIO			= (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ?  computeCheckStruct<deFloat16, float, SHADERTEMPLATE_STRIDE16BIT_STD430, SHADERTEMPLATE_STRIDE32BIT_STD430>
																										: computeCheckStruct<deFloat16, float, SHADERTEMPLATE_STRIDE16BIT_STD140, SHADERTEMPLATE_STRIDE32BIT_STD430>;
			spec.inputTypes[0]		= CAPABILITIES[capIdx].dtype;
			spec.inputs.push_back(BufferSp(new Float16Buffer(float16DData)));
			spec.outputs.push_back(BufferSp(new Float32Buffer(float32Data)));
			spec.extensions.push_back("VK_KHR_16bit_storage");
			spec.requestedVulkanFeatures = get16BitStorageFeatures(CAPABILITIES[capIdx].name);

			group->addChild(new SpvAsmComputeShaderCase(testCtx, testName.c_str(), testName.c_str(), spec));
		}
	}
}

void addCompute16bitStorageUniform32StructTo16StructGroup (tcu::TestCaseGroup* group)
{
	tcu::TestContext&				testCtx			= group->getTestContext();
	de::Random						rnd				(deStringHash(group->getName()));

	const StringTemplate			shaderTemplate	(
		"OpCapability Shader\n"
		"OpCapability ${capability}\n"
		"OpExtension \"SPV_KHR_16bit_storage\"\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\" %id\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"OpDecorate %id BuiltIn GlobalInvocationId\n"
		"\n"
		"${strideF16}"
		"\n"
		"${strideF32}"
		"\n"
		"OpMemberDecorate %SSBO_IN 0 Offset 0\n"
		"OpMemberDecorate %SSBO_OUT 0 Offset 0\n"
		"OpDecorate %SSBO_IN ${storage}\n"
		"OpDecorate %SSBO_OUT BufferBlock\n"
		"OpDecorate %ssboIN DescriptorSet 0\n"
		"OpDecorate %ssboOUT DescriptorSet 0\n"
		"OpDecorate %ssboIN Binding 0\n"
		"OpDecorate %ssboOUT Binding 1\n"
		"\n"
		"%bool     = OpTypeBool\n"
		"%void     = OpTypeVoid\n"
		"%voidf    = OpTypeFunction %void\n"
		"%u32      = OpTypeInt 32 0\n"
		"%uvec3    = OpTypeVector %u32 3\n"
		"%uvec3ptr = OpTypePointer Input %uvec3\n"
		"\n"
		"%i32      = OpTypeInt 32 1\n"
		"%v2i32    = OpTypeVector %i32 2\n"
		"%v4i32    = OpTypeVector %i32 4\n"
		"\n"
		"%f32      = OpTypeFloat 32\n"
		"%v2f32    = OpTypeVector %f32 2\n"
		"%v3f32    = OpTypeVector %f32 3\n"
		"%v4f32    = OpTypeVector %f32 4\n"
		"${types}\n"
		"\n"
		"%zero = OpConstant %i32 0\n"
		"%c_i32_1 = OpConstant %i32 1\n"
		"%c_i32_2 = OpConstant %i32 2\n"
		"%c_i32_3 = OpConstant %i32 3\n"
		"%c_i32_4 = OpConstant %i32 4\n"
		"%c_i32_5 = OpConstant %i32 5\n"
		"%c_i32_6 = OpConstant %i32 6\n"
		"%c_i32_7 = OpConstant %i32 7\n"
		"%c_i32_8 = OpConstant %i32 8\n"
		"%c_i32_9 = OpConstant %i32 9\n"
		"\n"
		"%c_u32_1 = OpConstant %u32 1\n"
		"%c_u32_3 = OpConstant %u32 3\n"
		"%c_u32_7 = OpConstant %u32 7\n"
		"%c_u32_11 = OpConstant %u32 11\n"
		"\n"
		"%f16arr3       = OpTypeArray %f16 %c_u32_3\n"
		"%v2f16arr3    = OpTypeArray %v2f16 %c_u32_3\n"
		"%v2f16arr11    = OpTypeArray %v2f16 %c_u32_11\n"
		"%v3f16arr11    = OpTypeArray %v3f16 %c_u32_11\n"
		"%v4f16arr3     = OpTypeArray %v4f16 %c_u32_3\n"
		"%struct16      = OpTypeStruct %f16 %v2f16arr3\n"
		"%struct16arr11 = OpTypeArray %struct16 %c_u32_11\n"
		"%f16Struct = OpTypeStruct %f16 %v2f16 %v3f16 %v4f16 %f16arr3 %struct16arr11 %v2f16arr11 %f16 %v3f16arr11 %v4f16arr3\n"
		"\n"
		"%f32arr3   = OpTypeArray %f32 %c_u32_3\n"
		"%v2f32arr3 = OpTypeArray %v2f32 %c_u32_3\n"
		"%v2f32arr11 = OpTypeArray %v2f32 %c_u32_11\n"
		"%v3f32arr11 = OpTypeArray %v3f32 %c_u32_11\n"
		"%v4f32arr3 = OpTypeArray %v4f32 %c_u32_3\n"
		"%struct32      = OpTypeStruct %f32 %v2f32arr3\n"
		"%struct32arr11 = OpTypeArray %struct32 %c_u32_11\n"
		"%f32Struct = OpTypeStruct %f32 %v2f32 %v3f32 %v4f32 %f32arr3 %struct32arr11 %v2f32arr11 %f32 %v3f32arr11 %v4f32arr3\n"
		"\n"
		"%f16StructArr7      = OpTypeArray %f16Struct %c_u32_7\n"
		"%f32StructArr7      = OpTypeArray %f32Struct %c_u32_7\n"
		"%SSBO_IN            = OpTypeStruct %f32StructArr7\n"
		"%SSBO_OUT           = OpTypeStruct %f16StructArr7\n"
		"%up_SSBOIN          = OpTypePointer Uniform %SSBO_IN\n"
		"%up_SSBOOUT         = OpTypePointer Uniform %SSBO_OUT\n"
		"%ssboIN             = OpVariable %up_SSBOIN Uniform\n"
		"%ssboOUT            = OpVariable %up_SSBOOUT Uniform\n"
		"\n"
		"%id        = OpVariable %uvec3ptr Input\n"
		"%main      = OpFunction %void None %voidf\n"
		"%label     = OpLabel\n"
		"\n"
		"%idval     = OpLoad %uvec3 %id\n"
		"%x         = OpCompositeExtract %u32 %idval 0\n"
		"%y         = OpCompositeExtract %u32 %idval 1\n"
		"\n"
		"%f32src  = OpAccessChain %f32ptr %ssboIN %zero %x %zero\n"
		"%val_f32 = OpLoad %f32 %f32src\n"
		"%val_f16 = OpFConvert %f16 %val_f32\n"
		"%f16dst  = OpAccessChain %f16ptr %ssboOUT %zero %x %zero\n"
		"OpStore %f16dst %val_f16\n"
		"\n"
		"%v2f32src  = OpAccessChain %v2f32ptr %ssboIN %zero %x %c_i32_1\n"
		"%val_v2f32 = OpLoad %v2f32 %v2f32src\n"
		"%val_v2f16 = OpFConvert %v2f16 %val_v2f32\n"
		"%v2f16dst  = OpAccessChain %v2f16ptr %ssboOUT %zero %x %c_i32_1\n"
		"OpStore %v2f16dst %val_v2f16\n"
		"\n"
		"%v3f32src  = OpAccessChain %v3f32ptr %ssboIN %zero %x %c_i32_2\n"
		"%val_v3f32 = OpLoad %v3f32 %v3f32src\n"
		"%val_v3f16 = OpFConvert %v3f16 %val_v3f32\n"
		"%v3f16dst  = OpAccessChain %v3f16ptr %ssboOUT %zero %x %c_i32_2\n"
		"OpStore %v3f16dst %val_v3f16\n"
		"\n"
		"%v4f32src  = OpAccessChain %v4f32ptr %ssboIN %zero %x %c_i32_3\n"
		"%val_v4f32 = OpLoad %v4f32 %v4f32src\n"
		"%val_v4f16 = OpFConvert %v4f16 %val_v4f32\n"
		"%v4f16dst  = OpAccessChain %v4f16ptr %ssboOUT %zero %x %c_i32_3\n"
		"OpStore %v4f16dst %val_v4f16\n"
		"\n"

		//struct {f16, v2f16[3]}
		"%Sf32src  = OpAccessChain %f32ptr %ssboIN %zero %x %c_i32_5 %y %zero\n"
		"%Sval_f32 = OpLoad %f32 %Sf32src\n"
		"%Sval_f16 = OpFConvert %f16 %Sval_f32\n"
		"%Sf16dst2  = OpAccessChain %f16ptr %ssboOUT %zero %x %c_i32_5 %y %zero\n"
		"OpStore %Sf16dst2 %Sval_f16\n"
		"\n"
		"%Sv2f32src0   = OpAccessChain %v2f32ptr %ssboIN %zero %x %c_i32_5 %y %c_i32_1 %zero\n"
		"%Sv2f32_0     = OpLoad %v2f32 %Sv2f32src0\n"
		"%Sv2f16_0     = OpFConvert %v2f16 %Sv2f32_0\n"
		"%Sv2f16dst_0  = OpAccessChain %v2f16ptr %ssboOUT %zero %x %c_i32_5 %y %c_i32_1 %zero\n"
		"OpStore %Sv2f16dst_0 %Sv2f16_0\n"
		"\n"
		"%Sv2f32src1  = OpAccessChain %v2f32ptr %ssboIN %zero %x %c_i32_5 %y %c_i32_1 %c_i32_1\n"
		"%Sv2f32_1 = OpLoad %v2f32 %Sv2f32src1\n"
		"%Sv2f16_1 = OpFConvert %v2f16 %Sv2f32_1\n"
		"%Sv2f16dst_1  = OpAccessChain %v2f16ptr %ssboOUT %zero %x %c_i32_5 %y %c_i32_1 %c_i32_1\n"
		"OpStore %Sv2f16dst_1 %Sv2f16_1\n"
		"\n"
		"%Sv2f32src2  = OpAccessChain %v2f32ptr %ssboIN %zero %x %c_i32_5 %y %c_i32_1 %c_i32_2\n"
		"%Sv2f32_2 = OpLoad %v2f32 %Sv2f32src2\n"
		"%Sv2f16_2 = OpFConvert %v2f16 %Sv2f32_2\n"
		"%Sv2f16dst_2  = OpAccessChain %v2f16ptr %ssboOUT %zero %x %c_i32_5 %y %c_i32_1 %c_i32_2\n"
		"OpStore %Sv2f16dst_2 %Sv2f16_2\n"
		"\n"

		"%v2f32src2  = OpAccessChain %v2f32ptr %ssboIN %zero %x %c_i32_6 %y\n"
		"%val2_v2f32 = OpLoad %v2f32 %v2f32src2\n"
		"%val2_v2f16 = OpFConvert %v2f16 %val2_v2f32\n"
		"%v2f16dst2  = OpAccessChain %v2f16ptr %ssboOUT %zero %x %c_i32_6 %y\n"
		"OpStore %v2f16dst2 %val2_v2f16\n"
		"\n"
		"%f32src2  = OpAccessChain %f32ptr %ssboIN %zero %x %c_i32_7\n"
		"%val2_f32 = OpLoad %f32 %f32src2\n"
		"%val2_f16 = OpFConvert %f16 %val2_f32\n"
		"%f16dst2  = OpAccessChain %f16ptr %ssboOUT %zero %x %c_i32_7\n"
		"OpStore %f16dst2 %val2_f16\n"
		"\n"
		"%v3f32src2  = OpAccessChain %v3f32ptr %ssboIN %zero %x %c_i32_8 %y\n"
		"%val2_v3f32 = OpLoad %v3f32 %v3f32src2\n"
		"%val2_v3f16 = OpFConvert %v3f16 %val2_v3f32\n"
		"%v3f16dst2  = OpAccessChain %v3f16ptr %ssboOUT %zero %x %c_i32_8 %y\n"
		"OpStore %v3f16dst2 %val2_v3f16\n"
		"\n"

		//Array with 3 elements
		"%LessThan3 = OpSLessThan %bool %y %c_i32_3\n"
		"OpSelectionMerge %BlockIf None\n"
		"OpBranchConditional %LessThan3 %LabelIf %BlockIf\n"
		"  %LabelIf = OpLabel\n"
		"  %f32src3  = OpAccessChain %f32ptr %ssboIN %zero %x %c_i32_4 %y\n"
		"  %val3_f32 = OpLoad %f32 %f32src3\n"
		"  %val3_f16 = OpFConvert %f16 %val3_f32\n"
		"  %f16dst3  = OpAccessChain %f16ptr %ssboOUT %zero %x %c_i32_4 %y\n"
		"  OpStore %f16dst3 %val3_f16\n"
		"\n"
		"  %v4f32src2  = OpAccessChain %v4f32ptr %ssboIN %zero %x %c_i32_9 %y\n"
		"  %val2_v4f32 = OpLoad %v4f32 %v4f32src2\n"
		"  %val2_v4f16 = OpFConvert %v4f16 %val2_v4f32\n"
		"  %v4f16dst2  = OpAccessChain %v4f16ptr %ssboOUT %zero %x %c_i32_9 %y\n"
		"  OpStore %v4f16dst2 %val2_v4f16\n"
		"OpBranch %BlockIf\n"
		"%BlockIf = OpLabel\n"

		"   OpReturn\n"
		"   OpFunctionEnd\n");

	{  // Floats
		vector<deFloat16>		float16Data		(getStructSize(SHADERTEMPLATE_STRIDE16BIT_STD430), 0u);

		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
		{
			ComputeShaderSpec		spec;
			map<string, string>		specs;
			string					testName		= string(CAPABILITIES[capIdx].name);
			vector<float>			float32DData	= (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ? data32bitStd430(rnd) : data32bitStd140(rnd);

			specs["capability"]		= CAPABILITIES[capIdx].cap;
			specs["storage"]		= CAPABILITIES[capIdx].decor;
			specs["strideF16"]		= getStructShaderComponet(SHADERTEMPLATE_STRIDE16BIT_STD430);
			specs["strideF32"]		= getStructShaderComponet((VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ? SHADERTEMPLATE_STRIDE32BIT_STD430 : SHADERTEMPLATE_STRIDE32BIT_STD140);
			specs["types"]			= getStructShaderComponet(SHADERTEMPLATE_TYPES);

			spec.assembly			= shaderTemplate.specialize(specs);
			spec.numWorkGroups		= IVec3(structData.structArraySize, structData.nestedArraySize, 1);
			spec.verifyIO			= (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ? computeCheckStruct<float, deFloat16, SHADERTEMPLATE_STRIDE32BIT_STD430, SHADERTEMPLATE_STRIDE16BIT_STD430> : computeCheckStruct<float, deFloat16, SHADERTEMPLATE_STRIDE32BIT_STD140, SHADERTEMPLATE_STRIDE16BIT_STD430>;
			spec.inputTypes[0]		= CAPABILITIES[capIdx].dtype;

			spec.inputs.push_back(BufferSp(new Float32Buffer(float32DData)));
			spec.outputs.push_back(BufferSp(new Float16Buffer(float16Data)));
			spec.extensions.push_back("VK_KHR_16bit_storage");
			spec.requestedVulkanFeatures = get16BitStorageFeatures(CAPABILITIES[capIdx].name);

			group->addChild(new SpvAsmComputeShaderCase(testCtx, testName.c_str(), testName.c_str(), spec));
		}
	}
}

void addCompute16bitStructMixedTypesGroup (tcu::TestCaseGroup* group)
{
	tcu::TestContext&		testCtx			= group->getTestContext();
	de::Random				rnd				(deStringHash(group->getName()));
	vector<deInt16>			outData			(getStructSize(SHADERTEMPLATE_STRIDEMIX_STD430), 0u);

	const StringTemplate	shaderTemplate	(
		"OpCapability Shader\n"
		"OpCapability StorageUniformBufferBlock16\n"
		"${capability}\n"
		"OpExtension \"SPV_KHR_storage_buffer_storage_class\"\n"
		"OpExtension \"SPV_KHR_16bit_storage\"\n"
		"OpMemoryModel Logical GLSL450\n"
		"OpEntryPoint GLCompute %main \"main\" %id\n"
		"OpExecutionMode %main LocalSize 1 1 1\n"
		"OpDecorate %id BuiltIn GlobalInvocationId\n"
		"${OutOffsets}"
		"${InOffsets}"
		"\n"//SSBO IN
		"OpMemberDecorate %SSBO_IN 0 Offset 0\n"
		"OpDecorate %ssboIN DescriptorSet 0\n"
		"OpDecorate %SSBO_IN ${storage}\n"
		"OpDecorate %SSBO_OUT BufferBlock\n"
		"OpDecorate %ssboIN Binding 0\n"
		"\n"//SSBO OUT
		"OpMemberDecorate %SSBO_OUT 0 Offset 0\n"
		"OpDecorate %ssboOUT DescriptorSet 0\n"
		"OpDecorate %ssboOUT Binding 1\n"
		"\n"//Types
		"%void  = OpTypeVoid\n"
		"%bool  = OpTypeBool\n"
		"%i16   = OpTypeInt 16 1\n"
		"%v2i16 = OpTypeVector %i16 2\n"
		"%v3i16 = OpTypeVector %i16 3\n"
		"%v4i16 = OpTypeVector %i16 4\n"
		"%i32   = OpTypeInt 32 1\n"
		"%v2i32 = OpTypeVector %i32 2\n"
		"%v3i32 = OpTypeVector %i32 3\n"
		"%v4i32 = OpTypeVector %i32 4\n"
		"%u32   = OpTypeInt 32 0\n"
		"%uvec3 = OpTypeVector %u32 3\n"
		"%f32   = OpTypeFloat 32\n"
		"%v4f32 = OpTypeVector %f32  4\n"
		"%voidf = OpTypeFunction %void\n"
		"\n"//Consta value
		"%zero     = OpConstant %i32 0\n"
		"%c_i32_1  = OpConstant %i32 1\n"
		"%c_i32_2  = OpConstant %i32 2\n"
		"%c_i32_3  = OpConstant %i32 3\n"
		"%c_i32_4  = OpConstant %i32 4\n"
		"%c_i32_5  = OpConstant %i32 5\n"
		"%c_i32_6  = OpConstant %i32 6\n"
		"%c_i32_7  = OpConstant %i32 7\n"
		"%c_i32_8  = OpConstant %i32 8\n"
		"%c_i32_9  = OpConstant %i32 9\n"
		"%c_i32_10 = OpConstant %i32 10\n"
		"%c_i32_11 = OpConstant %i32 11\n"
		"%c_u32_1  = OpConstant %u32 1\n"
		"%c_u32_7  = OpConstant %u32 7\n"
		"%c_u32_11 = OpConstant %u32 11\n"
		"\n"//Arrays & Structs
		"%v2b16NestedArr11In  = OpTypeArray %v2i16 %c_u32_11\n"
		"%b32NestedArr11In    = OpTypeArray %i32 %c_u32_11\n"
		"%sb16Arr11In         = OpTypeArray %i16 %c_u32_11\n"
		"%sb32Arr11In         = OpTypeArray %i32 %c_u32_11\n"
		"%sNestedIn           = OpTypeStruct %i16 %i32 %v2b16NestedArr11In %b32NestedArr11In\n"
		"%sNestedArr11In      = OpTypeArray %sNestedIn %c_u32_11\n"
		"%structIn            = OpTypeStruct %i16 %i32 %v2i16 %v2i32 %v3i16 %v3i32 %v4i16 %v4i32 %sNestedArr11In %sb16Arr11In %sb32Arr11In\n"
		"%structArr7In        = OpTypeArray %structIn %c_u32_7\n"
		"%v2b16NestedArr11Out = OpTypeArray %v2i16 %c_u32_11\n"
		"%b32NestedArr11Out   = OpTypeArray %i32 %c_u32_11\n"
		"%sb16Arr11Out        = OpTypeArray %i16 %c_u32_11\n"
		"%sb32Arr11Out        = OpTypeArray %i32 %c_u32_11\n"
		"%sNestedOut          = OpTypeStruct %i16 %i32 %v2b16NestedArr11Out %b32NestedArr11Out\n"
		"%sNestedArr11Out     = OpTypeArray %sNestedOut %c_u32_11\n"
		"%structOut           = OpTypeStruct %i16 %i32 %v2i16 %v2i32 %v3i16 %v3i32 %v4i16 %v4i32 %sNestedArr11Out %sb16Arr11Out %sb32Arr11Out\n"
		"%structArr7Out       = OpTypeArray %structOut %c_u32_7\n"
		"\n"//Pointers
		"%i16outPtr   = OpTypePointer Uniform %i16\n"
		"%v2i16outPtr = OpTypePointer Uniform %v2i16\n"
		"%v3i16outPtr = OpTypePointer Uniform %v3i16\n"
		"%v4i16outPtr = OpTypePointer Uniform %v4i16\n"
		"%i32outPtr   = OpTypePointer Uniform %i32\n"
		"%v2i32outPtr = OpTypePointer Uniform %v2i32\n"
		"%v3i32outPtr = OpTypePointer Uniform %v3i32\n"
		"%v4i32outPtr = OpTypePointer Uniform %v4i32\n"
		"%fp_i32      = OpTypePointer Function %i32\n"
		"%uvec3ptr    = OpTypePointer Input %uvec3\n"
		"\n"//SSBO IN
		"%SSBO_IN    = OpTypeStruct %structArr7In\n"
		"%up_SSBOIN  = OpTypePointer Uniform %SSBO_IN\n"
		"%ssboIN     = OpVariable %up_SSBOIN Uniform\n"
		"\n"//SSBO OUT
		"%SSBO_OUT   = OpTypeStruct %structArr7Out\n"
		"%up_SSBOOUT = OpTypePointer Uniform %SSBO_OUT\n"
		"%ssboOUT    = OpVariable %up_SSBOOUT Uniform\n"
		"\n"//MAIN
		"%id      = OpVariable %uvec3ptr Input\n"
		"%main    = OpFunction %void None %voidf\n"
		"%label   = OpLabel\n"
		"%idval   = OpLoad %uvec3 %id\n"
		"%x       = OpCompositeExtract %u32 %idval 0\n"
		"%y       = OpCompositeExtract %u32 %idval 1\n"
		"%ndxArrz = OpVariable %fp_i32  Function\n"
		"\n"//strutOut.b16 = strutIn.b16
		"%inP1  = OpAccessChain %i16${inPtr} %ssboIN %zero %x %zero\n"
		"%inV1  = OpLoad %i16 %inP1\n"
		"%outP1 = OpAccessChain %i16outPtr %ssboOUT %zero %x %zero\n"
		"OpStore %outP1 %inV1\n"
		"\n"//strutOut.b32 = strutIn.b32
		"%inP2  = OpAccessChain %i32${inPtr} %ssboIN %zero %x %c_i32_1\n"
		"%inV2  = OpLoad %i32 %inP2\n"
		"%outP2 = OpAccessChain %i32outPtr %ssboOUT %zero %x %c_i32_1\n"
		"OpStore %outP2 %inV2\n"
		"\n"//strutOut.v2b16 = strutIn.v2b16
		"%inP3  = OpAccessChain %v2i16${inPtr} %ssboIN %zero %x %c_i32_2\n"
		"%inV3  = OpLoad %v2i16 %inP3\n"
		"%outP3 = OpAccessChain %v2i16outPtr %ssboOUT %zero %x %c_i32_2\n"
		"OpStore %outP3 %inV3\n"
		"\n"//strutOut.v2b32 = strutIn.v2b32
		"%inP4  = OpAccessChain %v2i32${inPtr} %ssboIN %zero %x %c_i32_3\n"
		"%inV4  = OpLoad %v2i32 %inP4\n"
		"%outP4 = OpAccessChain %v2i32outPtr %ssboOUT %zero %x %c_i32_3\n"
		"OpStore %outP4 %inV4\n"
		"\n"//strutOut.v3b16 = strutIn.v3b16
		"%inP5  = OpAccessChain %v3i16${inPtr} %ssboIN %zero %x %c_i32_4\n"
		"%inV5  = OpLoad %v3i16 %inP5\n"
		"%outP5 = OpAccessChain %v3i16outPtr %ssboOUT %zero %x %c_i32_4\n"
		"OpStore %outP5 %inV5\n"
		"\n"//strutOut.v3b32 = strutIn.v3b32
		"%inP6  = OpAccessChain %v3i32${inPtr} %ssboIN %zero %x %c_i32_5\n"
		"%inV6  = OpLoad %v3i32 %inP6\n"
		"%outP6 = OpAccessChain %v3i32outPtr %ssboOUT %zero %x %c_i32_5\n"
		"OpStore %outP6 %inV6\n"
		"\n"//strutOut.v4b16 = strutIn.v4b16
		"%inP7  = OpAccessChain %v4i16${inPtr} %ssboIN %zero %x %c_i32_6\n"
		"%inV7  = OpLoad %v4i16 %inP7\n"
		"%outP7 = OpAccessChain %v4i16outPtr %ssboOUT %zero %x %c_i32_6\n"
		"OpStore %outP7 %inV7\n"
		"\n"//strutOut.v4b32 = strutIn.v4b32
		"%inP8  = OpAccessChain %v4i32${inPtr} %ssboIN %zero %x %c_i32_7\n"
		"%inV8  = OpLoad %v4i32 %inP8\n"
		"%outP8 = OpAccessChain %v4i32outPtr %ssboOUT %zero %x %c_i32_7\n"
		"OpStore %outP8 %inV8\n"
		"\n"//strutOut.b16[y] = strutIn.b16[y]
		"%inP9  = OpAccessChain %i16${inPtr} %ssboIN %zero %x %c_i32_9 %y\n"
		"%inV9  = OpLoad %i16 %inP9\n"
		"%outP9 = OpAccessChain %i16outPtr %ssboOUT %zero %x %c_i32_9 %y\n"
		"OpStore %outP9 %inV9\n"
		"\n"//strutOut.b32[y] = strutIn.b32[y]
		"%inP10  = OpAccessChain %i32${inPtr} %ssboIN %zero %x %c_i32_10 %y\n"
		"%inV10  = OpLoad %i32 %inP10\n"
		"%outP10 = OpAccessChain %i32outPtr %ssboOUT %zero %x %c_i32_10 %y\n"
		"OpStore %outP10 %inV10\n"
		"\n"//strutOut.strutNestedOut[y].b16 = strutIn.strutNestedIn[y].b16
		"%inP11 = OpAccessChain %i16${inPtr} %ssboIN %zero %x %c_i32_8 %y %zero\n"
		"%inV11 = OpLoad %i16 %inP11\n"
		"%outP11 = OpAccessChain %i16outPtr %ssboOUT %zero %x %c_i32_8 %y %zero\n"
		"OpStore %outP11 %inV11\n"
		"\n"//strutOut.strutNestedOut[y].b32 = strutIn.strutNestedIn[y].b32
		"%inP12 = OpAccessChain %i32${inPtr} %ssboIN %zero %x %c_i32_8 %y %c_i32_1\n"
		"%inV12 = OpLoad %i32 %inP12\n"
		"%outP12 = OpAccessChain %i32outPtr %ssboOUT %zero %x %c_i32_8 %y %c_i32_1\n"
		"OpStore %outP12 %inV12\n"
		"\n"
		"${zBeginLoop}"
		"\n"//strutOut.strutNestedOut[y].v2b16[valNdx] = strutIn.strutNestedIn[y].v2b16[valNdx]
		"%inP13  = OpAccessChain %v2i16${inPtr} %ssboIN %zero %x %c_i32_8 %y %c_i32_2 %Valz\n"
		"%inV13  = OpLoad %v2i16 %inP13\n"
		"%outP13 = OpAccessChain %v2i16outPtr %ssboOUT %zero %x %c_i32_8 %y %c_i32_2 %Valz\n"
		"OpStore %outP13 %inV13\n"
		"\n"//strutOut.strutNestedOut[y].b32[valNdx] = strutIn.strutNestedIn[y].b32[valNdx]
		"%inP14  = OpAccessChain %i32${inPtr} %ssboIN %zero %x %c_i32_8 %y %c_i32_3 %Valz\n"
		"%inV14  = OpLoad %i32 %inP14\n"
		"%outP14 = OpAccessChain %i32outPtr %ssboOUT %zero %x %c_i32_8 %y %c_i32_3 %Valz\n"
		"OpStore %outP14 %inV14\n"
		"\n${zEndLoop}\n"
		"OpBranch %exitLabel\n"
		"%exitLabel = OpLabel\n"
		"OpReturn\n"
		"OpFunctionEnd\n");

	for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
	{  // int
		const bool				isUniform	= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER == CAPABILITIES[capIdx].dtype;
		vector<deInt16>			inData		= isUniform ? dataMixStd140(rnd) : dataMixStd430(rnd);
		ComputeShaderSpec		spec;
		map<string, string>		specsOffset;
		map<string, string>		specsLoop;
		map<string, string>		specs;
		string					testName	= string(CAPABILITIES[capIdx].name);

		specsLoop["exeCount"]	= "c_i32_11";
		specsLoop["loopName"]	= "z";
		specs["zBeginLoop"]		= beginLoop(specsLoop);
		specs["zEndLoop"]		= endLoop(specsLoop);
		specs["capability"]		= isUniform ? "OpCapability " + string(CAPABILITIES[capIdx].cap) : " ";
		specs["inPtr"]			= "outPtr";
		specs["storage"]		= isUniform ? "Block" : "BufferBlock";
		specsOffset["InOut"]	= "In";
		specs["InOffsets"]		= StringTemplate(isUniform ? getStructShaderComponet(SHADERTEMPLATE_STRIDEMIX_STD140) : getStructShaderComponet(SHADERTEMPLATE_STRIDEMIX_STD430)).specialize(specsOffset);
		specsOffset["InOut"]	= "Out";
		specs["OutOffsets"]		= StringTemplate(getStructShaderComponet(SHADERTEMPLATE_STRIDEMIX_STD430)).specialize(specsOffset);

		spec.assembly					= shaderTemplate.specialize(specs);
		spec.numWorkGroups				= IVec3(structData.structArraySize, structData.nestedArraySize, 1);
		spec.verifyIO					= isUniform ? computeCheckStruct<deInt16, deInt16, SHADERTEMPLATE_STRIDEMIX_STD140, SHADERTEMPLATE_STRIDEMIX_STD430> : computeCheckStruct<deInt16, deInt16, SHADERTEMPLATE_STRIDEMIX_STD430, SHADERTEMPLATE_STRIDEMIX_STD430>;
		spec.inputTypes[0]				= CAPABILITIES[capIdx].dtype;
		spec.inputs.push_back			(BufferSp(new Int16Buffer(inData)));
		spec.outputs.push_back			(BufferSp(new Int16Buffer(outData)));
		spec.extensions.push_back		("VK_KHR_16bit_storage");
		spec.requestedVulkanFeatures	= get16BitStorageFeatures(CAPABILITIES[capIdx].name);

		group->addChild(new SpvAsmComputeShaderCase(testCtx, testName.c_str(), testName.c_str(), spec));
	}
}

void addGraphics16BitStorageUniformFloat32To16Group (tcu::TestCaseGroup* testGroup)
{
	de::Random							rnd					(deStringHash(testGroup->getName()));
	map<string, string>					fragments;
	GraphicsResources					resources;
	vector<string>						extensions;
	const deUint32						numDataPoints		= 256;
	RGBA								defaultColors[4];
	vector<float>						float32Data			= getFloat32s(rnd, numDataPoints);
	vector<deFloat16>					float16DummyData	(numDataPoints, 0);
	const StringTemplate				capabilities		("OpCapability ${cap}\n");

	resources.inputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float32Buffer(float32Data))));
	// We use a custom verifyIO to check the result via computing directly from inputs; the contents in outputs do not matter.
	resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float16Buffer(float16DummyData))));

	extensions.push_back("VK_KHR_16bit_storage");
	fragments["extension"]	= "OpExtension \"SPV_KHR_16bit_storage\"";

	struct RndMode
	{
		const char*				name;
		const char*				decor;
		GraphicsVerifyIOFunc	f;
	};

	getDefaultColors(defaultColors);

	{  // scalar cases
		fragments["pre_main"]				=
			"      %f16 = OpTypeFloat 16\n"
			"%c_i32_256 = OpConstant %i32 256\n"
			"   %up_f32 = OpTypePointer Uniform %f32\n"
			"   %up_f16 = OpTypePointer Uniform %f16\n"
			"   %ra_f32 = OpTypeArray %f32 %c_i32_256\n"
			"   %ra_f16 = OpTypeArray %f16 %c_i32_256\n"
			"   %SSBO32 = OpTypeStruct %ra_f32\n"
			"   %SSBO16 = OpTypeStruct %ra_f16\n"
			"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"%up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
			"   %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"   %ssbo16 = OpVariable %up_SSBO16 Uniform\n";

		const StringTemplate decoration		(
			"OpDecorate %ra_f32 ArrayStride 4\n"
			"OpDecorate %ra_f16 ArrayStride 2\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpMemberDecorate %SSBO16 0 Offset 0\n"
			"OpDecorate %SSBO32 ${indecor}\n"
			"OpDecorate %SSBO16 BufferBlock\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo16 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 0\n"
			"OpDecorate %ssbo16 Binding 1\n"
			"${rounddecor}\n");

		fragments["testfun"]				=
			"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
			"    %param = OpFunctionParameter %v4f32\n"

			"%entry = OpLabel\n"
			"    %i = OpVariable %fp_i32 Function\n"
			"         OpStore %i %c_i32_0\n"
			"         OpBranch %loop\n"

			" %loop = OpLabel\n"
			"   %15 = OpLoad %i32 %i\n"
			"   %lt = OpSLessThan %bool %15 %c_i32_256\n"
			"         OpLoopMerge %merge %inc None\n"
			"         OpBranchConditional %lt %write %merge\n"

			"%write = OpLabel\n"
			"   %30 = OpLoad %i32 %i\n"
			"  %src = OpAccessChain %up_f32 %ssbo32 %c_i32_0 %30\n"
			"%val32 = OpLoad %f32 %src\n"
			"%val16 = OpFConvert %f16 %val32\n"
			"  %dst = OpAccessChain %up_f16 %ssbo16 %c_i32_0 %30\n"
			"         OpStore %dst %val16\n"
			"         OpBranch %inc\n"

			"  %inc = OpLabel\n"
			"   %37 = OpLoad %i32 %i\n"
			"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
			"         OpStore %i %39\n"
			"         OpBranch %loop\n"

			"%merge = OpLabel\n"
			"         OpReturnValue %param\n"

			"OpFunctionEnd\n";

		const RndMode	rndModes[] =
		{
			{"rtz",						"OpDecorate %val16  FPRoundingMode RTZ",	graphicsCheck16BitFloats<ROUNDINGMODE_RTZ>},
			{"rte",						"OpDecorate %val16  FPRoundingMode RTE",	graphicsCheck16BitFloats<ROUNDINGMODE_RTE>},
			{"unspecified_rnd_mode",	"",											graphicsCheck16BitFloats<RoundingModeFlags(ROUNDINGMODE_RTE | ROUNDINGMODE_RTZ)>},
		};

		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			for (deUint32 rndModeIdx = 0; rndModeIdx < DE_LENGTH_OF_ARRAY(rndModes); ++rndModeIdx)
			{
				map<string, string>	specs;
				string				testName	= string(CAPABILITIES[capIdx].name) + "_scalar_float_" + rndModes[rndModeIdx].name;

				specs["cap"]					= CAPABILITIES[capIdx].cap;
				specs["indecor"]				= CAPABILITIES[capIdx].decor;
				specs["rounddecor"]				= rndModes[rndModeIdx].decor;

				fragments["capability"]			= capabilities.specialize(specs);
				fragments["decoration"]			= decoration.specialize(specs);

				resources.inputs.back().first	= CAPABILITIES[capIdx].dtype;
				resources.verifyIO				= rndModes[rndModeIdx].f;


				createTestsForAllStages(testName, defaultColors, defaultColors, fragments, resources, extensions, testGroup, get16BitStorageFeatures(CAPABILITIES[capIdx].name));
			}
	}

	{  // vector cases
		fragments["pre_main"]				=
			"      %f16 = OpTypeFloat 16\n"
			" %c_i32_64 = OpConstant %i32 64\n"
			"	 %v4f16 = OpTypeVector %f16 4\n"
			" %up_v4f32 = OpTypePointer Uniform %v4f32\n"
			" %up_v4f16 = OpTypePointer Uniform %v4f16\n"
			" %ra_v4f32 = OpTypeArray %v4f32 %c_i32_64\n"
			" %ra_v4f16 = OpTypeArray %v4f16 %c_i32_64\n"
			"   %SSBO32 = OpTypeStruct %ra_v4f32\n"
			"   %SSBO16 = OpTypeStruct %ra_v4f16\n"
			"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"%up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
			"   %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"   %ssbo16 = OpVariable %up_SSBO16 Uniform\n";

		const StringTemplate decoration		(
			"OpDecorate %ra_v4f32 ArrayStride 16\n"
			"OpDecorate %ra_v4f16 ArrayStride 8\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpMemberDecorate %SSBO16 0 Offset 0\n"
			"OpDecorate %SSBO32 ${indecor}\n"
			"OpDecorate %SSBO16 BufferBlock\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo16 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 0\n"
			"OpDecorate %ssbo16 Binding 1\n"
			"${rounddecor}\n");

		// ssbo16[] <- convert ssbo32[] to 16bit float
		fragments["testfun"]				=
			"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
			"    %param = OpFunctionParameter %v4f32\n"

			"%entry = OpLabel\n"
			"    %i = OpVariable %fp_i32 Function\n"
			"         OpStore %i %c_i32_0\n"
			"         OpBranch %loop\n"

			" %loop = OpLabel\n"
			"   %15 = OpLoad %i32 %i\n"
			"   %lt = OpSLessThan %bool %15 %c_i32_64\n"
			"         OpLoopMerge %merge %inc None\n"
			"         OpBranchConditional %lt %write %merge\n"

			"%write = OpLabel\n"
			"   %30 = OpLoad %i32 %i\n"
			"  %src = OpAccessChain %up_v4f32 %ssbo32 %c_i32_0 %30\n"
			"%val32 = OpLoad %v4f32 %src\n"
			"%val16 = OpFConvert %v4f16 %val32\n"
			"  %dst = OpAccessChain %up_v4f16 %ssbo16 %c_i32_0 %30\n"
			"         OpStore %dst %val16\n"
			"         OpBranch %inc\n"

			"  %inc = OpLabel\n"
			"   %37 = OpLoad %i32 %i\n"
			"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
			"         OpStore %i %39\n"
			"         OpBranch %loop\n"

			"%merge = OpLabel\n"
			"         OpReturnValue %param\n"

			"OpFunctionEnd\n";

		const RndMode	rndModes[] =
		{
			{"rtz",						"OpDecorate %val16  FPRoundingMode RTZ",	graphicsCheck16BitFloats<ROUNDINGMODE_RTZ>},
			{"rte",						"OpDecorate %val16  FPRoundingMode RTE",	graphicsCheck16BitFloats<ROUNDINGMODE_RTE>},
			{"unspecified_rnd_mode",	"",											graphicsCheck16BitFloats<RoundingModeFlags(ROUNDINGMODE_RTE | ROUNDINGMODE_RTZ)>},
		};

		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			for (deUint32 rndModeIdx = 0; rndModeIdx < DE_LENGTH_OF_ARRAY(rndModes); ++rndModeIdx)
			{
				map<string, string>	specs;
				string				testName	= string(CAPABILITIES[capIdx].name) + "_vector_float_" + rndModes[rndModeIdx].name;

				specs["cap"]					= CAPABILITIES[capIdx].cap;
				specs["indecor"]				= CAPABILITIES[capIdx].decor;
				specs["rounddecor"]				= rndModes[rndModeIdx].decor;

				fragments["capability"]			= capabilities.specialize(specs);
				fragments["decoration"]			= decoration.specialize(specs);

				resources.inputs.back().first	= CAPABILITIES[capIdx].dtype;
				resources.verifyIO				= rndModes[rndModeIdx].f;


				createTestsForAllStages(testName, defaultColors, defaultColors, fragments, resources, extensions, testGroup, get16BitStorageFeatures(CAPABILITIES[capIdx].name));
			}
	}

	{  // matrix cases
		fragments["pre_main"]				=
			"       %f16 = OpTypeFloat 16\n"
			"  %c_i32_16 = OpConstant %i32 16\n"
			"     %v4f16 = OpTypeVector %f16 4\n"
			"   %m4x4f32 = OpTypeMatrix %v4f32 4\n"
			"   %m4x4f16 = OpTypeMatrix %v4f16 4\n"
			"  %up_v4f32 = OpTypePointer Uniform %v4f32\n"
			"  %up_v4f16 = OpTypePointer Uniform %v4f16\n"
			"%a16m4x4f32 = OpTypeArray %m4x4f32 %c_i32_16\n"
			"%a16m4x4f16 = OpTypeArray %m4x4f16 %c_i32_16\n"
			"    %SSBO32 = OpTypeStruct %a16m4x4f32\n"
			"    %SSBO16 = OpTypeStruct %a16m4x4f16\n"
			" %up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			" %up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
			"    %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"    %ssbo16 = OpVariable %up_SSBO16 Uniform\n";

		const StringTemplate decoration		(
			"OpDecorate %a16m4x4f32 ArrayStride 64\n"
			"OpDecorate %a16m4x4f16 ArrayStride 32\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpMemberDecorate %SSBO32 0 ColMajor\n"
			"OpMemberDecorate %SSBO32 0 MatrixStride 16\n"
			"OpMemberDecorate %SSBO16 0 Offset 0\n"
			"OpMemberDecorate %SSBO16 0 ColMajor\n"
			"OpMemberDecorate %SSBO16 0 MatrixStride 8\n"
			"OpDecorate %SSBO32 ${indecor}\n"
			"OpDecorate %SSBO16 BufferBlock\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo16 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 0\n"
			"OpDecorate %ssbo16 Binding 1\n"
			"${rounddecor}\n");

		fragments["testfun"]				=
			"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
			"    %param = OpFunctionParameter %v4f32\n"

			"%entry = OpLabel\n"
			"    %i = OpVariable %fp_i32 Function\n"
			"         OpStore %i %c_i32_0\n"
			"         OpBranch %loop\n"

			" %loop = OpLabel\n"
			"   %15 = OpLoad %i32 %i\n"
			"   %lt = OpSLessThan %bool %15 %c_i32_16\n"
			"         OpLoopMerge %merge %inc None\n"
			"         OpBranchConditional %lt %write %merge\n"

			"  %write = OpLabel\n"
			"     %30 = OpLoad %i32 %i\n"
			"  %src_0 = OpAccessChain %up_v4f32 %ssbo32 %c_i32_0 %30 %c_i32_0\n"
			"  %src_1 = OpAccessChain %up_v4f32 %ssbo32 %c_i32_0 %30 %c_i32_1\n"
			"  %src_2 = OpAccessChain %up_v4f32 %ssbo32 %c_i32_0 %30 %c_i32_2\n"
			"  %src_3 = OpAccessChain %up_v4f32 %ssbo32 %c_i32_0 %30 %c_i32_3\n"
			"%val32_0 = OpLoad %v4f32 %src_0\n"
			"%val32_1 = OpLoad %v4f32 %src_1\n"
			"%val32_2 = OpLoad %v4f32 %src_2\n"
			"%val32_3 = OpLoad %v4f32 %src_3\n"
			"%val16_0 = OpFConvert %v4f16 %val32_0\n"
			"%val16_1 = OpFConvert %v4f16 %val32_1\n"
			"%val16_2 = OpFConvert %v4f16 %val32_2\n"
			"%val16_3 = OpFConvert %v4f16 %val32_3\n"
			"  %dst_0 = OpAccessChain %up_v4f16 %ssbo16 %c_i32_0 %30 %c_i32_0\n"
			"  %dst_1 = OpAccessChain %up_v4f16 %ssbo16 %c_i32_0 %30 %c_i32_1\n"
			"  %dst_2 = OpAccessChain %up_v4f16 %ssbo16 %c_i32_0 %30 %c_i32_2\n"
			"  %dst_3 = OpAccessChain %up_v4f16 %ssbo16 %c_i32_0 %30 %c_i32_3\n"
			"           OpStore %dst_0 %val16_0\n"
			"           OpStore %dst_1 %val16_1\n"
			"           OpStore %dst_2 %val16_2\n"
			"           OpStore %dst_3 %val16_3\n"
			"           OpBranch %inc\n"

			"  %inc = OpLabel\n"
			"   %37 = OpLoad %i32 %i\n"
			"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
			"         OpStore %i %39\n"
			"         OpBranch %loop\n"

			"%merge = OpLabel\n"
			"         OpReturnValue %param\n"

			"OpFunctionEnd\n";

		const RndMode	rndModes[] =
		{
			{"rte",						"OpDecorate %val16_0  FPRoundingMode RTE\nOpDecorate %val16_1  FPRoundingMode RTE\nOpDecorate %val16_2  FPRoundingMode RTE\nOpDecorate %val16_3  FPRoundingMode RTE",	graphicsCheck16BitFloats<ROUNDINGMODE_RTE>},
			{"rtz",						"OpDecorate %val16_0  FPRoundingMode RTZ\nOpDecorate %val16_1  FPRoundingMode RTZ\nOpDecorate %val16_2  FPRoundingMode RTZ\nOpDecorate %val16_3  FPRoundingMode RTZ",	graphicsCheck16BitFloats<ROUNDINGMODE_RTZ>},
			{"unspecified_rnd_mode",	"",																																										graphicsCheck16BitFloats<RoundingModeFlags(ROUNDINGMODE_RTE | ROUNDINGMODE_RTZ)>},
		};

		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			for (deUint32 rndModeIdx = 0; rndModeIdx < DE_LENGTH_OF_ARRAY(rndModes); ++rndModeIdx)
			{
				map<string, string>	specs;
				string				testName	= string(CAPABILITIES[capIdx].name) + "_matrix_float_" + rndModes[rndModeIdx].name;

				specs["cap"]					= CAPABILITIES[capIdx].cap;
				specs["indecor"]				= CAPABILITIES[capIdx].decor;
				specs["rounddecor"]				= rndModes[rndModeIdx].decor;

				fragments["capability"]			= capabilities.specialize(specs);
				fragments["decoration"]			= decoration.specialize(specs);

				resources.inputs.back().first	= CAPABILITIES[capIdx].dtype;
				resources.verifyIO				= rndModes[rndModeIdx].f;


				createTestsForAllStages(testName, defaultColors, defaultColors, fragments, resources, extensions, testGroup, get16BitStorageFeatures(CAPABILITIES[capIdx].name));
			}
	}
}

void addGraphics16BitStorageInputOutputFloat32To16Group (tcu::TestCaseGroup* testGroup)
{
	de::Random			rnd					(deStringHash(testGroup->getName()));
	RGBA				defaultColors[4];
	vector<string>		extensions;
	map<string, string>	fragments			= passthruFragments();
	const deUint32		numDataPoints		= 64;
	vector<float>		float32Data			= getFloat32s(rnd, numDataPoints);

	extensions.push_back("VK_KHR_16bit_storage");

	fragments["capability"]				= "OpCapability StorageInputOutput16\n";
	fragments["extension"]				= "OpExtension \"SPV_KHR_16bit_storage\"\n";

	getDefaultColors(defaultColors);

	struct RndMode
	{
		const char*				name;
		const char*				decor;
		RoundingModeFlags		flags;
	};

	const RndMode		rndModes[]		=
	{
		{"rtz",						"OpDecorate %ret  FPRoundingMode RTZ",	ROUNDINGMODE_RTZ},
		{"rte",						"OpDecorate %ret  FPRoundingMode RTE",	ROUNDINGMODE_RTE},
		{"unspecified_rnd_mode",	"",										RoundingModeFlags(ROUNDINGMODE_RTE | ROUNDINGMODE_RTZ)},
	};

	struct Case
	{
		const char*	name;
		const char*	interfaceOpFunc;
		const char*	preMain;
		const char*	inputType;
		const char*	outputType;
		deUint32	numPerCase;
		deUint32	numElements;
	};

	const Case	cases[]		=
	{
		{ // Scalar cases
			"scalar",

			"%interface_op_func = OpFunction %f16 None %f16_f32_function\n"
			"        %io_param1 = OpFunctionParameter %f32\n"
			"            %entry = OpLabel\n"
			"			   %ret = OpFConvert %f16 %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n",

			"             %f16 = OpTypeFloat 16\n"
			"          %op_f16 = OpTypePointer Output %f16\n"
			"           %a3f16 = OpTypeArray %f16 %c_i32_3\n"
			"        %op_a3f16 = OpTypePointer Output %a3f16\n"
			"%f16_f32_function = OpTypeFunction %f16 %f32\n"
			"           %a3f32 = OpTypeArray %f32 %c_i32_3\n"
			"        %ip_a3f32 = OpTypePointer Input %a3f32\n",

			"f32",
			"f16",
			4,
			1,
		},
		{ // Vector cases
			"vector",

			"%interface_op_func = OpFunction %v2f16 None %v2f16_v2f32_function\n"
			"        %io_param1 = OpFunctionParameter %v2f32\n"
			"            %entry = OpLabel\n"
			"			   %ret = OpFConvert %v2f16 %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n",

			"                 %f16 = OpTypeFloat 16\n"
			"               %v2f16 = OpTypeVector %f16 2\n"
			"            %op_v2f16 = OpTypePointer Output %v2f16\n"
			"             %a3v2f16 = OpTypeArray %v2f16 %c_i32_3\n"
			"          %op_a3v2f16 = OpTypePointer Output %a3v2f16\n"
			"%v2f16_v2f32_function = OpTypeFunction %v2f16 %v2f32\n"
			"             %a3v2f32 = OpTypeArray %v2f32 %c_i32_3\n"
			"          %ip_a3v2f32 = OpTypePointer Input %a3v2f32\n",

			"v2f32",
			"v2f16",
			2 * 4,
			2,
		}
	};

	VulkanFeatures	requiredFeatures;
	requiredFeatures.ext16BitStorage = EXT16BITSTORAGEFEATURES_INPUT_OUTPUT;

	for (deUint32 caseIdx = 0; caseIdx < DE_LENGTH_OF_ARRAY(cases); ++caseIdx)
		for (deUint32 rndModeIdx = 0; rndModeIdx < DE_LENGTH_OF_ARRAY(rndModes); ++rndModeIdx)
		{
			fragments["interface_op_func"]	= cases[caseIdx].interfaceOpFunc;
			fragments["pre_main"]			= cases[caseIdx].preMain;
			fragments["decoration"]			= rndModes[rndModeIdx].decor;

			fragments["input_type"]			= cases[caseIdx].inputType;
			fragments["output_type"]		= cases[caseIdx].outputType;

			GraphicsInterfaces	interfaces;
			const deUint32		numPerCase	= cases[caseIdx].numPerCase;
			vector<float>		subInputs	(numPerCase);
			vector<deFloat16>	subOutputs	(numPerCase);

			// The pipeline need this to call compare16BitFloat() when checking the result.
			interfaces.setRoundingMode(rndModes[rndModeIdx].flags);

			for (deUint32 caseNdx = 0; caseNdx < numDataPoints / numPerCase; ++caseNdx)
			{
				string			testName	= string(cases[caseIdx].name) + numberToString(caseNdx) + "_" + rndModes[rndModeIdx].name;

				for (deUint32 numNdx = 0; numNdx < numPerCase; ++numNdx)
				{
					subInputs[numNdx]	= float32Data[caseNdx * numPerCase + numNdx];
					// We derive the expected result from inputs directly in the graphics pipeline.
					subOutputs[numNdx]	= 0;
				}
				interfaces.setInputOutput(std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_FLOAT32), BufferSp(new Float32Buffer(subInputs))),
										  std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_FLOAT16), BufferSp(new Float16Buffer(subOutputs))));
				createTestsForAllStages(testName, defaultColors, defaultColors, fragments, interfaces, extensions, testGroup, requiredFeatures);
			}
		}
}

void addGraphics16BitStorageInputOutputFloat16To32Group (tcu::TestCaseGroup* testGroup)
{
	de::Random				rnd					(deStringHash(testGroup->getName()));
	RGBA					defaultColors[4];
	vector<string>			extensions;
	map<string, string>		fragments			= passthruFragments();
	const deUint32			numDataPoints		= 64;
	vector<deFloat16>		float16Data			(getFloat16s(rnd, numDataPoints));
	vector<float>			float32Data;

	float32Data.reserve(numDataPoints);
	for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
		float32Data.push_back(deFloat16To32(float16Data[numIdx]));

	extensions.push_back("VK_KHR_16bit_storage");

	fragments["capability"]				= "OpCapability StorageInputOutput16\n";
	fragments["extension"]				= "OpExtension \"SPV_KHR_16bit_storage\"\n";

	getDefaultColors(defaultColors);

	struct Case
	{
		const char*	name;
		const char*	interfaceOpFunc;
		const char*	preMain;
		const char*	inputType;
		const char*	outputType;
		deUint32	numPerCase;
		deUint32	numElements;
	};

	Case	cases[]		=
	{
		{ // Scalar cases
			"scalar",

			"%interface_op_func = OpFunction %f32 None %f32_f16_function\n"
			"        %io_param1 = OpFunctionParameter %f16\n"
			"            %entry = OpLabel\n"
			"			   %ret = OpFConvert %f32 %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n",

			"             %f16 = OpTypeFloat 16\n"
			"          %ip_f16 = OpTypePointer Input %f16\n"
			"           %a3f16 = OpTypeArray %f16 %c_i32_3\n"
			"        %ip_a3f16 = OpTypePointer Input %a3f16\n"
			"%f32_f16_function = OpTypeFunction %f32 %f16\n"
			"           %a3f32 = OpTypeArray %f32 %c_i32_3\n"
			"        %op_a3f32 = OpTypePointer Output %a3f32\n",

			"f16",
			"f32",
			4,
			1,
		},
		{ // Vector cases
			"vector",

			"%interface_op_func = OpFunction %v2f32 None %v2f32_v2f16_function\n"
			"        %io_param1 = OpFunctionParameter %v2f16\n"
			"            %entry = OpLabel\n"
			"			   %ret = OpFConvert %v2f32 %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n",

			"                 %f16 = OpTypeFloat 16\n"
			"		        %v2f16 = OpTypeVector %f16 2\n"
			"            %ip_v2f16 = OpTypePointer Input %v2f16\n"
			"             %a3v2f16 = OpTypeArray %v2f16 %c_i32_3\n"
			"          %ip_a3v2f16 = OpTypePointer Input %a3v2f16\n"
			"%v2f32_v2f16_function = OpTypeFunction %v2f32 %v2f16\n"
			"             %a3v2f32 = OpTypeArray %v2f32 %c_i32_3\n"
			"          %op_a3v2f32 = OpTypePointer Output %a3v2f32\n",

			"v2f16",
			"v2f32",
			2 * 4,
			2,
		}
	};

	VulkanFeatures	requiredFeatures;
	requiredFeatures.ext16BitStorage = EXT16BITSTORAGEFEATURES_INPUT_OUTPUT;

	for (deUint32 caseIdx = 0; caseIdx < DE_LENGTH_OF_ARRAY(cases); ++caseIdx)
	{
		fragments["interface_op_func"]	= cases[caseIdx].interfaceOpFunc;
		fragments["pre_main"]			= cases[caseIdx].preMain;

		fragments["input_type"]			= cases[caseIdx].inputType;
		fragments["output_type"]		= cases[caseIdx].outputType;

		GraphicsInterfaces	interfaces;
		const deUint32		numPerCase	= cases[caseIdx].numPerCase;
		vector<deFloat16>	subInputs	(numPerCase);
		vector<float>		subOutputs	(numPerCase);

		for (deUint32 caseNdx = 0; caseNdx < numDataPoints / numPerCase; ++caseNdx)
		{
			string			testName	= string(cases[caseIdx].name) + numberToString(caseNdx);

			for (deUint32 numNdx = 0; numNdx < numPerCase; ++numNdx)
			{
				subInputs[numNdx]	= float16Data[caseNdx * numPerCase + numNdx];
				subOutputs[numNdx]	= float32Data[caseNdx * numPerCase + numNdx];
			}
			interfaces.setInputOutput(std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_FLOAT16), BufferSp(new Float16Buffer(subInputs))),
									  std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_FLOAT32), BufferSp(new Float32Buffer(subOutputs))));
			createTestsForAllStages(testName, defaultColors, defaultColors, fragments, interfaces, extensions, testGroup, requiredFeatures);
		}
	}
}

void addGraphics16BitStorageInputOutputFloat16To16Group (tcu::TestCaseGroup* testGroup)
{
	de::Random			rnd					(deStringHash(testGroup->getName()));
	RGBA				defaultColors[4];
	vector<string>		extensions;
	map<string, string>	fragments			= passthruFragments();
	const deUint32		numDataPoints		= 64;
	vector<deFloat16>	float16Data			(getFloat16s(rnd, numDataPoints));
	VulkanFeatures		requiredFeatures;

	requiredFeatures.ext16BitStorage = EXT16BITSTORAGEFEATURES_INPUT_OUTPUT;
	extensions.push_back("VK_KHR_16bit_storage");

	fragments["capability"]					= "OpCapability StorageInputOutput16\n";
	fragments["extension"]					= "OpExtension \"SPV_KHR_16bit_storage\"\n";

	getDefaultColors(defaultColors);

	struct Case
	{
		const char*	name;
		const char*	interfaceOpFunc;
		const char*	preMain;
		const char*	inputType;
		const char*	outputType;
		deUint32	numPerCase;
		deUint32	numElements;
	};

	Case				cases[]				=
	{
		{ // Scalar cases
			"scalar",

			"%interface_op_func = OpFunction %f16 None %f16_f16_function\n"
			"        %io_param1 = OpFunctionParameter %f16\n"
			"            %entry = OpLabel\n"
			"              %ret = OpCopyObject %f16 %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n",

			"             %f16 = OpTypeFloat 16\n"
			"          %ip_f16 = OpTypePointer Input %f16\n"
			"           %a3f16 = OpTypeArray %f16 %c_i32_3\n"
			"        %ip_a3f16 = OpTypePointer Input %a3f16\n"
			"%f16_f16_function = OpTypeFunction %f16 %f16\n"
			"          %op_f16 = OpTypePointer Output %f16\n"
			"        %op_a3f16 = OpTypePointer Output %a3f16\n",

			"f16",
			"f16",
			4,
			1,
		},
		{ // Vector cases
			"vector",

			"%interface_op_func = OpFunction %v2f16 None %v2f16_v2f16_function\n"
			"        %io_param1 = OpFunctionParameter %v2f16\n"
			"            %entry = OpLabel\n"
			"              %ret = OpCopyObject %v2f16 %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n",

			"                 %f16 = OpTypeFloat 16\n"
			"               %v2f16 = OpTypeVector %f16 2\n"
			"            %ip_v2f16 = OpTypePointer Input %v2f16\n"
			"             %a3v2f16 = OpTypeArray %v2f16 %c_i32_3\n"
			"          %ip_a3v2f16 = OpTypePointer Input %a3v2f16\n"
			"%v2f16_v2f16_function = OpTypeFunction %v2f16 %v2f16\n"
			"            %op_v2f16 = OpTypePointer Output %v2f16\n"
			"          %op_a3v2f16 = OpTypePointer Output %a3v2f16\n",

			"v2f16",
			"v2f16",
			2 * 4,
			2,
		}
	};

	for (deUint32 caseIdx = 0; caseIdx < DE_LENGTH_OF_ARRAY(cases); ++caseIdx)
	{
		fragments["interface_op_func"]			= cases[caseIdx].interfaceOpFunc;
		fragments["pre_main"]					= cases[caseIdx].preMain;

		fragments["input_type"]					= cases[caseIdx].inputType;
		fragments["output_type"]				= cases[caseIdx].outputType;

		GraphicsInterfaces	interfaces;
		const deUint32		numPerCase			= cases[caseIdx].numPerCase;
		vector<deFloat16>	subInputsOutputs	(numPerCase);

		for (deUint32 caseNdx = 0; caseNdx < numDataPoints / numPerCase; ++caseNdx)
		{
			string testName = string(cases[caseIdx].name) + numberToString(caseNdx);

			for (deUint32 numNdx = 0; numNdx < numPerCase; ++numNdx)
				subInputsOutputs[numNdx] = float16Data[caseNdx * numPerCase + numNdx];

			interfaces.setInputOutput(std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_FLOAT16), BufferSp(new Float16Buffer(subInputsOutputs))),
									  std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_FLOAT16), BufferSp(new Float16Buffer(subInputsOutputs))));

			createTestsForAllStages(testName, defaultColors, defaultColors, fragments, interfaces, extensions, testGroup, requiredFeatures);
		}
	}
}

void addGraphics16BitStorageInputOutputInt32To16Group (tcu::TestCaseGroup* testGroup)
{
	de::Random							rnd					(deStringHash(testGroup->getName()));
	RGBA								defaultColors[4];
	vector<string>						extensions;
	map<string, string>					fragments			= passthruFragments();
	const deUint32						numDataPoints		= 64;
	// inputs and outputs are declared to be vectors of signed integers.
	// However, depending on the test, they may be interpreted as unsiged
	// integers. That won't be a problem as long as we passed the bits
	// in faithfully to the pipeline.
	vector<deInt32>						inputs				= getInt32s(rnd, numDataPoints);
	vector<deInt16>						outputs;

	outputs.reserve(inputs.size());
	for (deUint32 numNdx = 0; numNdx < inputs.size(); ++numNdx)
		outputs.push_back(static_cast<deInt16>(0xffff & inputs[numNdx]));

	extensions.push_back("VK_KHR_16bit_storage");

	fragments["capability"]				= "OpCapability StorageInputOutput16\n";
	fragments["extension"]				= "OpExtension \"SPV_KHR_16bit_storage\"\n";

	getDefaultColors(defaultColors);

	const StringTemplate	scalarInterfaceOpFunc(
			"%interface_op_func = OpFunction %${type16} None %${type16}_${type32}_function\n"
			"        %io_param1 = OpFunctionParameter %${type32}\n"
			"            %entry = OpLabel\n"
			"			   %ret = ${convert} %${type16} %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n");

	const StringTemplate	scalarPreMain(
			"             %${type16} = OpTypeInt 16 ${signed}\n"
			"          %op_${type16} = OpTypePointer Output %${type16}\n"
			"           %a3${type16} = OpTypeArray %${type16} %c_i32_3\n"
			"        %op_a3${type16} = OpTypePointer Output %a3${type16}\n"
			"%${type16}_${type32}_function = OpTypeFunction %${type16} %${type32}\n"
			"           %a3${type32} = OpTypeArray %${type32} %c_i32_3\n"
			"        %ip_a3${type32} = OpTypePointer Input %a3${type32}\n");

	const StringTemplate	vecInterfaceOpFunc(
			"%interface_op_func = OpFunction %${type16} None %${type16}_${type32}_function\n"
			"        %io_param1 = OpFunctionParameter %${type32}\n"
			"            %entry = OpLabel\n"
			"			   %ret = ${convert} %${type16} %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n");

	const StringTemplate	vecPreMain(
			"	                %i16 = OpTypeInt 16 1\n"
			"	                %u16 = OpTypeInt 16 0\n"
			"                 %v4i16 = OpTypeVector %i16 4\n"
			"                 %v4u16 = OpTypeVector %u16 4\n"
			"          %op_${type16} = OpTypePointer Output %${type16}\n"
			"           %a3${type16} = OpTypeArray %${type16} %c_i32_3\n"
			"        %op_a3${type16} = OpTypePointer Output %a3${type16}\n"
			"%${type16}_${type32}_function = OpTypeFunction %${type16} %${type32}\n"
			"           %a3${type32} = OpTypeArray %${type32} %c_i32_3\n"
			"        %ip_a3${type32} = OpTypePointer Input %a3${type32}\n");

	struct Case
	{
		const char*				name;
		const StringTemplate&	interfaceOpFunc;
		const StringTemplate&	preMain;
		const char*				type32;
		const char*				type16;
		const char*				sign;
		const char*				opcode;
		deUint32				numPerCase;
		deUint32				numElements;
	};

	Case	cases[]		=
	{
		{"scalar_sint",	scalarInterfaceOpFunc,	scalarPreMain,	"i32",		"i16",		"1",	"OpSConvert",	4,		1},
		{"scalar_uint",	scalarInterfaceOpFunc,	scalarPreMain,	"u32",		"u16",		"0",	"OpUConvert",	4,		1},
		{"vector_sint",	vecInterfaceOpFunc,		vecPreMain,		"v4i32",	"v4i16",	"1",	"OpSConvert",	4 * 4,	4},
		{"vector_uint",	vecInterfaceOpFunc,		vecPreMain,		"v4u32",	"v4u16",	"0",	"OpUConvert",	4 * 4,	4},
	};

	VulkanFeatures	requiredFeatures;
	requiredFeatures.ext16BitStorage = EXT16BITSTORAGEFEATURES_INPUT_OUTPUT;

	for (deUint32 caseIdx = 0; caseIdx < DE_LENGTH_OF_ARRAY(cases); ++caseIdx)
	{
		map<string, string>				specs;

		specs["type32"]					= cases[caseIdx].type32;
		specs["type16"]					= cases[caseIdx].type16;
		specs["signed"]					= cases[caseIdx].sign;
		specs["convert"]				= cases[caseIdx].opcode;

		fragments["pre_main"]			= cases[caseIdx].preMain.specialize(specs);
		fragments["interface_op_func"]	= cases[caseIdx].interfaceOpFunc.specialize(specs);
		fragments["input_type"]			= cases[caseIdx].type32;
		fragments["output_type"]		= cases[caseIdx].type16;

		GraphicsInterfaces				interfaces;
		const deUint32					numPerCase	= cases[caseIdx].numPerCase;
		vector<deInt32>					subInputs	(numPerCase);
		vector<deInt16>					subOutputs	(numPerCase);

		for (deUint32 caseNdx = 0; caseNdx < numDataPoints / numPerCase; ++caseNdx)
		{
			string			testName	= string(cases[caseIdx].name) + numberToString(caseNdx);

			for (deUint32 numNdx = 0; numNdx < numPerCase; ++numNdx)
			{
				subInputs[numNdx]	= inputs[caseNdx * numPerCase + numNdx];
				subOutputs[numNdx]	= outputs[caseNdx * numPerCase + numNdx];
			}
			if (strcmp(cases[caseIdx].sign, "1") == 0)
			{
				interfaces.setInputOutput(std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_INT32), BufferSp(new Int32Buffer(subInputs))),
										  std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_INT16), BufferSp(new Int16Buffer(subOutputs))));
			}
			else
			{
				interfaces.setInputOutput(std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_UINT32), BufferSp(new Int32Buffer(subInputs))),
										  std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_UINT16), BufferSp(new Int16Buffer(subOutputs))));
			}
			createTestsForAllStages(testName, defaultColors, defaultColors, fragments, interfaces, extensions, testGroup, requiredFeatures);
		}
	}
}

void addGraphics16BitStorageInputOutputInt16To32Group (tcu::TestCaseGroup* testGroup)
{
	de::Random							rnd					(deStringHash(testGroup->getName()));
	RGBA								defaultColors[4];
	vector<string>						extensions;
	map<string, string>					fragments			= passthruFragments();
	const deUint32						numDataPoints		= 64;
	// inputs and outputs are declared to be vectors of signed integers.
	// However, depending on the test, they may be interpreted as unsiged
	// integers. That won't be a problem as long as we passed the bits
	// in faithfully to the pipeline.
	vector<deInt16>						inputs				= getInt16s(rnd, numDataPoints);
	vector<deInt32>						sOutputs;
	vector<deInt32>						uOutputs;
	const deUint16						signBitMask			= 0x8000;
	const deUint32						signExtendMask		= 0xffff0000;

	sOutputs.reserve(inputs.size());
	uOutputs.reserve(inputs.size());

	for (deUint32 numNdx = 0; numNdx < inputs.size(); ++numNdx)
	{
		uOutputs.push_back(static_cast<deUint16>(inputs[numNdx]));
		if (inputs[numNdx] & signBitMask)
			sOutputs.push_back(static_cast<deInt32>(inputs[numNdx] | signExtendMask));
		else
			sOutputs.push_back(static_cast<deInt32>(inputs[numNdx]));
	}

	extensions.push_back("VK_KHR_16bit_storage");

	fragments["capability"]				= "OpCapability StorageInputOutput16\n";
	fragments["extension"]				= "OpExtension \"SPV_KHR_16bit_storage\"\n";

	getDefaultColors(defaultColors);

	const StringTemplate scalarIfOpFunc	(
			"%interface_op_func = OpFunction %${type32} None %${type32}_${type16}_function\n"
			"        %io_param1 = OpFunctionParameter %${type16}\n"
			"            %entry = OpLabel\n"
			"			   %ret = ${convert} %${type32} %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n");

	const StringTemplate scalarPreMain	(
			"             %${type16} = OpTypeInt 16 ${signed}\n"
			"          %ip_${type16} = OpTypePointer Input %${type16}\n"
			"           %a3${type16} = OpTypeArray %${type16} %c_i32_3\n"
			"        %ip_a3${type16} = OpTypePointer Input %a3${type16}\n"
			"%${type32}_${type16}_function = OpTypeFunction %${type32} %${type16}\n"
			"           %a3${type32} = OpTypeArray %${type32} %c_i32_3\n"
			"        %op_a3${type32} = OpTypePointer Output %a3${type32}\n");

	const StringTemplate vecIfOpFunc	(
			"%interface_op_func = OpFunction %${type32} None %${type32}_${type16}_function\n"
			"        %io_param1 = OpFunctionParameter %${type16}\n"
			"            %entry = OpLabel\n"
			"			   %ret = ${convert} %${type32} %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n");

	const StringTemplate vecPreMain	(
			"	                %i16 = OpTypeInt 16 1\n"
			"	                %u16 = OpTypeInt 16 0\n"
			"                 %v4i16 = OpTypeVector %i16 4\n"
			"                 %v4u16 = OpTypeVector %u16 4\n"
			"          %ip_${type16} = OpTypePointer Input %${type16}\n"
			"           %a3${type16} = OpTypeArray %${type16} %c_i32_3\n"
			"        %ip_a3${type16} = OpTypePointer Input %a3${type16}\n"
			"%${type32}_${type16}_function = OpTypeFunction %${type32} %${type16}\n"
			"           %a3${type32} = OpTypeArray %${type32} %c_i32_3\n"
			"        %op_a3${type32} = OpTypePointer Output %a3${type32}\n");

	struct Case
	{
		const char*				name;
		const StringTemplate&	interfaceOpFunc;
		const StringTemplate&	preMain;
		const char*				type32;
		const char*				type16;
		const char*				sign;
		const char*				opcode;
		deUint32				numPerCase;
		deUint32				numElements;
	};

	Case	cases[]		=
	{
		{"scalar_sint",	scalarIfOpFunc,	scalarPreMain,	"i32",		"i16",		"1",	"OpSConvert",	4,		1},
		{"scalar_uint",	scalarIfOpFunc,	scalarPreMain,	"u32",		"u16",		"0",	"OpUConvert",	4,		1},
		{"vector_sint",	vecIfOpFunc,	vecPreMain,		"v4i32",	"v4i16",	"1",	"OpSConvert",	4 * 4,	4},
		{"vector_uint",	vecIfOpFunc,	vecPreMain,		"v4u32",	"v4u16",	"0",	"OpUConvert",	4 * 4,	4},
	};

	VulkanFeatures	requiredFeatures;
	requiredFeatures.ext16BitStorage = EXT16BITSTORAGEFEATURES_INPUT_OUTPUT;

	for (deUint32 caseIdx = 0; caseIdx < DE_LENGTH_OF_ARRAY(cases); ++caseIdx)
	{
		map<string, string>				specs;

		specs["type32"]					= cases[caseIdx].type32;
		specs["type16"]					= cases[caseIdx].type16;
		specs["signed"]					= cases[caseIdx].sign;
		specs["convert"]				= cases[caseIdx].opcode;

		fragments["pre_main"]			= cases[caseIdx].preMain.specialize(specs);
		fragments["interface_op_func"]	= cases[caseIdx].interfaceOpFunc.specialize(specs);
		fragments["input_type"]			= cases[caseIdx].type16;
		fragments["output_type"]		= cases[caseIdx].type32;

		GraphicsInterfaces				interfaces;
		const deUint32					numPerCase	= cases[caseIdx].numPerCase;
		vector<deInt16>					subInputs	(numPerCase);
		vector<deInt32>					subOutputs	(numPerCase);

		for (deUint32 caseNdx = 0; caseNdx < numDataPoints / numPerCase; ++caseNdx)
		{
			string			testName	= string(cases[caseIdx].name) + numberToString(caseNdx);

			for (deUint32 numNdx = 0; numNdx < numPerCase; ++numNdx)
			{
				subInputs[numNdx]	= inputs[caseNdx * numPerCase + numNdx];
				if (cases[caseIdx].sign[0] == '1')
					subOutputs[numNdx]	= sOutputs[caseNdx * numPerCase + numNdx];
				else
					subOutputs[numNdx]	= uOutputs[caseNdx * numPerCase + numNdx];
			}
			if (strcmp(cases[caseIdx].sign, "1") == 0)
			{
				interfaces.setInputOutput(std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_INT16), BufferSp(new Int16Buffer(subInputs))),
										  std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_INT32), BufferSp(new Int32Buffer(subOutputs))));
			}
			else
			{
				interfaces.setInputOutput(std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_UINT16), BufferSp(new Int16Buffer(subInputs))),
										  std::make_pair(IFDataType(cases[caseIdx].numElements, NUMBERTYPE_UINT32), BufferSp(new Int32Buffer(subOutputs))));
			}
			createTestsForAllStages(testName, defaultColors, defaultColors, fragments, interfaces, extensions, testGroup, requiredFeatures);
		}
	}
}

void addGraphics16BitStorageInputOutputInt16To16Group (tcu::TestCaseGroup* testGroup)
{
	de::Random				rnd					(deStringHash(testGroup->getName()));
	RGBA					defaultColors[4];
	vector<string>			extensions;
	map<string, string>		fragments			= passthruFragments();
	const deUint32			numDataPoints		= 64;
	// inputs and outputs are declared to be vectors of signed integers.
	// However, depending on the test, they may be interpreted as unsiged
	// integers. That won't be a problem as long as we passed the bits
	// in faithfully to the pipeline.
	vector<deInt16>			inputs				= getInt16s(rnd, numDataPoints);
	VulkanFeatures			requiredFeatures;

	requiredFeatures.ext16BitStorage = EXT16BITSTORAGEFEATURES_INPUT_OUTPUT;
	extensions.push_back("VK_KHR_16bit_storage");

	fragments["capability"]						= "OpCapability StorageInputOutput16\n";
	fragments["extension"]						= "OpExtension \"SPV_KHR_16bit_storage\"\n";

	getDefaultColors(defaultColors);

	const StringTemplate	scalarIfOpFunc		(
			"%interface_op_func = OpFunction %${type16} None %${type16}_${type16}_function\n"
			"        %io_param1 = OpFunctionParameter %${type16}\n"
			"            %entry = OpLabel\n"
			"              %ret = OpCopyObject %${type16} %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n");

	const StringTemplate	scalarPreMain		(
			"             %${type16} = OpTypeInt 16 ${signed}\n"
			"          %ip_${type16} = OpTypePointer Input %${type16}\n"
			"           %a3${type16} = OpTypeArray %${type16} %c_i32_3\n"
			"        %ip_a3${type16} = OpTypePointer Input %a3${type16}\n"
			"%${type16}_${type16}_function = OpTypeFunction %${type16} %${type16}\n"
			"          %op_${type16} = OpTypePointer Output %${type16}\n"
			"        %op_a3${type16} = OpTypePointer Output %a3${type16}\n");

	const StringTemplate	vecIfOpFunc			(
			"%interface_op_func = OpFunction %${type16} None %${type16}_${type16}_function\n"
			"        %io_param1 = OpFunctionParameter %${type16}\n"
			"            %entry = OpLabel\n"
			"              %ret = OpCopyObject %${type16} %io_param1\n"
			"                     OpReturnValue %ret\n"
			"                     OpFunctionEnd\n");

	const StringTemplate	vecPreMain			(
			"                   %i16 = OpTypeInt 16 1\n"
			"                   %u16 = OpTypeInt 16 0\n"
			"                 %v4i16 = OpTypeVector %i16 4\n"
			"                 %v4u16 = OpTypeVector %u16 4\n"
			"          %ip_${type16} = OpTypePointer Input %${type16}\n"
			"           %a3${type16} = OpTypeArray %${type16} %c_i32_3\n"
			"        %ip_a3${type16} = OpTypePointer Input %a3${type16}\n"
			"%${type16}_${type16}_function = OpTypeFunction %${type16} %${type16}\n"
			"          %op_${type16} = OpTypePointer Output %${type16}\n"
			"        %op_a3${type16} = OpTypePointer Output %a3${type16}\n");

	struct Case
	{
		const char*				name;
		const StringTemplate&	interfaceOpFunc;
		const StringTemplate&	preMain;
		const char*				type16;
		const char*				sign;
		deUint32				numPerCase;
		deUint32				numElements;
	};

	Case					cases[]				=
	{
		{"scalar_sint",	scalarIfOpFunc,	scalarPreMain,	"i16",		"1",	4,		1},
		{"scalar_uint",	scalarIfOpFunc,	scalarPreMain,	"u16",		"0",	4,		1},
		{"vector_sint",	vecIfOpFunc,	vecPreMain,		"v4i16",	"1",	4 * 4,	4},
		{"vector_uint",	vecIfOpFunc,	vecPreMain,		"v4u16",	"0",	4 * 4,	4},
	};

	for (deUint32 caseIdx = 0; caseIdx < DE_LENGTH_OF_ARRAY(cases); ++caseIdx)
	{
		map<string, string>				specs;

		specs["type16"]					= cases[caseIdx].type16;
		specs["signed"]					= cases[caseIdx].sign;

		fragments["pre_main"]			= cases[caseIdx].preMain.specialize(specs);
		fragments["interface_op_func"]	= cases[caseIdx].interfaceOpFunc.specialize(specs);
		fragments["input_type"]			= cases[caseIdx].type16;
		fragments["output_type"]		= cases[caseIdx].type16;

		GraphicsInterfaces				interfaces;
		const deUint32					numPerCase			= cases[caseIdx].numPerCase;
		vector<deInt16>					subInputsOutputs	(numPerCase);
		const NumberType				numberType			= strcmp(cases[caseIdx].sign, "1") == 0 ? NUMBERTYPE_INT16 : NUMBERTYPE_UINT16;

		for (deUint32 caseNdx = 0; caseNdx < numDataPoints / numPerCase; ++caseNdx)
		{
			string testName = string(cases[caseIdx].name) + numberToString(caseNdx);

			for (deUint32 numNdx = 0; numNdx < numPerCase; ++numNdx)
				subInputsOutputs[numNdx] = inputs[caseNdx * numPerCase + numNdx];

			interfaces.setInputOutput(std::make_pair(IFDataType(cases[caseIdx].numElements, numberType), BufferSp(new Int16Buffer(subInputsOutputs))),
									  std::make_pair(IFDataType(cases[caseIdx].numElements, numberType), BufferSp(new Int16Buffer(subInputsOutputs))));

			createTestsForAllStages(testName, defaultColors, defaultColors, fragments, interfaces, extensions, testGroup, requiredFeatures);
		}
	}
}

void addGraphics16BitStoragePushConstantFloat16To32Group (tcu::TestCaseGroup* testGroup)
{
	de::Random							rnd					(deStringHash(testGroup->getName()));
	map<string, string>					fragments;
	RGBA								defaultColors[4];
	vector<string>						extensions;
	GraphicsResources					resources;
	PushConstants						pcs;
	const deUint32						numDataPoints		= 64;
	vector<deFloat16>					float16Data			(getFloat16s(rnd, numDataPoints));
	vector<float>						float32Data;
	VulkanFeatures						requiredFeatures;

	struct ConstantIndex
	{
		bool		useConstantIndex;
		deUint32	constantIndex;
	};

	ConstantIndex	constantIndices[] =
	{
		{ false,	0 },
		{ true,		4 },
		{ true,		5 },
		{ true,		6 }
	};

	float32Data.reserve(numDataPoints);
	for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
		float32Data.push_back(deFloat16To32(float16Data[numIdx]));

	extensions.push_back("VK_KHR_16bit_storage");
	requiredFeatures.ext16BitStorage = EXT16BITSTORAGEFEATURES_PUSH_CONSTANT;

	fragments["capability"]				= "OpCapability StoragePushConstant16\n";
	fragments["extension"]				= "OpExtension \"SPV_KHR_16bit_storage\"";

	pcs.setPushConstant(BufferSp(new Float16Buffer(float16Data)));
	resources.verifyIO = check32BitFloats;

	getDefaultColors(defaultColors);

	const StringTemplate	testFun		(
		"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
		"    %param = OpFunctionParameter %v4f32\n"

		"%entry = OpLabel\n"
		"    %i = OpVariable %fp_i32 Function\n"
		"         OpStore %i %c_i32_0\n"
		"         OpBranch %loop\n"

		" %loop = OpLabel\n"
		"   %15 = OpLoad %i32 %i\n"
		"   %lt = OpSLessThan %bool %15 ${count}\n"
		"         OpLoopMerge %merge %inc None\n"
		"         OpBranchConditional %lt %write %merge\n"

		"%write = OpLabel\n"
		"   %30 = OpLoad %i32 %i\n"
		"  %src = OpAccessChain ${pp_type16} %pc16 %c_i32_0 %${arrayindex} ${index0:opt}\n"
		"%val16 = OpLoad ${f_type16} %src\n"
		"%val32 = OpFConvert ${f_type32} %val16\n"
		"  %dst = OpAccessChain ${up_type32} %ssbo32 %c_i32_0 %30 ${index0:opt}\n"
		"         OpStore %dst %val32\n"

		"${store:opt}\n"

		"         OpBranch %inc\n"

		"  %inc = OpLabel\n"
		"   %37 = OpLoad %i32 %i\n"
		"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
		"         OpStore %i %39\n"
		"         OpBranch %loop\n"

		"%merge = OpLabel\n"
		"         OpReturnValue %param\n"

		"OpFunctionEnd\n");

	{  // Scalar cases
		const StringTemplate	preMain		(
			"      %f16 = OpTypeFloat 16\n"
			" %c_i32_64 = OpConstant %i32 64\n"
			" %c_i32_ci = OpConstant %i32 ${constarrayidx}\n"
			"  %a64f16 = OpTypeArray %f16 %c_i32_64\n"
			"  %a64f32 = OpTypeArray %f32 %c_i32_64\n"
			"   %pp_f16 = OpTypePointer PushConstant %f16\n"
			"   %up_f32 = OpTypePointer Uniform %f32\n"
			"   %SSBO32 = OpTypeStruct %a64f32\n"
			"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"   %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"     %PC16 = OpTypeStruct %a64f16\n"
			"  %pp_PC16 = OpTypePointer PushConstant %PC16\n"
			"     %pc16 = OpVariable %pp_PC16 PushConstant\n");

		fragments["decoration"]				=
			"OpDecorate %a64f16 ArrayStride 2\n"
			"OpDecorate %a64f32 ArrayStride 4\n"
			"OpDecorate %SSBO32 BufferBlock\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpDecorate %PC16 Block\n"
			"OpMemberDecorate %PC16 0 Offset 0\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 0\n";

		map<string, string>		specs;

		specs["count"]			= "%c_i32_64";
		specs["pp_type16"]		= "%pp_f16";
		specs["f_type16"]		= "%f16";
		specs["f_type32"]		= "%f32";
		specs["up_type32"]		= "%up_f32";

		for (deUint32 constIndexIdx = 0; constIndexIdx < DE_LENGTH_OF_ARRAY(constantIndices); ++constIndexIdx)
		{
			bool			useConstIdx		= constantIndices[constIndexIdx].useConstantIndex;
			deUint32		constIdx		= constantIndices[constIndexIdx].constantIndex;
			string			testName		= "scalar";
			vector<float>	float32ConstIdxData;

			if (useConstIdx)
			{
				float32ConstIdxData.reserve(numDataPoints);

				for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
					float32ConstIdxData.push_back(float32Data[constIdx]);
			}

			specs["constarrayidx"]	= de::toString(constIdx);
			if (useConstIdx)
				specs["arrayindex"] = "c_i32_ci";
			else
				specs["arrayindex"] = "30";

			resources.outputs.clear();
			resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float32Buffer(useConstIdx ? float32ConstIdxData : float32Data))));

			fragments["pre_main"]		= preMain.specialize(specs);
			fragments["testfun"]		= testFun.specialize(specs);

			if (useConstIdx)
				testName += string("_const_idx_") + de::toString(constIdx);

			createTestsForAllStages(testName.c_str(), defaultColors, defaultColors, fragments, pcs, resources, extensions, testGroup, requiredFeatures);
		}
	}

	{  // Vector cases
		const StringTemplate	preMain		(
			"      %f16 = OpTypeFloat 16\n"
			"    %v4f16 = OpTypeVector %f16 4\n"
			" %c_i32_16 = OpConstant %i32 16\n"
			" %c_i32_ci = OpConstant %i32 ${constarrayidx}\n"
			" %a16v4f16 = OpTypeArray %v4f16 %c_i32_16\n"
			" %a16v4f32 = OpTypeArray %v4f32 %c_i32_16\n"
			" %pp_v4f16 = OpTypePointer PushConstant %v4f16\n"
			" %up_v4f32 = OpTypePointer Uniform %v4f32\n"
			"   %SSBO32 = OpTypeStruct %a16v4f32\n"
			"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"   %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"     %PC16 = OpTypeStruct %a16v4f16\n"
			"  %pp_PC16 = OpTypePointer PushConstant %PC16\n"
			"     %pc16 = OpVariable %pp_PC16 PushConstant\n");

		fragments["decoration"]				=
			"OpDecorate %a16v4f16 ArrayStride 8\n"
			"OpDecorate %a16v4f32 ArrayStride 16\n"
			"OpDecorate %SSBO32 BufferBlock\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpDecorate %PC16 Block\n"
			"OpMemberDecorate %PC16 0 Offset 0\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 0\n";

		map<string, string>		specs;

		specs["count"]			= "%c_i32_16";
		specs["pp_type16"]		= "%pp_v4f16";
		specs["f_type16"]		= "%v4f16";
		specs["f_type32"]		= "%v4f32";
		specs["up_type32"]		= "%up_v4f32";

		for (deUint32 constIndexIdx = 0; constIndexIdx < DE_LENGTH_OF_ARRAY(constantIndices); ++constIndexIdx)
		{
			bool			useConstIdx			= constantIndices[constIndexIdx].useConstantIndex;
			deUint32		constIdx			= constantIndices[constIndexIdx].constantIndex;
			string			testName			= "vector";
			vector<float>	float32ConstIdxData;

			if (useConstIdx)
			{
				float32ConstIdxData.reserve(numDataPoints);

				for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
					float32ConstIdxData.push_back(float32Data[constIdx * 4 + numIdx % 4]);
			}

			specs["constarrayidx"]	= de::toString(constIdx);
			if (useConstIdx)
				specs["arrayindex"] = "c_i32_ci";
			else
				specs["arrayindex"] = "30";

			resources.outputs.clear();
			resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float32Buffer(useConstIdx ? float32ConstIdxData : float32Data))));

			fragments["pre_main"]	= preMain.specialize(specs);
			fragments["testfun"]	= testFun.specialize(specs);

			if (useConstIdx)
				testName += string("_const_idx_") + de::toString(constIdx);

			createTestsForAllStages(testName.c_str(), defaultColors, defaultColors, fragments, pcs, resources, extensions, testGroup, requiredFeatures);
		}
	}

	{  // Matrix cases
		const StringTemplate	preMain		(
			"   %c_i32_8 = OpConstant %i32 8\n"
			"  %c_i32_ci = OpConstant %i32 ${constarrayidx}\n"
			"      %f16  = OpTypeFloat 16\n"
			"    %v4f16  = OpTypeVector %f16 4\n"
			"  %m2v4f16  = OpTypeMatrix %v4f16 2\n"
			"  %m2v4f32  = OpTypeMatrix %v4f32 2\n"
			" %a8m2v4f16 = OpTypeArray %m2v4f16 %c_i32_8\n"
			" %a8m2v4f32 = OpTypeArray %m2v4f32 %c_i32_8\n"
			" %pp_v4f16  = OpTypePointer PushConstant %v4f16\n"
			" %up_v4f32  = OpTypePointer Uniform %v4f32\n"
			"   %SSBO32  = OpTypeStruct %a8m2v4f32\n"
			"%up_SSBO32  = OpTypePointer Uniform %SSBO32\n"
			"   %ssbo32  = OpVariable %up_SSBO32 Uniform\n"
			"     %PC16  = OpTypeStruct %a8m2v4f16\n"
			"  %pp_PC16  = OpTypePointer PushConstant %PC16\n"
			"     %pc16  = OpVariable %pp_PC16 PushConstant\n");

		fragments["decoration"]				=
			"OpDecorate %a8m2v4f16 ArrayStride 16\n"
			"OpDecorate %a8m2v4f32 ArrayStride 32\n"
			"OpDecorate %SSBO32 BufferBlock\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpMemberDecorate %SSBO32 0 ColMajor\n"
			"OpMemberDecorate %SSBO32 0 MatrixStride 16\n"
			"OpDecorate %PC16 Block\n"
			"OpMemberDecorate %PC16 0 Offset 0\n"
			"OpMemberDecorate %PC16 0 ColMajor\n"
			"OpMemberDecorate %PC16 0 MatrixStride 8\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 0\n";

		map<string, string>		specs;

		specs["count"]			= "%c_i32_8";
		specs["pp_type16"]		= "%pp_v4f16";
		specs["up_type32"]		= "%up_v4f32";
		specs["f_type16"]		= "%v4f16";
		specs["f_type32"]		= "%v4f32";
		specs["index0"]			= "%c_i32_0";

		for (deUint32 constIndexIdx = 0; constIndexIdx < DE_LENGTH_OF_ARRAY(constantIndices); ++constIndexIdx)
		{
			bool					useConstIdx			= constantIndices[constIndexIdx].useConstantIndex;
			deUint32				constIdx			= constantIndices[constIndexIdx].constantIndex;
			string					testName			= "matrix";
			vector<float>			float32ConstIdxData;
			const StringTemplate	store				(
				"  %src_1 = OpAccessChain %pp_v4f16 %pc16 %c_i32_0 %${arrayindex} %c_i32_1\n"
				"%val16_1 = OpLoad %v4f16 %src_1\n"
				"%val32_1 = OpFConvert %v4f32 %val16_1\n"
				"  %dst_1 = OpAccessChain %up_v4f32 %ssbo32 %c_i32_0 %30 %c_i32_1\n"
				"           OpStore %dst_1 %val32_1\n");

			if (useConstIdx)
			{
				float32ConstIdxData.reserve(numDataPoints);

				for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
					float32ConstIdxData.push_back(float32Data[constIdx * 8 + numIdx % 8]);
			}

			specs["constarrayidx"]	= de::toString(constIdx);
			if (useConstIdx)
				specs["arrayindex"] = "c_i32_ci";
			else
				specs["arrayindex"] = "30";

			specs["store"] = store.specialize(specs);

			resources.outputs.clear();
			resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float32Buffer(useConstIdx ? float32ConstIdxData : float32Data))));

			fragments["pre_main"]		= preMain.specialize(specs);
			fragments["testfun"]		= testFun.specialize(specs);

			if (useConstIdx)
				testName += string("_const_idx_") + de::toString(constIdx);

			createTestsForAllStages(testName.c_str(), defaultColors, defaultColors, fragments, pcs, resources, extensions, testGroup, requiredFeatures);
		}
	}
}

void addGraphics16BitStoragePushConstantInt16To32Group (tcu::TestCaseGroup* testGroup)
{
	de::Random							rnd					(deStringHash(testGroup->getName()));
	map<string, string>					fragments;
	RGBA								defaultColors[4];
	const deUint32						numDataPoints		= 64;
	vector<deInt16>						inputs				= getInt16s(rnd, numDataPoints);
	vector<deInt32>						sOutputs;
	vector<deInt32>						uOutputs;
	PushConstants						pcs;
	GraphicsResources					resources;
	vector<string>						extensions;
	const deUint16						signBitMask			= 0x8000;
	const deUint32						signExtendMask		= 0xffff0000;
	VulkanFeatures						requiredFeatures;

	struct ConstantIndex
	{
		bool		useConstantIndex;
		deUint32	constantIndex;
	};

	ConstantIndex	constantIndices[] =
	{
		{ false,	0 },
		{ true,		4 },
		{ true,		5 },
		{ true,		6 }
	};

	sOutputs.reserve(inputs.size());
	uOutputs.reserve(inputs.size());

	for (deUint32 numNdx = 0; numNdx < inputs.size(); ++numNdx)
	{
		uOutputs.push_back(static_cast<deUint16>(inputs[numNdx]));
		if (inputs[numNdx] & signBitMask)
			sOutputs.push_back(static_cast<deInt32>(inputs[numNdx] | signExtendMask));
		else
			sOutputs.push_back(static_cast<deInt32>(inputs[numNdx]));
	}

	extensions.push_back("VK_KHR_16bit_storage");
	requiredFeatures.ext16BitStorage = EXT16BITSTORAGEFEATURES_PUSH_CONSTANT;

	fragments["capability"]				= "OpCapability StoragePushConstant16\n";
	fragments["extension"]				= "OpExtension \"SPV_KHR_16bit_storage\"";

	pcs.setPushConstant(BufferSp(new Int16Buffer(inputs)));

	getDefaultColors(defaultColors);

	const StringTemplate	testFun		(
		"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
		"    %param = OpFunctionParameter %v4f32\n"

		"%entry = OpLabel\n"
		"    %i = OpVariable %fp_i32 Function\n"
		"         OpStore %i %c_i32_0\n"
		"         OpBranch %loop\n"

		" %loop = OpLabel\n"
		"   %15 = OpLoad %i32 %i\n"
		"   %lt = OpSLessThan %bool %15 %c_i32_${count}\n"
		"         OpLoopMerge %merge %inc None\n"
		"         OpBranchConditional %lt %write %merge\n"

		"%write = OpLabel\n"
		"   %30 = OpLoad %i32 %i\n"
		"  %src = OpAccessChain %pp_${type16} %pc16 %c_i32_0 %${arrayindex}\n"
		"%val16 = OpLoad %${type16} %src\n"
		"%val32 = ${convert} %${type32} %val16\n"
		"  %dst = OpAccessChain %up_${type32} %ssbo32 %c_i32_0 %30\n"
		"         OpStore %dst %val32\n"
		"         OpBranch %inc\n"

		"  %inc = OpLabel\n"
		"   %37 = OpLoad %i32 %i\n"
		"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
		"         OpStore %i %39\n"
		"         OpBranch %loop\n"

		"%merge = OpLabel\n"
		"         OpReturnValue %param\n"

		"OpFunctionEnd\n");

	{  // Scalar cases
		const StringTemplate	preMain		(
			"         %${type16} = OpTypeInt 16 ${signed}\n"
			"    %c_i32_${count} = OpConstant %i32 ${count}\n"					// Should be the same as numDataPoints
			"          %c_i32_ci = OpConstant %i32 ${constarrayidx}\n"
			"%a${count}${type16} = OpTypeArray %${type16} %c_i32_${count}\n"
			"%a${count}${type32} = OpTypeArray %${type32} %c_i32_${count}\n"
			"      %pp_${type16} = OpTypePointer PushConstant %${type16}\n"
			"      %up_${type32} = OpTypePointer Uniform      %${type32}\n"
			"            %SSBO32 = OpTypeStruct %a${count}${type32}\n"
			"         %up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"            %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"              %PC16 = OpTypeStruct %a${count}${type16}\n"
			"           %pp_PC16 = OpTypePointer PushConstant %PC16\n"
			"              %pc16 = OpVariable %pp_PC16 PushConstant\n");

		const StringTemplate	decoration	(
			"OpDecorate %a${count}${type16} ArrayStride 2\n"
			"OpDecorate %a${count}${type32} ArrayStride 4\n"
			"OpDecorate %SSBO32 BufferBlock\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpDecorate %PC16 Block\n"
			"OpMemberDecorate %PC16 0 Offset 0\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 0\n");

		{  // signed int
			map<string, string>		specs;

			specs["type16"]			= "i16";
			specs["type32"]			= "i32";
			specs["signed"]			= "1";
			specs["count"]			= "64";
			specs["convert"]		= "OpSConvert";

			for (deUint32 constIndexIdx = 0; constIndexIdx < DE_LENGTH_OF_ARRAY(constantIndices); ++constIndexIdx)
			{
				bool			useConstIdx		= constantIndices[constIndexIdx].useConstantIndex;
				deUint32		constIdx		= constantIndices[constIndexIdx].constantIndex;
				string			testName		= "sint_scalar";
				vector<deInt32>	constIdxData;

				if (useConstIdx)
				{
					constIdxData.reserve(numDataPoints);

					for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
						constIdxData.push_back(sOutputs[constIdx]);
				}

				specs["constarrayidx"]	= de::toString(constIdx);
				if (useConstIdx)
					specs["arrayindex"] = "c_i32_ci";
				else
					specs["arrayindex"] = "30";

				if (useConstIdx)
					testName += string("_const_idx_") + de::toString(constIdx);

				resources.outputs.clear();
				resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Int32Buffer(useConstIdx ? constIdxData : sOutputs))));

				fragments["testfun"]	= testFun.specialize(specs);
				fragments["pre_main"]	= preMain.specialize(specs);
				fragments["decoration"]	= decoration.specialize(specs);

				createTestsForAllStages(testName.c_str(), defaultColors, defaultColors, fragments, pcs, resources, extensions, testGroup, requiredFeatures);
			}
		}
		{  // unsigned int
			map<string, string>		specs;

			specs["type16"]			= "u16";
			specs["type32"]			= "u32";
			specs["signed"]			= "0";
			specs["count"]			= "64";
			specs["convert"]		= "OpUConvert";

			for (deUint32 constIndexIdx = 0; constIndexIdx < DE_LENGTH_OF_ARRAY(constantIndices); ++constIndexIdx)
			{
				bool			useConstIdx		= constantIndices[constIndexIdx].useConstantIndex;
				deUint32		constIdx		= constantIndices[constIndexIdx].constantIndex;
				string			testName		= "uint_scalar";
				vector<deInt32>	constIdxData;

				if (useConstIdx)
				{
					constIdxData.reserve(numDataPoints);

					for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
						constIdxData.push_back(uOutputs[constIdx]);
				}

				specs["constarrayidx"]	= de::toString(constIdx);
				if (useConstIdx)
					specs["arrayindex"] = "c_i32_ci";
				else
					specs["arrayindex"] = "30";

				if (useConstIdx)
					testName += string("_const_idx_") + de::toString(constIdx);

				resources.outputs.clear();
				resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Int32Buffer(useConstIdx ? constIdxData : uOutputs))));

				fragments["testfun"]	= testFun.specialize(specs);
				fragments["pre_main"]	= preMain.specialize(specs);
				fragments["decoration"]	= decoration.specialize(specs);

				createTestsForAllStages(testName.c_str(), defaultColors, defaultColors, fragments, pcs, resources, extensions, testGroup, requiredFeatures);
			}
		}
	}

	{  // Vector cases
		const StringTemplate	preMain		(
			"    %${base_type16} = OpTypeInt 16 ${signed}\n"
			"         %${type16} = OpTypeVector %${base_type16} 2\n"
			"    %c_i32_${count} = OpConstant %i32 ${count}\n"
			"          %c_i32_ci = OpConstant %i32 ${constarrayidx}\n"
			"%a${count}${type16} = OpTypeArray %${type16} %c_i32_${count}\n"
			"%a${count}${type32} = OpTypeArray %${type32} %c_i32_${count}\n"
			"      %pp_${type16} = OpTypePointer PushConstant %${type16}\n"
			"      %up_${type32} = OpTypePointer Uniform      %${type32}\n"
			"            %SSBO32 = OpTypeStruct %a${count}${type32}\n"
			"         %up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"            %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"              %PC16 = OpTypeStruct %a${count}${type16}\n"
			"           %pp_PC16 = OpTypePointer PushConstant %PC16\n"
			"              %pc16 = OpVariable %pp_PC16 PushConstant\n");

		const StringTemplate	decoration	(
			"OpDecorate %a${count}${type16} ArrayStride 4\n"
			"OpDecorate %a${count}${type32} ArrayStride 8\n"
			"OpDecorate %SSBO32 BufferBlock\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpDecorate %PC16 Block\n"
			"OpMemberDecorate %PC16 0 Offset 0\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 0\n");

		{  // signed int
			map<string, string>		specs;

			specs["base_type16"]	= "i16";
			specs["type16"]			= "v2i16";
			specs["type32"]			= "v2i32";
			specs["signed"]			= "1";
			specs["count"]			= "32";
			specs["convert"]		= "OpSConvert";

			for (deUint32 constIndexIdx = 0; constIndexIdx < DE_LENGTH_OF_ARRAY(constantIndices); ++constIndexIdx)
			{
				bool			useConstIdx		= constantIndices[constIndexIdx].useConstantIndex;
				deUint32		constIdx		= constantIndices[constIndexIdx].constantIndex;
				string			testName		= "sint_vector";
				vector<deInt32>	constIdxData;

				if (useConstIdx)
				{
					constIdxData.reserve(numDataPoints);

					for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
						constIdxData.push_back(sOutputs[constIdx * 2 + numIdx % 2]);
				}

				specs["constarrayidx"]	= de::toString(constIdx);
				if (useConstIdx)
					specs["arrayindex"] = "c_i32_ci";
				else
					specs["arrayindex"] = "30";

				if (useConstIdx)
					testName += string("_const_idx_") + de::toString(constIdx);

				resources.outputs.clear();
				resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Int32Buffer(useConstIdx ? constIdxData : sOutputs))));

				fragments["testfun"]	= testFun.specialize(specs);
				fragments["pre_main"]	= preMain.specialize(specs);
				fragments["decoration"]	= decoration.specialize(specs);

				createTestsForAllStages(testName.c_str(), defaultColors, defaultColors, fragments, pcs, resources, extensions, testGroup, requiredFeatures);
			}
		}
		{  // unsigned int
			map<string, string>		specs;

			specs["base_type16"]	= "u16";
			specs["type16"]			= "v2u16";
			specs["type32"]			= "v2u32";
			specs["signed"]			= "0";
			specs["count"]			= "32";
			specs["convert"]		= "OpUConvert";

			for (deUint32 constIndexIdx = 0; constIndexIdx < DE_LENGTH_OF_ARRAY(constantIndices); ++constIndexIdx)
			{
				bool			useConstIdx		= constantIndices[constIndexIdx].useConstantIndex;
				deUint32		constIdx		= constantIndices[constIndexIdx].constantIndex;
				string			testName		= "uint_vector";
				vector<deInt32>	constIdxData;

				if (useConstIdx)
				{
					constIdxData.reserve(numDataPoints);

					for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
						constIdxData.push_back(uOutputs[constIdx * 2 + numIdx % 2]);
				}

				specs["constarrayidx"]	= de::toString(constIdx);
				if (useConstIdx)
					specs["arrayindex"] = "c_i32_ci";
				else
					specs["arrayindex"] = "30";

				if (useConstIdx)
					testName += string("_const_idx_") + de::toString(constIdx);

				resources.outputs.clear();
				resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Int32Buffer(useConstIdx ? constIdxData : uOutputs))));

				fragments["testfun"]	= testFun.specialize(specs);
				fragments["pre_main"]	= preMain.specialize(specs);
				fragments["decoration"]	= decoration.specialize(specs);

				createTestsForAllStages(testName.c_str(), defaultColors, defaultColors, fragments, pcs, resources, extensions, testGroup, requiredFeatures);
			}
		}
	}
}

void addGraphics16BitStorageUniformInt16To32Group (tcu::TestCaseGroup* testGroup)
{
	de::Random							rnd					(deStringHash(testGroup->getName()));
	map<string, string>					fragments;
	const deUint32						numDataPoints		= 256;
	RGBA								defaultColors[4];
	vector<deInt16>						inputs				= getInt16s(rnd, numDataPoints);
	vector<deInt32>						sOutputs;
	vector<deInt32>						uOutputs;
	GraphicsResources					resources;
	vector<string>						extensions;
	const deUint16						signBitMask			= 0x8000;
	const deUint32						signExtendMask		= 0xffff0000;
	const StringTemplate				capabilities		("OpCapability ${cap}\n");

	sOutputs.reserve(inputs.size());
	uOutputs.reserve(inputs.size());

	for (deUint32 numNdx = 0; numNdx < inputs.size(); ++numNdx)
	{
		uOutputs.push_back(static_cast<deUint16>(inputs[numNdx]));
		if (inputs[numNdx] & signBitMask)
			sOutputs.push_back(static_cast<deInt32>(inputs[numNdx] | signExtendMask));
		else
			sOutputs.push_back(static_cast<deInt32>(inputs[numNdx]));
	}

	resources.inputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Int16Buffer(inputs))));

	extensions.push_back("VK_KHR_16bit_storage");
	fragments["extension"]	= "OpExtension \"SPV_KHR_16bit_storage\"";

	getDefaultColors(defaultColors);

	struct IntegerFacts
	{
		const char*	name;
		const char*	type32;
		const char*	type16;
		const char* opcode;
		bool		isSigned;
	};

	const IntegerFacts	intFacts[]	=
	{
		{"sint",	"%i32",		"%i16",		"OpSConvert",	true},
		{"uint",	"%u32",		"%u16",		"OpUConvert",	false},
	};

	struct ConstantIndex
	{
		bool		useConstantIndex;
		deUint32	constantIndex;
	};

	ConstantIndex	constantIndices[] =
	{
		{ false,	0 },
		{ true,		4 },
		{ true,		5 },
		{ true,		6 }
	};

	const StringTemplate scalarPreMain		(
			"${itype16} = OpTypeInt 16 ${signed}\n"
			"%c_i32_256 = OpConstant %i32 256\n"
			"%c_i32_ci  = OpConstant %i32 ${constarrayidx}\n"
			"   %up_i32 = OpTypePointer Uniform ${itype32}\n"
			"   %up_i16 = OpTypePointer Uniform ${itype16}\n"
			"   %ra_i32 = OpTypeArray ${itype32} %c_i32_256\n"
			"   %ra_i16 = OpTypeArray ${itype16} %c_i32_256\n"
			"   %SSBO32 = OpTypeStruct %ra_i32\n"
			"   %SSBO16 = OpTypeStruct %ra_i16\n"
			"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"%up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
			"   %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"   %ssbo16 = OpVariable %up_SSBO16 Uniform\n");

	const StringTemplate scalarDecoration		(
			"OpDecorate %ra_i32 ArrayStride 4\n"
			"OpDecorate %ra_i16 ArrayStride 2\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpMemberDecorate %SSBO16 0 Offset 0\n"
			"OpDecorate %SSBO32 BufferBlock\n"
			"OpDecorate %SSBO16 ${indecor}\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo16 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 1\n"
			"OpDecorate %ssbo16 Binding 0\n");

	const StringTemplate scalarTestFunc	(
			"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
			"    %param = OpFunctionParameter %v4f32\n"

			"%entry = OpLabel\n"
			"    %i = OpVariable %fp_i32 Function\n"
			"         OpStore %i %c_i32_0\n"
			"         OpBranch %loop\n"

			" %loop = OpLabel\n"
			"   %15 = OpLoad %i32 %i\n"
			"   %lt = OpSLessThan %bool %15 %c_i32_256\n"
			"         OpLoopMerge %merge %inc None\n"
			"         OpBranchConditional %lt %write %merge\n"

			"%write = OpLabel\n"
			"   %30 = OpLoad %i32 %i\n"
			"  %src = OpAccessChain %up_i16 %ssbo16 %c_i32_0 %${arrayindex}\n"
			"%val16 = OpLoad ${itype16} %src\n"
			"%val32 = ${convert} ${itype32} %val16\n"
			"  %dst = OpAccessChain %up_i32 %ssbo32 %c_i32_0 %30\n"
			"         OpStore %dst %val32\n"
			"         OpBranch %inc\n"

			"  %inc = OpLabel\n"
			"   %37 = OpLoad %i32 %i\n"
			"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
			"         OpStore %i %39\n"
			"         OpBranch %loop\n"
			"%merge = OpLabel\n"
			"         OpReturnValue %param\n"

			"OpFunctionEnd\n");

	const StringTemplate vecPreMain		(
			"${itype16} = OpTypeInt 16 ${signed}\n"
			"%c_i32_128 = OpConstant %i32 128\n"
			"%c_i32_ci  = OpConstant %i32 ${constarrayidx}\n"
			"%v2itype16 = OpTypeVector ${itype16} 2\n"
			" %up_v2i32 = OpTypePointer Uniform ${v2itype32}\n"
			" %up_v2i16 = OpTypePointer Uniform %v2itype16\n"
			" %ra_v2i32 = OpTypeArray ${v2itype32} %c_i32_128\n"
			" %ra_v2i16 = OpTypeArray %v2itype16 %c_i32_128\n"
			"   %SSBO32 = OpTypeStruct %ra_v2i32\n"
			"   %SSBO16 = OpTypeStruct %ra_v2i16\n"
			"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"%up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
			"   %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"   %ssbo16 = OpVariable %up_SSBO16 Uniform\n");

	const StringTemplate vecDecoration		(
			"OpDecorate %ra_v2i32 ArrayStride 8\n"
			"OpDecorate %ra_v2i16 ArrayStride 4\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpMemberDecorate %SSBO16 0 Offset 0\n"
			"OpDecorate %SSBO32 BufferBlock\n"
			"OpDecorate %SSBO16 ${indecor}\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo16 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 1\n"
			"OpDecorate %ssbo16 Binding 0\n");

	const StringTemplate vecTestFunc	(
			"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
			"    %param = OpFunctionParameter %v4f32\n"

			"%entry = OpLabel\n"
			"    %i = OpVariable %fp_i32 Function\n"
			"         OpStore %i %c_i32_0\n"
			"         OpBranch %loop\n"

			" %loop = OpLabel\n"
			"   %15 = OpLoad %i32 %i\n"
			"   %lt = OpSLessThan %bool %15 %c_i32_128\n"
			"         OpLoopMerge %merge %inc None\n"
			"         OpBranchConditional %lt %write %merge\n"

			"%write = OpLabel\n"
			"   %30 = OpLoad %i32 %i\n"
			"  %src = OpAccessChain %up_v2i16 %ssbo16 %c_i32_0 %${arrayindex}\n"
			"%val16 = OpLoad %v2itype16 %src\n"
			"%val32 = ${convert} ${v2itype32} %val16\n"
			"  %dst = OpAccessChain %up_v2i32 %ssbo32 %c_i32_0 %30\n"
			"         OpStore %dst %val32\n"
			"         OpBranch %inc\n"

			"  %inc = OpLabel\n"
			"   %37 = OpLoad %i32 %i\n"
			"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
			"         OpStore %i %39\n"
			"         OpBranch %loop\n"
			"%merge = OpLabel\n"
			"         OpReturnValue %param\n"

			"OpFunctionEnd\n");

	struct Category
	{
		const char*				name;
		const StringTemplate&	preMain;
		const StringTemplate&	decoration;
		const StringTemplate&	testFunction;
		const deUint32			numElements;
	};

	const Category		categories[]	=
	{
		{"scalar",	scalarPreMain,	scalarDecoration,	scalarTestFunc,	1},
		{"vector",	vecPreMain,		vecDecoration,		vecTestFunc,	2},
	};

	for (deUint32 catIdx = 0; catIdx < DE_LENGTH_OF_ARRAY(categories); ++catIdx)
		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			for (deUint32 factIdx = 0; factIdx < DE_LENGTH_OF_ARRAY(intFacts); ++factIdx)
				for (deUint32 constIndexIdx = 0; constIndexIdx < DE_LENGTH_OF_ARRAY(constantIndices); ++constIndexIdx)
				{
					bool				useConstIdx		= constantIndices[constIndexIdx].useConstantIndex;
					deUint32			constIdx		= constantIndices[constIndexIdx].constantIndex;
					map<string, string>	specs;
					string				name			= string(CAPABILITIES[capIdx].name) + "_" + categories[catIdx].name + "_" + intFacts[factIdx].name;

					specs["cap"]						= CAPABILITIES[capIdx].cap;
					specs["indecor"]					= CAPABILITIES[capIdx].decor;
					specs["itype32"]					= intFacts[factIdx].type32;
					specs["v2itype32"]					= "%v2" + string(intFacts[factIdx].type32).substr(1);
					specs["v3itype32"]					= "%v3" + string(intFacts[factIdx].type32).substr(1);
					specs["itype16"]					= intFacts[factIdx].type16;
					if (intFacts[factIdx].isSigned)
						specs["signed"]					= "1";
					else
						specs["signed"]					= "0";
					specs["convert"]					= intFacts[factIdx].opcode;
					specs["constarrayidx"]				= de::toString(constIdx);
					if (useConstIdx)
						specs["arrayindex"] = "c_i32_ci";
					else
						specs["arrayindex"] = "30";

					fragments["pre_main"]				= categories[catIdx].preMain.specialize(specs);
					fragments["testfun"]				= categories[catIdx].testFunction.specialize(specs);
					fragments["capability"]				= capabilities.specialize(specs);
					fragments["decoration"]				= categories[catIdx].decoration.specialize(specs);

					vector<deInt32>		constIdxOutputs;
					if (useConstIdx)
					{
						const deUint32 numElements = categories[catIdx].numElements;
						name += string("_const_idx_") + de::toString(constIdx);
						for (deUint32 i = 0; i < numDataPoints; i++)
						{
							deUint32 idx = constIdx * numElements + i % numElements;
							constIdxOutputs.push_back(intFacts[factIdx].isSigned ? sOutputs[idx] : uOutputs[idx]);
						}
					}

					resources.inputs.back().first	= CAPABILITIES[capIdx].dtype;
					resources.outputs.clear();
					if (useConstIdx)
						resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Int32Buffer(constIdxOutputs))));
					else if (intFacts[factIdx].isSigned)
						resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Int32Buffer(sOutputs))));
					else
						resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Int32Buffer(uOutputs))));

					createTestsForAllStages(name, defaultColors, defaultColors, fragments, resources, extensions, testGroup, get16BitStorageFeatures(CAPABILITIES[capIdx].name));
				}
}

void addGraphics16BitStorageUniformFloat16To32Group (tcu::TestCaseGroup* testGroup)
{
	de::Random							rnd					(deStringHash(testGroup->getName()));
	map<string, string>					fragments;
	vector<string>						extensions;
	const deUint32						numDataPoints		= 256;
	RGBA								defaultColors[4];
	const StringTemplate				capabilities		("OpCapability ${cap}\n");
	vector<deFloat16>					float16Data			= getFloat16s(rnd, numDataPoints);

	struct ConstantIndex
	{
		bool		useConstantIndex;
		deUint32	constantIndex;
	};

	ConstantIndex	constantIndices[] =
	{
		{ false,	0 },
		{ true,		4 },
		{ true,		5 },
		{ true,		6 }
	};

	extensions.push_back("VK_KHR_16bit_storage");
	fragments["extension"]	= "OpExtension \"SPV_KHR_16bit_storage\"";

	getDefaultColors(defaultColors);

	{ // scalar cases
		const StringTemplate preMain		(
			"      %f16 = OpTypeFloat 16\n"
			"%c_i32_256 = OpConstant %i32 256\n"
			" %c_i32_ci = OpConstant %i32 ${constarrayidx}\n"
			"   %up_f32 = OpTypePointer Uniform %f32\n"
			"   %up_f16 = OpTypePointer Uniform %f16\n"
			"   %ra_f32 = OpTypeArray %f32 %c_i32_256\n"
			"   %ra_f16 = OpTypeArray %f16 %c_i32_256\n"
			"   %SSBO32 = OpTypeStruct %ra_f32\n"
			"   %SSBO16 = OpTypeStruct %ra_f16\n"
			"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"%up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
			"   %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"   %ssbo16 = OpVariable %up_SSBO16 Uniform\n");

		const StringTemplate decoration		(
			"OpDecorate %ra_f32 ArrayStride 4\n"
			"OpDecorate %ra_f16 ArrayStride 2\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpMemberDecorate %SSBO16 0 Offset 0\n"
			"OpDecorate %SSBO32 BufferBlock\n"
			"OpDecorate %SSBO16 ${indecor}\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo16 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 1\n"
			"OpDecorate %ssbo16 Binding 0\n");

		// ssbo32[] <- convert ssbo16[] to 32bit float
		const StringTemplate testFun		(
			"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
			"    %param = OpFunctionParameter %v4f32\n"

			"%entry = OpLabel\n"
			"    %i = OpVariable %fp_i32 Function\n"
			"         OpStore %i %c_i32_0\n"
			"         OpBranch %loop\n"

			" %loop = OpLabel\n"
			"   %15 = OpLoad %i32 %i\n"
			"   %lt = OpSLessThan %bool %15 %c_i32_256\n"
			"         OpLoopMerge %merge %inc None\n"
			"         OpBranchConditional %lt %write %merge\n"

			"%write = OpLabel\n"
			"   %30 = OpLoad %i32 %i\n"
			"  %src = OpAccessChain %up_f16 %ssbo16 %c_i32_0 %${arrayindex}\n"
			"%val16 = OpLoad %f16 %src\n"
			"%val32 = OpFConvert %f32 %val16\n"
			"  %dst = OpAccessChain %up_f32 %ssbo32 %c_i32_0 %30\n"
			"         OpStore %dst %val32\n"
			"         OpBranch %inc\n"

			"  %inc = OpLabel\n"
			"   %37 = OpLoad %i32 %i\n"
			"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
			"         OpStore %i %39\n"
			"         OpBranch %loop\n"

			"%merge = OpLabel\n"
			"         OpReturnValue %param\n"

			"OpFunctionEnd\n");

		for (deUint32 constIndexIdx = 0; constIndexIdx < DE_LENGTH_OF_ARRAY(constantIndices); ++constIndexIdx)
		{
			for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			{
				GraphicsResources	resources;
				map<string, string>	specs;
				string				testName	= string(CAPABILITIES[capIdx].name) + "_scalar_float";
				bool				useConstIdx	= constantIndices[constIndexIdx].useConstantIndex;
				deUint32			constIdx	= constantIndices[constIndexIdx].constantIndex;

				specs["cap"]					= CAPABILITIES[capIdx].cap;
				specs["indecor"]				= CAPABILITIES[capIdx].decor;
				specs["constarrayidx"]			= de::toString(constIdx);
				if (useConstIdx)
					specs["arrayindex"] = "c_i32_ci";
				else
					specs["arrayindex"] = "30";

				fragments["capability"]			= capabilities.specialize(specs);
				fragments["decoration"]			= decoration.specialize(specs);
				fragments["pre_main"]			= preMain.specialize(specs);
				fragments["testfun"]			= testFun.specialize(specs);

				vector<float>		float32Data;
				float32Data.reserve(numDataPoints);
				for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
					float32Data.push_back(deFloat16To32(float16Data[useConstIdx ? constIdx : numIdx]));

				resources.inputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float16Buffer(float16Data))));
				resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float32Buffer(float32Data))));
				resources.verifyIO = check32BitFloats;
				resources.inputs.back().first	= CAPABILITIES[capIdx].dtype;

				if (useConstIdx)
					testName += string("_const_idx_") + de::toString(constIdx);

				createTestsForAllStages(testName, defaultColors, defaultColors, fragments, resources, extensions, testGroup, get16BitStorageFeatures(CAPABILITIES[capIdx].name));
			}
		}
	}

	{ // vector cases
		const StringTemplate preMain		(
			"      %f16 = OpTypeFloat 16\n"
			"%c_i32_128 = OpConstant %i32 128\n"
			"%c_i32_ci  = OpConstant %i32 ${constarrayidx}\n"
			"	 %v2f16 = OpTypeVector %f16 2\n"
			" %up_v2f32 = OpTypePointer Uniform %v2f32\n"
			" %up_v2f16 = OpTypePointer Uniform %v2f16\n"
			" %ra_v2f32 = OpTypeArray %v2f32 %c_i32_128\n"
			" %ra_v2f16 = OpTypeArray %v2f16 %c_i32_128\n"
			"   %SSBO32 = OpTypeStruct %ra_v2f32\n"
			"   %SSBO16 = OpTypeStruct %ra_v2f16\n"
			"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"%up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
			"   %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"   %ssbo16 = OpVariable %up_SSBO16 Uniform\n");

		const StringTemplate decoration		(
			"OpDecorate %ra_v2f32 ArrayStride 8\n"
			"OpDecorate %ra_v2f16 ArrayStride 4\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpMemberDecorate %SSBO16 0 Offset 0\n"
			"OpDecorate %SSBO32 BufferBlock\n"
			"OpDecorate %SSBO16 ${indecor}\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo16 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 1\n"
			"OpDecorate %ssbo16 Binding 0\n");

		// ssbo32[] <- convert ssbo16[] to 32bit float
		const StringTemplate testFun		(
			"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
			"    %param = OpFunctionParameter %v4f32\n"

			"%entry = OpLabel\n"
			"    %i = OpVariable %fp_i32 Function\n"
			"         OpStore %i %c_i32_0\n"
			"         OpBranch %loop\n"

			" %loop = OpLabel\n"
			"   %15 = OpLoad %i32 %i\n"
			"   %lt = OpSLessThan %bool %15 %c_i32_128\n"
			"         OpLoopMerge %merge %inc None\n"
			"         OpBranchConditional %lt %write %merge\n"

			"%write = OpLabel\n"
			"   %30 = OpLoad %i32 %i\n"
			"  %src = OpAccessChain %up_v2f16 %ssbo16 %c_i32_0 %${arrayindex}\n"
			"%val16 = OpLoad %v2f16 %src\n"
			"%val32 = OpFConvert %v2f32 %val16\n"
			"  %dst = OpAccessChain %up_v2f32 %ssbo32 %c_i32_0 %30\n"
			"         OpStore %dst %val32\n"
			"         OpBranch %inc\n"

			"  %inc = OpLabel\n"
			"   %37 = OpLoad %i32 %i\n"
			"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
			"         OpStore %i %39\n"
			"         OpBranch %loop\n"

			"%merge = OpLabel\n"
			"         OpReturnValue %param\n"

			"OpFunctionEnd\n");

		for (deUint32 constIndexIdx = 0; constIndexIdx < DE_LENGTH_OF_ARRAY(constantIndices); ++constIndexIdx)
		{
			for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			{
				GraphicsResources	resources;
				map<string, string>	specs;
				string				testName	= string(CAPABILITIES[capIdx].name) + "_vector_float";
				bool				useConstIdx	= constantIndices[constIndexIdx].useConstantIndex;
				deUint32			constIdx	= constantIndices[constIndexIdx].constantIndex;

				specs["cap"]					= CAPABILITIES[capIdx].cap;
				specs["indecor"]				= CAPABILITIES[capIdx].decor;
				specs["constarrayidx"]			= de::toString(constIdx);
				if (useConstIdx)
					specs["arrayindex"] = "c_i32_ci";
				else
					specs["arrayindex"] = "30";

				fragments["capability"]			= capabilities.specialize(specs);
				fragments["decoration"]			= decoration.specialize(specs);
				fragments["pre_main"]			= preMain.specialize(specs);
				fragments["testfun"]			= testFun.specialize(specs);

				vector<float>		float32Data;
				float32Data.reserve(numDataPoints);
				for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
					float32Data.push_back(deFloat16To32(float16Data[constantIndices[constIndexIdx].useConstantIndex ? (constantIndices[constIndexIdx].constantIndex * 2 + numIdx % 2) : numIdx]));

				resources.inputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float16Buffer(float16Data))));
				resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float32Buffer(float32Data))));
				resources.verifyIO = check32BitFloats;
				resources.inputs.back().first	= CAPABILITIES[capIdx].dtype;

				if (constantIndices[constIndexIdx].useConstantIndex)
					testName += string("_const_idx_") + de::toString(constantIndices[constIndexIdx].constantIndex);

				createTestsForAllStages(testName, defaultColors, defaultColors, fragments, resources, extensions, testGroup, get16BitStorageFeatures(CAPABILITIES[capIdx].name));
			}
		}
	}

	{ // matrix cases
		fragments["pre_main"]				=
			" %c_i32_32 = OpConstant %i32 32\n"
			"      %f16 = OpTypeFloat 16\n"
			"    %v2f16 = OpTypeVector %f16 2\n"
			"  %m4x2f32 = OpTypeMatrix %v2f32 4\n"
			"  %m4x2f16 = OpTypeMatrix %v2f16 4\n"
			" %up_v2f32 = OpTypePointer Uniform %v2f32\n"
			" %up_v2f16 = OpTypePointer Uniform %v2f16\n"
			"%a8m4x2f32 = OpTypeArray %m4x2f32 %c_i32_32\n"
			"%a8m4x2f16 = OpTypeArray %m4x2f16 %c_i32_32\n"
			"   %SSBO32 = OpTypeStruct %a8m4x2f32\n"
			"   %SSBO16 = OpTypeStruct %a8m4x2f16\n"
			"%up_SSBO32 = OpTypePointer Uniform %SSBO32\n"
			"%up_SSBO16 = OpTypePointer Uniform %SSBO16\n"
			"   %ssbo32 = OpVariable %up_SSBO32 Uniform\n"
			"   %ssbo16 = OpVariable %up_SSBO16 Uniform\n";

		const StringTemplate decoration		(
			"OpDecorate %a8m4x2f32 ArrayStride 32\n"
			"OpDecorate %a8m4x2f16 ArrayStride 16\n"
			"OpMemberDecorate %SSBO32 0 Offset 0\n"
			"OpMemberDecorate %SSBO32 0 ColMajor\n"
			"OpMemberDecorate %SSBO32 0 MatrixStride 8\n"
			"OpMemberDecorate %SSBO16 0 Offset 0\n"
			"OpMemberDecorate %SSBO16 0 ColMajor\n"
			"OpMemberDecorate %SSBO16 0 MatrixStride 4\n"
			"OpDecorate %SSBO32 BufferBlock\n"
			"OpDecorate %SSBO16 ${indecor}\n"
			"OpDecorate %ssbo32 DescriptorSet 0\n"
			"OpDecorate %ssbo16 DescriptorSet 0\n"
			"OpDecorate %ssbo32 Binding 1\n"
			"OpDecorate %ssbo16 Binding 0\n");

		fragments["testfun"]				=
			"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
			"    %param = OpFunctionParameter %v4f32\n"

			"%entry = OpLabel\n"
			"    %i = OpVariable %fp_i32 Function\n"
			"         OpStore %i %c_i32_0\n"
			"         OpBranch %loop\n"

			" %loop = OpLabel\n"
			"   %15 = OpLoad %i32 %i\n"
			"   %lt = OpSLessThan %bool %15 %c_i32_32\n"
			"         OpLoopMerge %merge %inc None\n"
			"         OpBranchConditional %lt %write %merge\n"

			"  %write = OpLabel\n"
			"     %30 = OpLoad %i32 %i\n"
			"  %src_0 = OpAccessChain %up_v2f16 %ssbo16 %c_i32_0 %30 %c_i32_0\n"
			"  %src_1 = OpAccessChain %up_v2f16 %ssbo16 %c_i32_0 %30 %c_i32_1\n"
			"  %src_2 = OpAccessChain %up_v2f16 %ssbo16 %c_i32_0 %30 %c_i32_2\n"
			"  %src_3 = OpAccessChain %up_v2f16 %ssbo16 %c_i32_0 %30 %c_i32_3\n"
			"%val16_0 = OpLoad %v2f16 %src_0\n"
			"%val16_1 = OpLoad %v2f16 %src_1\n"
			"%val16_2 = OpLoad %v2f16 %src_2\n"
			"%val16_3 = OpLoad %v2f16 %src_3\n"
			"%val32_0 = OpFConvert %v2f32 %val16_0\n"
			"%val32_1 = OpFConvert %v2f32 %val16_1\n"
			"%val32_2 = OpFConvert %v2f32 %val16_2\n"
			"%val32_3 = OpFConvert %v2f32 %val16_3\n"
			"  %dst_0 = OpAccessChain %up_v2f32 %ssbo32 %c_i32_0 %30 %c_i32_0\n"
			"  %dst_1 = OpAccessChain %up_v2f32 %ssbo32 %c_i32_0 %30 %c_i32_1\n"
			"  %dst_2 = OpAccessChain %up_v2f32 %ssbo32 %c_i32_0 %30 %c_i32_2\n"
			"  %dst_3 = OpAccessChain %up_v2f32 %ssbo32 %c_i32_0 %30 %c_i32_3\n"
			"           OpStore %dst_0 %val32_0\n"
			"           OpStore %dst_1 %val32_1\n"
			"           OpStore %dst_2 %val32_2\n"
			"           OpStore %dst_3 %val32_3\n"
			"           OpBranch %inc\n"

			"  %inc = OpLabel\n"
			"   %37 = OpLoad %i32 %i\n"
			"   %39 = OpIAdd %i32 %37 %c_i32_1\n"
			"         OpStore %i %39\n"
			"         OpBranch %loop\n"

			"%merge = OpLabel\n"
			"         OpReturnValue %param\n"

			"OpFunctionEnd\n";

			for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
			{
				GraphicsResources	resources;
				map<string, string>	specs;
				string				testName	= string(CAPABILITIES[capIdx].name) + "_matrix_float";

				specs["cap"]					= CAPABILITIES[capIdx].cap;
				specs["indecor"]				= CAPABILITIES[capIdx].decor;

				fragments["capability"]			= capabilities.specialize(specs);
				fragments["decoration"]			= decoration.specialize(specs);

				vector<float>		float32Data;
				float32Data.reserve(numDataPoints);
				for (deUint32 numIdx = 0; numIdx < numDataPoints; ++numIdx)
					float32Data.push_back(deFloat16To32(float16Data[numIdx]));

				resources.inputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float16Buffer(float16Data))));
				resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float32Buffer(float32Data))));
				resources.verifyIO = check32BitFloats;
				resources.inputs.back().first	= CAPABILITIES[capIdx].dtype;

				createTestsForAllStages(testName, defaultColors, defaultColors, fragments, resources, extensions, testGroup, get16BitStorageFeatures(CAPABILITIES[capIdx].name));
		}
	}
}

void addGraphics16BitStorageUniformStructFloat16To32Group (tcu::TestCaseGroup* testGroup)
{
	de::Random							rnd					(deStringHash(testGroup->getName()));
	map<string, string>					fragments;
	vector<string>						extensions;
	RGBA								defaultColors[4];
	const StringTemplate				capabilities		("OpCapability ${cap}\n");
	vector<float>						float32Data			(getStructSize(SHADERTEMPLATE_STRIDE32BIT_STD430), 0.0f);

	extensions.push_back("VK_KHR_16bit_storage");
	fragments["extension"]	= "OpExtension \"SPV_KHR_16bit_storage\"";

	getDefaultColors(defaultColors);

	const StringTemplate preMain		(
		"\n"
		"${types}\n"
		"\n"
		"%zero = OpConstant %i32 0\n"
		"%c_i32_5 = OpConstant %i32 5\n"
		"%c_i32_6 = OpConstant %i32 6\n"
		"%c_i32_7 = OpConstant %i32 7\n"
		"%c_i32_8 = OpConstant %i32 8\n"
		"%c_i32_9 = OpConstant %i32 9\n"
		"%c_i32_11 = OpConstant %i32 11\n"
		"\n"
		"%c_u32_7 = OpConstant %u32 7\n"
		"%c_u32_11 = OpConstant %u32 11\n"
		"\n"
		"%f16arr3       = OpTypeArray %f16 %c_u32_3\n"
		"%v2f16arr3    = OpTypeArray %v2f16 %c_u32_3\n"
		"%v2f16arr11    = OpTypeArray %v2f16 %c_u32_11\n"
		"%v3f16arr11    = OpTypeArray %v3f16 %c_u32_11\n"
		"%v4f16arr3     = OpTypeArray %v4f16 %c_u32_3\n"
		"%struct16      = OpTypeStruct %f16 %v2f16arr3\n"
		"%struct16arr11 = OpTypeArray %struct16 %c_u32_11\n"
		"%f16Struct = OpTypeStruct %f16 %v2f16 %v3f16 %v4f16 %f16arr3 %struct16arr11 %v2f16arr11 %f16 %v3f16arr11 %v4f16arr3\n"
		"\n"
		"%f32arr3   = OpTypeArray %f32 %c_u32_3\n"
		"%v2f32arr3 = OpTypeArray %v2f32 %c_u32_3\n"
		"%v2f32arr11 = OpTypeArray %v2f32 %c_u32_11\n"
		"%v3f32arr11 = OpTypeArray %v3f32 %c_u32_11\n"
		"%v4f32arr3 = OpTypeArray %v4f32 %c_u32_3\n"
		"%struct32      = OpTypeStruct %f32 %v2f32arr3\n"
		"%struct32arr11 = OpTypeArray %struct32 %c_u32_11\n"
		"%f32Struct = OpTypeStruct %f32 %v2f32 %v3f32 %v4f32 %f32arr3 %struct32arr11 %v2f32arr11 %f32 %v3f32arr11 %v4f32arr3\n"
		"\n"
		"%f16StructArr7      = OpTypeArray %f16Struct %c_u32_7\n"
		"%f32StructArr7      = OpTypeArray %f32Struct %c_u32_7\n"
		"%SSBO_IN            = OpTypeStruct %f16StructArr7\n"
		"%SSBO_OUT           = OpTypeStruct %f32StructArr7\n"
		"%up_SSBOIN          = OpTypePointer Uniform %SSBO_IN\n"
		"%up_SSBOOUT         = OpTypePointer Uniform %SSBO_OUT\n"
		"%ssboIN             = OpVariable %up_SSBOIN Uniform\n"
		"%ssboOUT            = OpVariable %up_SSBOOUT Uniform\n"
		"\n");

	const StringTemplate decoration		(
		"${strideF16}"
		"\n"
		"${strideF32}"
		"\n"
		"OpMemberDecorate %SSBO_IN 0 Offset 0\n"
		"OpMemberDecorate %SSBO_OUT 0 Offset 0\n"
		"OpDecorate %SSBO_IN ${indecor}\n"
		"OpDecorate %SSBO_OUT BufferBlock\n"
		"OpDecorate %ssboIN DescriptorSet 0\n"
		"OpDecorate %ssboOUT DescriptorSet 0\n"
		"OpDecorate %ssboIN Binding 0\n"
		"OpDecorate %ssboOUT Binding 1\n"
		"\n");

	fragments["testfun"]			=
		"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
		"    %param = OpFunctionParameter %v4f32\n"
		"%label     = OpLabel\n"
		"%loopNdx    = OpVariable %fp_i32 Function\n"
		"%insideLoopNdx = OpVariable %fp_i32 Function\n"

		"OpStore %loopNdx %zero\n"
		"OpBranch %loop\n"
		"%loop = OpLabel\n"
		"OpLoopMerge %merge %13 None\n"
		"OpBranch %14\n"
		"%14 = OpLabel\n"
		"%valLoopNdx = OpLoad %i32 %loopNdx\n"
		"%18 = OpSLessThan %bool %valLoopNdx %c_i32_7\n"
		"OpBranchConditional %18 %11 %merge\n"
		"%11 = OpLabel\n"
		"\n"
		"%f16src  = OpAccessChain %f16ptr %ssboIN %zero %valLoopNdx %zero\n"
		"%val_f16 = OpLoad %f16 %f16src\n"
		"%val_f32 = OpFConvert %f32 %val_f16\n"
		"%f32dst  = OpAccessChain %f32ptr %ssboOUT %zero %valLoopNdx %zero\n"
		"OpStore %f32dst %val_f32\n"
		"\n"
		"%v2f16src  = OpAccessChain %v2f16ptr %ssboIN %zero %valLoopNdx %c_i32_1\n"
		"%val_v2f16 = OpLoad %v2f16 %v2f16src\n"
		"%val_v2f32 = OpFConvert %v2f32 %val_v2f16\n"
		"%v2f32dst  = OpAccessChain %v2f32ptr %ssboOUT %zero %valLoopNdx %c_i32_1\n"
		"OpStore %v2f32dst %val_v2f32\n"
		"\n"
		"%v3f16src  = OpAccessChain %v3f16ptr %ssboIN %zero %valLoopNdx %c_i32_2\n"
		"%val_v3f16 = OpLoad %v3f16 %v3f16src\n"
		"%val_v3f32 = OpFConvert %v3f32 %val_v3f16\n"
		"%v3f32dst  = OpAccessChain %v3f32ptr %ssboOUT %zero %valLoopNdx %c_i32_2\n"
		"OpStore %v3f32dst %val_v3f32\n"
		"\n"
		"%v4f16src  = OpAccessChain %v4f16ptr %ssboIN %zero %valLoopNdx %c_i32_3\n"
		"%val_v4f16 = OpLoad %v4f16 %v4f16src\n"
		"%val_v4f32 = OpFConvert %v4f32 %val_v4f16\n"
		"%v4f32dst  = OpAccessChain %v4f32ptr %ssboOUT %zero %valLoopNdx %c_i32_3\n"
		"OpStore %v4f32dst %val_v4f32\n"
		"\n"
		"%f16src2  = OpAccessChain %f16ptr %ssboIN %zero %valLoopNdx %c_i32_7\n"
		"%val2_f16 = OpLoad %f16 %f16src2\n"
		"%val2_f32 = OpFConvert %f32 %val2_f16\n"
		"%f32dst2  = OpAccessChain %f32ptr %ssboOUT %zero %valLoopNdx %c_i32_7\n"
		"OpStore %f32dst2 %val2_f32\n"
		"\n"
		"OpStore %insideLoopNdx %zero\n"
		"OpBranch %loopInside\n"
		"%loopInside = OpLabel\n"
		"OpLoopMerge %92 %93 None\n"
		"OpBranch %94\n"
		"%94 = OpLabel\n"
		"%valInsideLoopNdx = OpLoad %i32 %insideLoopNdx\n"
		"%96 = OpSLessThan %bool %valInsideLoopNdx %c_i32_11\n"
		"OpBranchConditional %96 %91 %92\n"
		"\n"
		"%91 = OpLabel\n"
		"\n"
		"%v2f16src2  = OpAccessChain %v2f16ptr %ssboIN %zero %valLoopNdx %c_i32_6 %valInsideLoopNdx\n"
		"%val2_v2f16 = OpLoad %v2f16 %v2f16src2\n"
		"%val2_v2f32 = OpFConvert %v2f32 %val2_v2f16\n"
		"%v2f32dst2  = OpAccessChain %v2f32ptr %ssboOUT %zero %valLoopNdx %c_i32_6 %valInsideLoopNdx\n"
		"OpStore %v2f32dst2 %val2_v2f32\n"
		"\n"
		"%v3f16src2  = OpAccessChain %v3f16ptr %ssboIN %zero %valLoopNdx %c_i32_8 %valInsideLoopNdx\n"
		"%val2_v3f16 = OpLoad %v3f16 %v3f16src2\n"
		"%val2_v3f32 = OpFConvert %v3f32 %val2_v3f16\n"
		"%v3f32dst2  = OpAccessChain %v3f32ptr %ssboOUT %zero %valLoopNdx %c_i32_8 %valInsideLoopNdx\n"
		"OpStore %v3f32dst2 %val2_v3f32\n"
		"\n"
		//struct {f16, v2f16[3]}
		"%Sf16src  = OpAccessChain %f16ptr %ssboIN %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %zero\n"
		"%Sval_f16 = OpLoad %f16 %Sf16src\n"
		"%Sval_f32 = OpFConvert %f32 %Sval_f16\n"
		"%Sf32dst2  = OpAccessChain %f32ptr %ssboOUT %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %zero\n"
		"OpStore %Sf32dst2 %Sval_f32\n"
		"\n"
		"%Sv2f16src0   = OpAccessChain %v2f16ptr %ssboIN %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %zero\n"
		"%Sv2f16_0     = OpLoad %v2f16 %Sv2f16src0\n"
		"%Sv2f32_0     = OpFConvert %v2f32 %Sv2f16_0\n"
		"%Sv2f32dst_0  = OpAccessChain %v2f32ptr %ssboOUT %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %zero\n"
		"OpStore %Sv2f32dst_0 %Sv2f32_0\n"
		"\n"
		"%Sv2f16src1  = OpAccessChain %v2f16ptr %ssboIN %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %c_i32_1\n"
		"%Sv2f16_1 = OpLoad %v2f16 %Sv2f16src1\n"
		"%Sv2f32_1 = OpFConvert %v2f32 %Sv2f16_1\n"
		"%Sv2f32dst_1  = OpAccessChain %v2f32ptr %ssboOUT %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %c_i32_1\n"
		"OpStore %Sv2f32dst_1 %Sv2f32_1\n"
		"\n"
		"%Sv2f16src2  = OpAccessChain %v2f16ptr %ssboIN %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %c_i32_2\n"
		"%Sv2f16_2 = OpLoad %v2f16 %Sv2f16src2\n"
		"%Sv2f32_2 = OpFConvert %v2f32 %Sv2f16_2\n"
		"%Sv2f32dst_2  = OpAccessChain %v2f32ptr %ssboOUT %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %c_i32_2\n"
		"OpStore %Sv2f32dst_2 %Sv2f32_2\n"
		"\n"
		//Array with 3 elements
		"%LessThan3 = OpSLessThan %bool %valInsideLoopNdx %c_i32_3\n"
		"OpSelectionMerge %BlockIf None\n"
		"OpBranchConditional %LessThan3 %LabelIf %BlockIf\n"
		"%LabelIf = OpLabel\n"
		"  %f16src3  = OpAccessChain %f16ptr %ssboIN %zero %valLoopNdx %c_i32_4 %valInsideLoopNdx\n"
		"  %val3_f16 = OpLoad %f16 %f16src3\n"
		"  %val3_f32 = OpFConvert %f32 %val3_f16\n"
		"  %f32dst3  = OpAccessChain %f32ptr %ssboOUT %zero %valLoopNdx %c_i32_4 %valInsideLoopNdx\n"
		"  OpStore %f32dst3 %val3_f32\n"
		"\n"
		"  %v4f16src2  = OpAccessChain %v4f16ptr %ssboIN %zero %valLoopNdx %c_i32_9 %valInsideLoopNdx\n"
		"  %val2_v4f16 = OpLoad %v4f16 %v4f16src2\n"
		"  %val2_v4f32 = OpFConvert %v4f32 %val2_v4f16\n"
		"  %v4f32dst2  = OpAccessChain %v4f32ptr %ssboOUT %zero %valLoopNdx %c_i32_9 %valInsideLoopNdx\n"
		"  OpStore %v4f32dst2 %val2_v4f32\n"
		"OpBranch %BlockIf\n"
		"%BlockIf = OpLabel\n"
		"\n"
		"OpBranch %93\n"
		"%93 = OpLabel\n"
		"%132 = OpLoad %i32 %insideLoopNdx\n"
		"%133 = OpIAdd %i32 %132 %c_i32_1\n"
		"OpStore %insideLoopNdx %133\n"
		"OpBranch %loopInside\n"
		"\n"
		"%92 = OpLabel\n"
		"OpBranch %13\n"
		"%13 = OpLabel\n"
		"%134 = OpLoad %i32 %loopNdx\n"
		"%135 = OpIAdd %i32 %134 %c_i32_1\n"
		"OpStore %loopNdx %135\n"
		"OpBranch %loop\n"

		"%merge = OpLabel\n"
		"         OpReturnValue %param\n"
		"         OpFunctionEnd\n";

		for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
		{
			vector<deFloat16>	float16Data	= (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ? data16bitStd430(rnd) :  data16bitStd140(rnd);
			GraphicsResources	resources;
			map<string, string>	specs;
			string				testName	= string(CAPABILITIES[capIdx].name);

			specs["cap"]					= CAPABILITIES[capIdx].cap;
			specs["indecor"]				= CAPABILITIES[capIdx].decor;
			specs["strideF16"]				= getStructShaderComponet((VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ? SHADERTEMPLATE_STRIDE16BIT_STD430 : SHADERTEMPLATE_STRIDE16BIT_STD140);
			specs["strideF32"]				= getStructShaderComponet(SHADERTEMPLATE_STRIDE32BIT_STD430);
			specs["types"]					= getStructShaderComponet(SHADERTEMPLATE_TYPES);

			fragments["capability"]			= capabilities.specialize(specs);
			fragments["decoration"]			= decoration.specialize(specs);
			fragments["pre_main"]			= preMain.specialize(specs);

			resources.inputs.push_back(std::make_pair(CAPABILITIES[capIdx].dtype, BufferSp(new Float16Buffer(float16Data))));
			resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float32Buffer(float32Data))));
			resources.verifyIO = (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ? graphicsCheckStruct<deFloat16, float, SHADERTEMPLATE_STRIDE16BIT_STD430, SHADERTEMPLATE_STRIDE32BIT_STD430> : graphicsCheckStruct<deFloat16, float, SHADERTEMPLATE_STRIDE16BIT_STD140, SHADERTEMPLATE_STRIDE32BIT_STD430>;

			createTestsForAllStages(testName, defaultColors, defaultColors, fragments, resources, extensions, testGroup, get16BitStorageFeatures(CAPABILITIES[capIdx].name));
		}
}

void addGraphics16BitStorageUniformStructFloat32To16Group (tcu::TestCaseGroup* testGroup)
{
	de::Random							rnd					(deStringHash(testGroup->getName()));
	map<string, string>					fragments;
	vector<string>						extensions;
	RGBA								defaultColors[4];
	const StringTemplate				capabilities		("OpCapability ${cap}\n");
	vector<deUint16>					float16Data			(getStructSize(SHADERTEMPLATE_STRIDE16BIT_STD430), 0u);

	extensions.push_back("VK_KHR_16bit_storage");
	fragments["extension"]	= "OpExtension \"SPV_KHR_16bit_storage\"";

	getDefaultColors(defaultColors);

	const StringTemplate preMain		(
		"\n"
		"${types}\n"
		"\n"
		"%zero = OpConstant %i32 0\n"
		"%c_i32_5 = OpConstant %i32 5\n"
		"%c_i32_6 = OpConstant %i32 6\n"
		"%c_i32_7 = OpConstant %i32 7\n"
		"%c_i32_8 = OpConstant %i32 8\n"
		"%c_i32_9 = OpConstant %i32 9\n"
		"%c_i32_11 = OpConstant %i32 11\n"
		"\n"
		"%c_u32_7 = OpConstant %u32 7\n"
		"%c_u32_11 = OpConstant %u32 11\n"
		"\n"
		"%f16arr3       = OpTypeArray %f16 %c_u32_3\n"
		"%v2f16arr3    = OpTypeArray %v2f16 %c_u32_3\n"
		"%v2f16arr11    = OpTypeArray %v2f16 %c_u32_11\n"
		"%v3f16arr11    = OpTypeArray %v3f16 %c_u32_11\n"
		"%v4f16arr3     = OpTypeArray %v4f16 %c_u32_3\n"
		"%struct16      = OpTypeStruct %f16 %v2f16arr3\n"
		"%struct16arr11 = OpTypeArray %struct16 %c_u32_11\n"
		"%f16Struct = OpTypeStruct %f16 %v2f16 %v3f16 %v4f16 %f16arr3 %struct16arr11 %v2f16arr11 %f16 %v3f16arr11 %v4f16arr3\n"
		"\n"
		"%f32arr3   = OpTypeArray %f32 %c_u32_3\n"
		"%v2f32arr3 = OpTypeArray %v2f32 %c_u32_3\n"
		"%v2f32arr11 = OpTypeArray %v2f32 %c_u32_11\n"
		"%v3f32arr11 = OpTypeArray %v3f32 %c_u32_11\n"
		"%v4f32arr3 = OpTypeArray %v4f32 %c_u32_3\n"
		"%struct32      = OpTypeStruct %f32 %v2f32arr3\n"
		"%struct32arr11 = OpTypeArray %struct32 %c_u32_11\n"
		"%f32Struct = OpTypeStruct %f32 %v2f32 %v3f32 %v4f32 %f32arr3 %struct32arr11 %v2f32arr11 %f32 %v3f32arr11 %v4f32arr3\n"
		"\n"
		"%f16StructArr7      = OpTypeArray %f16Struct %c_u32_7\n"
		"%f32StructArr7      = OpTypeArray %f32Struct %c_u32_7\n"
		"%SSBO_IN            = OpTypeStruct %f32StructArr7\n"
		"%SSBO_OUT           = OpTypeStruct %f16StructArr7\n"
		"%up_SSBOIN          = OpTypePointer Uniform %SSBO_IN\n"
		"%up_SSBOOUT         = OpTypePointer Uniform %SSBO_OUT\n"
		"%ssboIN             = OpVariable %up_SSBOIN Uniform\n"
		"%ssboOUT            = OpVariable %up_SSBOOUT Uniform\n"
		"\n");

	const StringTemplate decoration		(
		"${strideF16}"
		"\n"
		"${strideF32}"
		"\n"
		"OpMemberDecorate %SSBO_IN 0 Offset 0\n"
		"OpMemberDecorate %SSBO_OUT 0 Offset 0\n"
		"OpDecorate %SSBO_IN ${indecor}\n"
		"OpDecorate %SSBO_OUT BufferBlock\n"
		"OpDecorate %ssboIN DescriptorSet 0\n"
		"OpDecorate %ssboOUT DescriptorSet 0\n"
		"OpDecorate %ssboIN Binding 0\n"
		"OpDecorate %ssboOUT Binding 1\n"
		"\n");

	fragments["testfun"]			=
		"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
		"%param = OpFunctionParameter %v4f32\n"
		"%label     = OpLabel\n"
		"%loopNdx    = OpVariable %fp_i32 Function\n"
		"%insideLoopNdx = OpVariable %fp_i32 Function\n"

		"OpStore %loopNdx %zero\n"
		"OpBranch %loop\n"
		"%loop = OpLabel\n"
		"OpLoopMerge %merge %13 None\n"
		"OpBranch %14\n"
		"%14 = OpLabel\n"
		"%valLoopNdx = OpLoad %i32 %loopNdx\n"
		"%18 = OpSLessThan %bool %valLoopNdx %c_i32_7\n"
		"OpBranchConditional %18 %11 %merge\n"
		"%11 = OpLabel\n"
		"\n"
		"%f32src  = OpAccessChain %f32ptr %ssboIN %zero %valLoopNdx %zero\n"
		"%val_f32 = OpLoad %f32 %f32src\n"
		"%val_f16 = OpFConvert %f16 %val_f32\n"
		"%f16dst  = OpAccessChain %f16ptr %ssboOUT %zero %valLoopNdx %zero\n"
		"OpStore %f16dst %val_f16\n"
		"\n"
		"%v2f32src  = OpAccessChain %v2f32ptr %ssboIN %zero %valLoopNdx %c_i32_1\n"
		"%val_v2f32 = OpLoad %v2f32 %v2f32src\n"
		"%val_v2f16 = OpFConvert %v2f16 %val_v2f32\n"
		"%v2f16dst  = OpAccessChain %v2f16ptr %ssboOUT %zero %valLoopNdx %c_i32_1\n"
		"OpStore %v2f16dst %val_v2f16\n"
		"\n"
		"%v3f32src  = OpAccessChain %v3f32ptr %ssboIN %zero %valLoopNdx %c_i32_2\n"
		"%val_v3f32 = OpLoad %v3f32 %v3f32src\n"
		"%val_v3f16 = OpFConvert %v3f16 %val_v3f32\n"
		"%v3f16dst  = OpAccessChain %v3f16ptr %ssboOUT %zero %valLoopNdx %c_i32_2\n"
		"OpStore %v3f16dst %val_v3f16\n"
		"\n"
		"%v4f32src  = OpAccessChain %v4f32ptr %ssboIN %zero %valLoopNdx %c_i32_3\n"
		"%val_v4f32 = OpLoad %v4f32 %v4f32src\n"
		"%val_v4f16 = OpFConvert %v4f16 %val_v4f32\n"
		"%v4f16dst  = OpAccessChain %v4f16ptr %ssboOUT %zero %valLoopNdx %c_i32_3\n"
		"OpStore %v4f16dst %val_v4f16\n"
		"\n"
		"%f32src2  = OpAccessChain %f32ptr %ssboIN %zero %valLoopNdx %c_i32_7\n"
		"%val2_f32 = OpLoad %f32 %f32src2\n"
		"%val2_f16 = OpFConvert %f16 %val2_f32\n"
		"%f16dst2  = OpAccessChain %f16ptr %ssboOUT %zero %valLoopNdx %c_i32_7\n"
		"OpStore %f16dst2 %val2_f16\n"
		"\n"
		"OpStore %insideLoopNdx %zero\n"
		"OpBranch %loopInside\n"
		"%loopInside = OpLabel\n"
		"OpLoopMerge %92 %93 None\n"
		"OpBranch %94\n"
		"%94 = OpLabel\n"
		"%valInsideLoopNdx = OpLoad %i32 %insideLoopNdx\n"
		"%96 = OpSLessThan %bool %valInsideLoopNdx %c_i32_11\n"
		"OpBranchConditional %96 %91 %92\n"
		"\n"
		"%91 = OpLabel\n"
		"\n"
		//struct {f16, v2f16[3]}
		"%Sf32src  = OpAccessChain %f32ptr %ssboIN %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %zero\n"
		"%Sval_f32 = OpLoad %f32 %Sf32src\n"
		"%Sval_f16 = OpFConvert %f16 %Sval_f32\n"
		"%Sf16dst2  = OpAccessChain %f16ptr %ssboOUT %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %zero\n"
		"OpStore %Sf16dst2 %Sval_f16\n"
		"\n"
		"%Sv2f32src0   = OpAccessChain %v2f32ptr %ssboIN %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %zero\n"
		"%Sv2f32_0     = OpLoad %v2f32 %Sv2f32src0\n"
		"%Sv2f16_0     = OpFConvert %v2f16 %Sv2f32_0\n"
		"%Sv2f16dst_0  = OpAccessChain %v2f16ptr %ssboOUT %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %zero\n"
		"OpStore %Sv2f16dst_0 %Sv2f16_0\n"
		"\n"
		"%Sv2f32src1  = OpAccessChain %v2f32ptr %ssboIN %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %c_i32_1\n"
		"%Sv2f32_1 = OpLoad %v2f32 %Sv2f32src1\n"
		"%Sv2f16_1 = OpFConvert %v2f16 %Sv2f32_1\n"
		"%Sv2f16dst_1  = OpAccessChain %v2f16ptr %ssboOUT %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %c_i32_1\n"
		"OpStore %Sv2f16dst_1 %Sv2f16_1\n"
		"\n"
		"%Sv2f32src2  = OpAccessChain %v2f32ptr %ssboIN %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %c_i32_2\n"
		"%Sv2f32_2 = OpLoad %v2f32 %Sv2f32src2\n"
		"%Sv2f16_2 = OpFConvert %v2f16 %Sv2f32_2\n"
		"%Sv2f16dst_2  = OpAccessChain %v2f16ptr %ssboOUT %zero %valLoopNdx %c_i32_5 %valInsideLoopNdx %c_i32_1 %c_i32_2\n"
		"OpStore %Sv2f16dst_2 %Sv2f16_2\n"
		"\n"

		"%v2f32src2  = OpAccessChain %v2f32ptr %ssboIN %zero %valLoopNdx %c_i32_6 %valInsideLoopNdx\n"
		"%val2_v2f32 = OpLoad %v2f32 %v2f32src2\n"
		"%val2_v2f16 = OpFConvert %v2f16 %val2_v2f32\n"
		"%v2f16dst2  = OpAccessChain %v2f16ptr %ssboOUT %zero %valLoopNdx %c_i32_6 %valInsideLoopNdx\n"
		"OpStore %v2f16dst2 %val2_v2f16\n"
		"\n"
		"%v3f32src2  = OpAccessChain %v3f32ptr %ssboIN %zero %valLoopNdx %c_i32_8 %valInsideLoopNdx\n"
		"%val2_v3f32 = OpLoad %v3f32 %v3f32src2\n"
		"%val2_v3f16 = OpFConvert %v3f16 %val2_v3f32\n"
		"%v3f16dst2  = OpAccessChain %v3f16ptr %ssboOUT %zero %valLoopNdx %c_i32_8 %valInsideLoopNdx\n"
		"OpStore %v3f16dst2 %val2_v3f16\n"
		"\n"

		//Array with 3 elements
		"%LessThan3 = OpSLessThan %bool %valInsideLoopNdx %c_i32_3\n"
		"OpSelectionMerge %BlockIf None\n"
		"OpBranchConditional %LessThan3 %LabelIf %BlockIf\n"
		"  %LabelIf = OpLabel\n"
		"  %f32src3  = OpAccessChain %f32ptr %ssboIN %zero %valLoopNdx %c_i32_4 %valInsideLoopNdx\n"
		"  %val3_f32 = OpLoad %f32 %f32src3\n"
		"  %val3_f16 = OpFConvert %f16 %val3_f32\n"
		"  %f16dst3  = OpAccessChain %f16ptr %ssboOUT %zero %valLoopNdx %c_i32_4 %valInsideLoopNdx\n"
		"  OpStore %f16dst3 %val3_f16\n"
		"\n"
		"  %v4f32src2  = OpAccessChain %v4f32ptr %ssboIN %zero %valLoopNdx %c_i32_9 %valInsideLoopNdx\n"
		"  %val2_v4f32 = OpLoad %v4f32 %v4f32src2\n"
		"  %val2_v4f16 = OpFConvert %v4f16 %val2_v4f32\n"
		"  %v4f16dst2  = OpAccessChain %v4f16ptr %ssboOUT %zero %valLoopNdx %c_i32_9 %valInsideLoopNdx\n"
		"  OpStore %v4f16dst2 %val2_v4f16\n"
		"OpBranch %BlockIf\n"
		"%BlockIf = OpLabel\n"

		"OpBranch %93\n"
		"%93 = OpLabel\n"
		"%132 = OpLoad %i32 %insideLoopNdx\n"
		"%133 = OpIAdd %i32 %132 %c_i32_1\n"
		"OpStore %insideLoopNdx %133\n"
		"OpBranch %loopInside\n"
		"\n"
		"%92 = OpLabel\n"
		"OpBranch %13\n"
		"%13 = OpLabel\n"
		"%134 = OpLoad %i32 %loopNdx\n"
		"%135 = OpIAdd %i32 %134 %c_i32_1\n"
		"OpStore %loopNdx %135\n"
		"OpBranch %loop\n"

		"%merge = OpLabel\n"
		"         OpReturnValue %param\n"
		"         OpFunctionEnd\n";

	for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
	{
		map<string, string>	specs;
		string				testName	= string(CAPABILITIES[capIdx].name);
		vector<float>		float32Data	= (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ? data32bitStd430(rnd) : data32bitStd140(rnd);
		GraphicsResources	resources;

		specs["cap"]					= "StorageUniformBufferBlock16";
		specs["indecor"]				= CAPABILITIES[capIdx].decor;
		specs["strideF16"]				= getStructShaderComponet(SHADERTEMPLATE_STRIDE16BIT_STD430);
		specs["strideF32"]				= getStructShaderComponet((VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ? SHADERTEMPLATE_STRIDE32BIT_STD430 : SHADERTEMPLATE_STRIDE32BIT_STD140);
		specs["types"]					= getStructShaderComponet(SHADERTEMPLATE_TYPES);

		fragments["capability"]			= capabilities.specialize(specs);
		fragments["decoration"]			= decoration.specialize(specs);
		fragments["pre_main"]			= preMain.specialize(specs);

		resources.inputs.push_back(std::make_pair( CAPABILITIES[capIdx].dtype, BufferSp(new Float32Buffer(float32Data))));
		resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Float16Buffer(float16Data))));
		resources.verifyIO				=  (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == CAPABILITIES[capIdx].dtype) ? graphicsCheckStruct<float, deFloat16, SHADERTEMPLATE_STRIDE32BIT_STD430, SHADERTEMPLATE_STRIDE16BIT_STD430> : graphicsCheckStruct<float, deFloat16, SHADERTEMPLATE_STRIDE32BIT_STD140, SHADERTEMPLATE_STRIDE16BIT_STD430>;

		VulkanFeatures features;
		features.ext16BitStorage = EXT16BITSTORAGEFEATURES_UNIFORM_BUFFER_BLOCK;

		createTestsForAllStages(testName, defaultColors, defaultColors, fragments, resources, extensions, testGroup, features);
	}
}

void addGraphics16bitStructMixedTypesGroup (tcu::TestCaseGroup* group)
{
	de::Random							rnd					(deStringHash(group->getName()));
	map<string, string>					fragments;
	vector<string>						extensions;
	RGBA								defaultColors[4];
	const StringTemplate				capabilities		("OpCapability StorageUniformBufferBlock16\n"
															"${cap}\n");
	vector<deInt16>						outData				(getStructSize(SHADERTEMPLATE_STRIDEMIX_STD430), 0u);

	extensions.push_back("VK_KHR_16bit_storage");
	fragments["extension"]	= "OpExtension \"SPV_KHR_16bit_storage\"\n";

	getDefaultColors(defaultColors);

	const StringTemplate				preMain				(
		"\n"//Types
		"%i16    = OpTypeInt 16 1\n"
		"%v2i16  = OpTypeVector %i16 2\n"
		"%v3i16  = OpTypeVector %i16 3\n"
		"%v4i16  = OpTypeVector %i16 4\n"
		"\n"//Consta value
		"%zero     = OpConstant %i32 0\n"
		"%c_i32_5  = OpConstant %i32 5\n"
		"%c_i32_6  = OpConstant %i32 6\n"
		"%c_i32_7  = OpConstant %i32 7\n"
		"%c_i32_8  = OpConstant %i32 8\n"
		"%c_i32_9  = OpConstant %i32 9\n"
		"%c_i32_10 = OpConstant %i32 10\n"
		"%c_i32_11 = OpConstant %i32 11\n"
		"%c_u32_7  = OpConstant %u32 7\n"
		"%c_u32_11 = OpConstant %u32 11\n"
		"\n"//Arrays & Structs
		"%v2b16NestedArr11In  = OpTypeArray %v2i16 %c_u32_11\n"
		"%b32NestedArr11In   = OpTypeArray %i32 %c_u32_11\n"
		"%sb16Arr11In         = OpTypeArray %i16 %c_u32_11\n"
		"%sb32Arr11In        = OpTypeArray %i32 %c_u32_11\n"
		"%sNestedIn          = OpTypeStruct %i16 %i32 %v2b16NestedArr11In %b32NestedArr11In\n"
		"%sNestedArr11In     = OpTypeArray %sNestedIn %c_u32_11\n"
		"%structIn           = OpTypeStruct %i16 %i32 %v2i16 %v2i32 %v3i16 %v3i32 %v4i16 %v4i32 %sNestedArr11In %sb16Arr11In %sb32Arr11In\n"
		"%structArr7In       = OpTypeArray %structIn %c_u32_7\n"
		"%v2b16NestedArr11Out = OpTypeArray %v2i16 %c_u32_11\n"
		"%b32NestedArr11Out  = OpTypeArray %i32 %c_u32_11\n"
		"%sb16Arr11Out        = OpTypeArray %i16 %c_u32_11\n"
		"%sb32Arr11Out       = OpTypeArray %i32 %c_u32_11\n"
		"%sNestedOut         = OpTypeStruct %i16 %i32 %v2b16NestedArr11Out %b32NestedArr11Out\n"
		"%sNestedArr11Out    = OpTypeArray %sNestedOut %c_u32_11\n"
		"%structOut          = OpTypeStruct %i16 %i32 %v2i16 %v2i32 %v3i16 %v3i32 %v4i16 %v4i32 %sNestedArr11Out %sb16Arr11Out %sb32Arr11Out\n"
		"%structArr7Out      = OpTypeArray %structOut %c_u32_7\n"
		"\n"//Pointers
		"%i16outPtr    = OpTypePointer Uniform %i16\n"
		"%v2i16outPtr  = OpTypePointer Uniform %v2i16\n"
		"%v3i16outPtr  = OpTypePointer Uniform %v3i16\n"
		"%v4i16outPtr  = OpTypePointer Uniform %v4i16\n"
		"%i32outPtr   = OpTypePointer Uniform %i32\n"
		"%v2i32outPtr = OpTypePointer Uniform %v2i32\n"
		"%v3i32outPtr = OpTypePointer Uniform %v3i32\n"
		"%v4i32outPtr = OpTypePointer Uniform %v4i32\n"
		"%uvec3ptr = OpTypePointer Input %v3u32\n"
		"\n"//SSBO IN
		"%SSBO_IN    = OpTypeStruct %structArr7In\n"
		"%up_SSBOIN  = OpTypePointer Uniform %SSBO_IN\n"
		"%ssboIN     = OpVariable %up_SSBOIN Uniform\n"
		"\n"//SSBO OUT
		"%SSBO_OUT   = OpTypeStruct %structArr7Out\n"
		"%up_SSBOOUT = OpTypePointer Uniform %SSBO_OUT\n"
		"%ssboOUT    = OpVariable %up_SSBOOUT Uniform\n");

		const StringTemplate			decoration			(
		"${OutOffsets}"
		"${InOffsets}"
		"\n"//SSBO IN
		"OpMemberDecorate %SSBO_IN 0 Offset 0\n"
		"OpDecorate %ssboIN DescriptorSet 0\n"
		"OpDecorate %SSBO_IN ${storage}\n"
		"OpDecorate %SSBO_OUT BufferBlock\n"
		"OpDecorate %ssboIN Binding 0\n"
		"\n"//SSBO OUT
		"OpMemberDecorate %SSBO_OUT 0 Offset 0\n"
		"OpDecorate %ssboOUT DescriptorSet 0\n"
		"OpDecorate %ssboOUT Binding 1\n");

		const StringTemplate			testFun				(
		"%test_code = OpFunction %v4f32 None %v4f32_v4f32_function\n"
		"%param     = OpFunctionParameter %v4f32\n"
		"%label     = OpLabel\n"
		"%ndxArrx   = OpVariable %fp_i32  Function\n"
		"%ndxArry   = OpVariable %fp_i32  Function\n"
		"%ndxArrz   = OpVariable %fp_i32  Function\n"
		"${xBeginLoop}"
		"\n"//strutOut.b16 = strutIn.b16
		"%inP1  = OpAccessChain %i16${inPtr} %ssboIN %zero %Valx %zero\n"
		"%inV1  = OpLoad %i16 %inP1\n"
		"%outP1 = OpAccessChain %i16outPtr %ssboOUT %zero %Valx %zero\n"
		"OpStore %outP1 %inV1\n"
		"\n"//strutOut.b32 = strutIn.b32
		"%inP2  = OpAccessChain %i32${inPtr} %ssboIN %zero %Valx %c_i32_1\n"
		"%inV2  = OpLoad %i32 %inP2\n"
		"%outP2 = OpAccessChain %i32outPtr %ssboOUT %zero %Valx %c_i32_1\n"
		"OpStore %outP2 %inV2\n"
		"\n"//strutOut.v2b16 = strutIn.v2b16
		"%inP3  = OpAccessChain %v2i16${inPtr} %ssboIN %zero %Valx %c_i32_2\n"
		"%inV3  = OpLoad %v2i16 %inP3\n"
		"%outP3 = OpAccessChain %v2i16outPtr %ssboOUT %zero %Valx %c_i32_2\n"
		"OpStore %outP3 %inV3\n"
		"\n"//strutOut.v2b32 = strutIn.v2b32
		"%inP4  = OpAccessChain %v2i32${inPtr} %ssboIN %zero %Valx %c_i32_3\n"
		"%inV4  = OpLoad %v2i32 %inP4\n"
		"%outP4 = OpAccessChain %v2i32outPtr %ssboOUT %zero %Valx %c_i32_3\n"
		"OpStore %outP4 %inV4\n"
		"\n"//strutOut.v3b16 = strutIn.v3b16
		"%inP5  = OpAccessChain %v3i16${inPtr} %ssboIN %zero %Valx %c_i32_4\n"
		"%inV5  = OpLoad %v3i16 %inP5\n"
		"%outP5 = OpAccessChain %v3i16outPtr %ssboOUT %zero %Valx %c_i32_4\n"
		"OpStore %outP5 %inV5\n"
		"\n"//strutOut.v3b32 = strutIn.v3b32
		"%inP6  = OpAccessChain %v3i32${inPtr} %ssboIN %zero %Valx %c_i32_5\n"
		"%inV6  = OpLoad %v3i32 %inP6\n"
		"%outP6 = OpAccessChain %v3i32outPtr %ssboOUT %zero %Valx %c_i32_5\n"
		"OpStore %outP6 %inV6\n"
		"\n"//strutOut.v4b16 = strutIn.v4b16
		"%inP7  = OpAccessChain %v4i16${inPtr} %ssboIN %zero %Valx %c_i32_6\n"
		"%inV7  = OpLoad %v4i16 %inP7\n"
		"%outP7 = OpAccessChain %v4i16outPtr %ssboOUT %zero %Valx %c_i32_6\n"
		"OpStore %outP7 %inV7\n"
		"\n"//strutOut.v4b32 = strutIn.v4b32
		"%inP8  = OpAccessChain %v4i32${inPtr} %ssboIN %zero %Valx %c_i32_7\n"
		"%inV8  = OpLoad %v4i32 %inP8\n"
		"%outP8 = OpAccessChain %v4i32outPtr %ssboOUT %zero %Valx %c_i32_7\n"
		"OpStore %outP8 %inV8\n"
		"${yBeginLoop}"
		"\n"//strutOut.b16[y] = strutIn.b16[y]
		"%inP9  = OpAccessChain %i16${inPtr} %ssboIN %zero %Valx %c_i32_9 %Valy\n"
		"%inV9  = OpLoad %i16 %inP9\n"
		"%outP9 = OpAccessChain %i16outPtr %ssboOUT %zero %Valx %c_i32_9 %Valy\n"
		"OpStore %outP9 %inV9\n"
		"\n"//strutOut.b32[y] = strutIn.b32[y]
		"%inP10  = OpAccessChain %i32${inPtr} %ssboIN %zero %Valx %c_i32_10 %Valy\n"
		"%inV10  = OpLoad %i32 %inP10\n"
		"%outP10 = OpAccessChain %i32outPtr %ssboOUT %zero %Valx %c_i32_10 %Valy\n"
		"OpStore %outP10 %inV10\n"
		"\n"//strutOut.strutNestedOut[y].b16 = strutIn.strutNestedIn[y].b16
		"%inP11 = OpAccessChain %i16${inPtr} %ssboIN %zero %Valx %c_i32_8 %Valy %zero\n"
		"%inV11 = OpLoad %i16 %inP11\n"
		"%outP11 = OpAccessChain %i16outPtr %ssboOUT %zero %Valx %c_i32_8 %Valy %zero\n"
		"OpStore %outP11 %inV11\n"
		"\n"//strutOut.strutNestedOut[y].b32 = strutIn.strutNestedIn[y].b32
		"%inP12 = OpAccessChain %i32${inPtr} %ssboIN %zero %Valx %c_i32_8 %Valy %c_i32_1\n"
		"%inV12 = OpLoad %i32 %inP12\n"
		"%outP12 = OpAccessChain %i32outPtr %ssboOUT %zero %Valx %c_i32_8 %Valy %c_i32_1\n"
		"OpStore %outP12 %inV12\n"
		"${zBeginLoop}"
		"\n"//strutOut.strutNestedOut[y].v2b16[valNdx] = strutIn.strutNestedIn[y].v2b16[valNdx]
		"%inP13  = OpAccessChain %v2i16${inPtr} %ssboIN %zero %Valx %c_i32_8 %Valy %c_i32_2 %Valz\n"
		"%inV13  = OpLoad %v2i16 %inP13\n"
		"%outP13 = OpAccessChain %v2i16outPtr %ssboOUT %zero %Valx %c_i32_8 %Valy %c_i32_2 %Valz\n"
		"OpStore %outP13 %inV13\n"
		"\n"//strutOut.strutNestedOut[y].b32[valNdx] = strutIn.strutNestedIn[y].b32[valNdx]
		"%inP14  = OpAccessChain %i32${inPtr} %ssboIN %zero %Valx %c_i32_8 %Valy %c_i32_3 %Valz\n"
		"%inV14  = OpLoad %i32 %inP14\n"
		"%outP14 = OpAccessChain %i32outPtr %ssboOUT %zero %Valx %c_i32_8 %Valy %c_i32_3 %Valz\n"
		"OpStore %outP14 %inV14\n"
		"${zEndLoop}"
		"${yEndLoop}"
		"${xEndLoop}"
		"\n"
		"OpBranch %ExitLabel\n"
		"%ExitLabel = OpLabel\n"
		"OpReturnValue %param\n"
		"OpFunctionEnd\n");

	for (deUint32 capIdx = 0; capIdx < DE_LENGTH_OF_ARRAY(CAPABILITIES); ++capIdx)
	{  // int
		const bool				isUniform	= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER == CAPABILITIES[capIdx].dtype;
		vector<deInt16>			inData		= isUniform ? dataMixStd140(rnd) : dataMixStd430(rnd);
		GraphicsResources		resources;
		map<string, string>		specsLoop;
		map<string, string>		specsOffset;
		map<string, string>		specs;
		string					testName	= string(CAPABILITIES[capIdx].name);

		specsLoop["exeCount"]	= "c_i32_7";
		specsLoop["loopName"]	= "x";
		specs["xBeginLoop"]		= beginLoop(specsLoop);
		specs["xEndLoop"]		= endLoop(specsLoop);

		specsLoop["exeCount"]	= "c_i32_11";
		specsLoop["loopName"]	= "y";
		specs["yBeginLoop"]		= beginLoop(specsLoop);
		specs["yEndLoop"]		= endLoop(specsLoop);

		specsLoop["exeCount"]	= "c_i32_11";
		specsLoop["loopName"]	= "z";
		specs["zBeginLoop"]		= beginLoop(specsLoop);
		specs["zEndLoop"]		= endLoop(specsLoop);

		specs["storage"]		= isUniform ? "Block" : "BufferBlock";
		specs["cap"]			= isUniform ?"OpCapability " + string( CAPABILITIES[capIdx].cap) : "";
		specs["inPtr"]			= "outPtr";
		specsOffset["InOut"]	= "In";
		specs["InOffsets"]		= StringTemplate(isUniform ? getStructShaderComponet(SHADERTEMPLATE_STRIDEMIX_STD140) : getStructShaderComponet(SHADERTEMPLATE_STRIDEMIX_STD430)).specialize(specsOffset);
		specsOffset["InOut"]	= "Out";
		specs["OutOffsets"]		= StringTemplate(getStructShaderComponet(SHADERTEMPLATE_STRIDEMIX_STD430)).specialize(specsOffset);

		fragments["capability"]			= capabilities.specialize(specs);
		fragments["decoration"]			= decoration.specialize(specs);
		fragments["pre_main"]			= preMain.specialize(specs);
		fragments["testfun"]			= testFun.specialize(specs);

		resources.verifyIO				= isUniform ? graphicsCheckStruct<deInt16, deInt16, SHADERTEMPLATE_STRIDEMIX_STD140, SHADERTEMPLATE_STRIDEMIX_STD430> : graphicsCheckStruct<deInt16, deInt16, SHADERTEMPLATE_STRIDEMIX_STD430, SHADERTEMPLATE_STRIDEMIX_STD430>;
		resources.inputs.push_back(std::make_pair( CAPABILITIES[capIdx].dtype, BufferSp(new Int16Buffer(inData))));
		resources.outputs.push_back(std::make_pair(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufferSp(new Int16Buffer(outData))));

		createTestsForAllStages(testName, defaultColors, defaultColors, fragments, resources, extensions, group, get16BitStorageFeatures(CAPABILITIES[capIdx].name));
	}
}

} // anonymous

tcu::TestCaseGroup* create16BitStorageComputeGroup (tcu::TestContext& testCtx)
{
	de::MovePtr<tcu::TestCaseGroup> group		(new tcu::TestCaseGroup(testCtx, "16bit_storage", "Compute tests for VK_KHR_16bit_storage extension"));
	addTestGroup(group.get(), "uniform_32_to_16", "32bit floats/ints to 16bit tests under capability StorageUniform{|BufferBlock}", addCompute16bitStorageUniform32To16Group);
	addTestGroup(group.get(), "uniform_16_to_32", "16bit floats/ints to 32bit tests under capability StorageUniform{|BufferBlock}", addCompute16bitStorageUniform16To32Group);
	addTestGroup(group.get(), "push_constant_16_to_32", "16bit floats/ints to 32bit tests under capability StoragePushConstant16", addCompute16bitStoragePushConstant16To32Group);
	addTestGroup(group.get(), "uniform_16struct_to_32struct", "16bit floats struct to 32bit tests under capability StorageUniform{|BufferBlock}", addCompute16bitStorageUniform16StructTo32StructGroup);
	addTestGroup(group.get(), "uniform_32struct_to_16struct", "32bit floats struct to 16bit tests under capability StorageUniform{|BufferBlock}", addCompute16bitStorageUniform32StructTo16StructGroup);
	addTestGroup(group.get(), "struct_mixed_types", "mixed type of 8bit and 32bit struct", addCompute16bitStructMixedTypesGroup);
	addTestGroup(group.get(), "uniform_16_to_16", "16bit floats/ints to 16bit tests under capability StorageUniformBufferBlock16", addCompute16bitStorageUniform16To16Group);
	addTestGroup(group.get(), "uniform_16_to_32_chainaccess", "chain access 16bit floats/ints to 32bit tests under capability StorageUniform{|BufferBlock}", addCompute16bitStorageUniform16To32ChainAccessGroup);

	return group.release();
}

tcu::TestCaseGroup* create16BitStorageGraphicsGroup (tcu::TestContext& testCtx)
{
	de::MovePtr<tcu::TestCaseGroup> group		(new tcu::TestCaseGroup(testCtx, "16bit_storage", "Graphics tests for VK_KHR_16bit_storage extension"));

	addTestGroup(group.get(), "uniform_float_32_to_16", "32-bit floats into 16-bit tests under capability StorageUniform{|BufferBlock}16", addGraphics16BitStorageUniformFloat32To16Group);
	addTestGroup(group.get(), "uniform_float_16_to_32", "16-bit floats into 32-bit testsunder capability StorageUniform{|BufferBlock}16", addGraphics16BitStorageUniformFloat16To32Group);
	addTestGroup(group.get(), "uniform_int_32_to_16", "32-bit int into 16-bit tests under capability StorageUniform{|BufferBlock}16", addGraphics16BitStorageUniformInt32To16Group);
	addTestGroup(group.get(), "uniform_int_16_to_32", "16-bit int into 32-bit tests under capability StorageUniform{|BufferBlock}16", addGraphics16BitStorageUniformInt16To32Group);
	addTestGroup(group.get(), "input_output_float_32_to_16", "32-bit floats into 16-bit tests under capability StorageInputOutput16", addGraphics16BitStorageInputOutputFloat32To16Group);
	addTestGroup(group.get(), "input_output_float_16_to_32", "16-bit floats into 32-bit tests under capability StorageInputOutput16", addGraphics16BitStorageInputOutputFloat16To32Group);
	addTestGroup(group.get(), "input_output_float_16_to_16", "16-bit floats pass-through tests under capability StorageInputOutput16", addGraphics16BitStorageInputOutputFloat16To16Group);
	addTestGroup(group.get(), "input_output_int_32_to_16", "32-bit int into 16-bit tests under capability StorageInputOutput16", addGraphics16BitStorageInputOutputInt32To16Group);
	addTestGroup(group.get(), "input_output_int_16_to_32", "16-bit int into 32-bit tests under capability StorageInputOutput16", addGraphics16BitStorageInputOutputInt16To32Group);
	addTestGroup(group.get(), "input_output_int_16_to_16", "16-bit int into 16-bit tests under capability StorageInputOutput16", addGraphics16BitStorageInputOutputInt16To16Group);
	addTestGroup(group.get(), "push_constant_float_16_to_32", "16-bit floats into 32-bit tests under capability StoragePushConstant16", addGraphics16BitStoragePushConstantFloat16To32Group);
	addTestGroup(group.get(), "push_constant_int_16_to_32", "16-bit int into 32-bit tests under capability StoragePushConstant16", addGraphics16BitStoragePushConstantInt16To32Group);
	addTestGroup(group.get(), "uniform_16struct_to_32struct", "16-bit float struct into 32-bit tests under capability StorageUniform{|BufferBlock}16", addGraphics16BitStorageUniformStructFloat16To32Group);
	addTestGroup(group.get(), "uniform_32struct_to_16struct", "32-bit float struct into 16-bit tests under capability StorageUniform{|BufferBlock}16", addGraphics16BitStorageUniformStructFloat32To16Group);
	addTestGroup(group.get(), "struct_mixed_types", "mixed type of 8bit and 32bit struct", addGraphics16bitStructMixedTypesGroup);

	return group.release();
}

} // SpirVAssembly
} // vkt
