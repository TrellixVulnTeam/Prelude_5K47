#ifndef _VKOBJUTIL_HPP
#define _VKOBJUTIL_HPP
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
 * \brief Utilities for creating commonly used Vulkan objects
 *//*--------------------------------------------------------------------*/

#include <vector>
#include "vkRef.hpp"

namespace vk
{

Move<VkPipeline> makeGraphicsPipeline(const DeviceInterface&						vk,
									  const VkDevice								device,
									  const VkPipelineLayout						pipelineLayout,
									  const VkShaderModule							vertexShaderModule,
									  const VkShaderModule							tessellationControlShaderModule,
									  const VkShaderModule							tessellationEvalShaderModule,
									  const VkShaderModule							geometryShaderModule,
									  const VkShaderModule							fragmentShaderModule,
									  const VkRenderPass							renderPass,
									  const std::vector<VkViewport>&				viewports,
									  const std::vector<VkRect2D>&					scissors,
									  const VkPrimitiveTopology						topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
									  const deUint32								subpass = 0u,
									  const deUint32								patchControlPoints = 0u,
									  const VkPipelineVertexInputStateCreateInfo*	vertexInputStateCreateInfo = DE_NULL,
									  const VkPipelineRasterizationStateCreateInfo*	rasterizationStateCreateInfo = DE_NULL,
									  const VkPipelineMultisampleStateCreateInfo*	multisampleStateCreateInfo = DE_NULL,
									  const VkPipelineDepthStencilStateCreateInfo*	depthStencilStateCreateInfo = DE_NULL,
									  const VkPipelineColorBlendStateCreateInfo*	colorBlendStateCreateInfo = DE_NULL,
									  const VkPipelineDynamicStateCreateInfo*		dynamicStateCreateInfo = DE_NULL);

Move<VkRenderPass> makeRenderPass (const DeviceInterface&		vk,
								   const VkDevice				device,
								   const VkFormat				colorFormat,
								   const VkFormat				depthStencilFormat			= VK_FORMAT_UNDEFINED,
								   const VkAttachmentLoadOp		loadOperation				= VK_ATTACHMENT_LOAD_OP_CLEAR,
								   const VkImageLayout			finalLayoutColor			= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
								   const VkImageLayout			finalLayoutDepthStencil		= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
								   const VkImageLayout			subpassLayoutColor			= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
								   const VkImageLayout			subpassLayoutDepthStencil	= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

} // vk

#endif // _VKOBJUTIL_HPP
