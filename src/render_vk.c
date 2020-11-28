#include "render.h"
#include "bsp.h"
#include "camera.h"
#include "texture.h"
#include "material.h"

#include "atto/app.h"
#include "atto/worobushek.h"

#include <stddef.h>
#include <stdlib.h>
#include <assert.h>

#define MAX_DESC_SETS 65536
#define MAX_HEAPS 1
#define HEAP_SIZE (64*1024*1024)

struct DeviceMemoryBumpAllocator {
	uint32_t type_bit;
	VkDeviceMemory devmem;
	size_t size;
	size_t offset;
};

static struct DeviceMemoryBumpAllocator createDeviceMemory(size_t size, uint32_t type_index_bits, VkMemoryPropertyFlags flags) {
	struct DeviceMemoryBumpAllocator ret = {0};

	aAppDebugPrintf("createDeviceMemory: size=%zu bits=%x flags=%x", size, type_index_bits, flags);

	// Find compatible memory type
	uint32_t type_bit = 0;
	uint32_t index = 0;
	for (uint32_t i = 0; i < a_vk.mem_props.memoryTypeCount; ++i) {
		const uint32_t bit = (1 << i);
		if (!(type_index_bits & bit))
			continue;

		if ((a_vk.mem_props.memoryTypes[i].propertyFlags & flags) == flags) {
			type_bit = bit;
			index = i;
			break;
		}
	}

	if (!type_bit)
		return ret;

	VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	alloc_info.allocationSize = size;
	alloc_info.memoryTypeIndex = index;

	const VkResult alloc_result = vkAllocateMemory(a_vk.dev, &alloc_info, NULL, &ret.devmem);

	if (alloc_result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
		return ret;

	AVK_CHECK_RESULT(alloc_result);

	ret.type_bit = type_bit;
	ret.size = size;
	ret.offset = 0;
	return ret;
}

static size_t allocateFromAllocator(struct DeviceMemoryBumpAllocator* alloc, size_t size, size_t align) {
	align = align > 0 ? align : 1;
	const size_t offset = ((alloc->offset + align - 1) / align) * align;

	if (offset + size > alloc->size)
		return -1;

	alloc->offset = offset + size;
	return offset;
}


struct AVkMaterial {
	VkShaderModule module_vertex;
	VkShaderModule module_fragment;

	VkPipelineLayout pipeline_layout;

	VkPipeline pipeline;
};

static struct {
	VkFence fence;
	VkSemaphore done;
	VkRenderPass render_pass;

	VkImageView *image_views;
	VkFramebuffer *framebuffers;

	VkFormat depth_format;
	VkImage depth_image;
	VkImageView depth_image_view;
	VkDeviceMemory depth_memory;

	VkSampler default_sampler;

	struct {
		size_t size;
		VkDeviceMemory devmem;
		VkBuffer buffer;
	} staging;

	struct DeviceMemoryBumpAllocator heaps[MAX_HEAPS];

	// buffer || image
	// 	HW heap
	// HW heaps[]
	// 	buffer || image
	// 	pool chain []
	// 		pool
	// 		  - size: 64M
	// 		  - free_offset ..

	VkDescriptorSetLayout descset_layout;
	VkDescriptorPool desc_pool;
	VkDescriptorSet desc_sets[MAX_DESC_SETS];
	int next_free_set;

	struct AVkMaterial materials[MShader_COUNT];

	// Preallocated device memory
	//struct DeviceMemoryBumpAllocator alloc;

	VkCommandPool cmdpool;
	VkCommandBuffer cmd_primary;
	VkCommandBuffer cmd_materials[MShader_COUNT];
} g;

struct AVkMemBuffer {
	VkBuffer buf;
	size_t size;
};

// TODO reverse this: give pointer to data source
static void stagingUpload(size_t size, const void *data) {
	void *ptr = NULL;
	AVK_CHECK_RESULT(vkMapMemory(a_vk.dev, g.staging.devmem, 0, size, 0, &ptr));
	memcpy(ptr, data, size);
	vkUnmapMemory(a_vk.dev, g.staging.devmem);
}

/* static void destroyBuffer(struct AVkMemBuffer buf) { */
/* 	vkDestroyBuffer(a_vk.dev, buf.buf, NULL); */
/* } */

struct AllocatedMemory {
	VkDeviceMemory devmem;
	size_t offset;
};

static struct AllocatedMemory allocateDeviceMemory(VkMemoryRequirements req) {
	for (int i = 0; i < MAX_HEAPS; ++i) {
		struct DeviceMemoryBumpAllocator *heap = g.heaps + i;
		if (!heap->devmem) {
			const size_t size = req.size > HEAP_SIZE ? req.size : HEAP_SIZE;
			*heap = createDeviceMemory(size, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			assert(heap->devmem);
			assert(heap->type_bit & req.memoryTypeBits);
		}

		if (!(heap->type_bit & req.memoryTypeBits))
			continue;

		const size_t offset = allocateFromAllocator(heap, req.size, req.alignment);
		if (offset != (size_t)-1) {
			aAppDebugPrintf("\tallocated %zu KiB\talign=%zu:\tdevmem=%p\toffset=%zu KiB\tleft=%zu KiB", req.size / 1024, req.alignment, (void*)heap->devmem, offset / 1024, (heap->size - heap->offset) / 1024);
			return (struct AllocatedMemory){
				heap->devmem,
				offset
			};
		}
	}

	return (struct AllocatedMemory){0};
}

static struct AVkMemBuffer createBufferWithData(VkBufferUsageFlags usage, int size, const void *data) {
	struct AVkMemBuffer buf;
	VkBufferCreateInfo bci = {0};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = size;
	bci.usage = usage;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	AVK_CHECK_RESULT(vkCreateBuffer(a_vk.dev, &bci, NULL, &buf.buf));

	VkMemoryRequirements memreq;
	vkGetBufferMemoryRequirements(a_vk.dev, buf.buf, &memreq);
	aAppDebugPrintf("memreq: memoryTypeBits=0x%x alignment=%zu size=%zu", memreq.memoryTypeBits, memreq.alignment, memreq.size);

	struct AllocatedMemory mem = allocateDeviceMemory(memreq);
	assert(mem.devmem);
	AVK_CHECK_RESULT(vkBindBufferMemory(a_vk.dev, buf.buf, mem.devmem, mem.offset));

	// TODO: reverse this, let the source upload
	void *ptr = NULL;
	AVK_CHECK_RESULT(vkMapMemory(a_vk.dev, mem.devmem, mem.offset, size, 0, &ptr));
	memcpy(ptr, data, size);
	vkUnmapMemory(a_vk.dev, mem.devmem);

	buf.size = size;
	return buf;
}

static VkImage createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage) {
	VkImageCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.extent.width = width;
	ici.extent.height = height;
	ici.extent.depth = 1;
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.format = format;
	ici.tiling = tiling;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ici.usage = usage;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkImage image;
	AVK_CHECK_RESULT(vkCreateImage(a_vk.dev, &ici, NULL, &image));

	return image;
}

static void createRenderPass() {
	VkAttachmentDescription attachments[2] = {{
		.format = VK_FORMAT_B8G8R8A8_SRGB,// FIXME too early a_vk.swapchain.info.imageFormat;
		.samples = VK_SAMPLE_COUNT_1_BIT,
		//attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		//attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
	{ // Depth
		.format = g.depth_format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		//attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		//attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	}};

	VkAttachmentReference color_attachment = {0};
	color_attachment.attachment = 0;
	color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_attachment = {0};
	depth_attachment.attachment = 1;
	depth_attachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subdesc = {0};
	subdesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subdesc.colorAttachmentCount = 1;
	subdesc.pColorAttachments = &color_attachment;
	subdesc.pDepthStencilAttachment = &depth_attachment;

	VkRenderPassCreateInfo rpci = {0};
	rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpci.attachmentCount = COUNTOF(attachments);
	rpci.pAttachments = attachments;
	rpci.subpassCount = 1;
	rpci.pSubpasses = &subdesc;
	AVK_CHECK_RESULT(vkCreateRenderPass(a_vk.dev, &rpci, NULL, &g.render_pass));
}

static void createDesriptorSets() {
	VkDescriptorSetLayoutBinding bindings[] = {{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = &g.default_sampler,
		}, {
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = &g.default_sampler,
		},
	};

	VkDescriptorSetLayoutCreateInfo dslci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	dslci.bindingCount = COUNTOF(bindings);
	dslci.pBindings = bindings;
	dslci.flags = 0;
	AVK_CHECK_RESULT(vkCreateDescriptorSetLayout(a_vk.dev, &dslci, NULL, &g.descset_layout));

	VkDescriptorPoolSize dps[] = {
		{
			.type = bindings[0].descriptorType,
			.descriptorCount = MAX_DESC_SETS * 2,
		}
	};
	VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	dpci.pPoolSizes = dps;
	dpci.poolSizeCount = COUNTOF(dps);
	dpci.maxSets = MAX_DESC_SETS;
	AVK_CHECK_RESULT(vkCreateDescriptorPool(a_vk.dev, &dpci, NULL, &g.desc_pool));

	VkDescriptorSetLayout layouts[MAX_DESC_SETS];
	for (int i = 0; i < MAX_DESC_SETS; ++i)
		layouts[i] = g.descset_layout;

	VkDescriptorSetAllocateInfo dsai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	dsai.descriptorPool = g.desc_pool;
	dsai.descriptorSetCount = dpci.maxSets;
	dsai.pSetLayouts = layouts;
	AVK_CHECK_RESULT(vkAllocateDescriptorSets(a_vk.dev, &dsai, g.desc_sets));
}

static void createShader(struct AVkMaterial *material, const char *vertex, const char* fragment) {
	material->module_vertex = loadShaderFromFile(vertex);
	material->module_fragment = loadShaderFromFile(fragment);

	VkPushConstantRange push_const = {0};
	push_const.offset = 0;
	push_const.size = sizeof(AMat4f);
	push_const.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkPipelineLayoutCreateInfo plci = {0};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &g.descset_layout;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &push_const;
	AVK_CHECK_RESULT(vkCreatePipelineLayout(a_vk.dev, &plci, NULL, &material->pipeline_layout));
}

static void createCommandPool() {
	VkCommandPoolCreateInfo cpci = {0};
	cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cpci.queueFamilyIndex = 0;
	cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	AVK_CHECK_RESULT(vkCreateCommandPool(a_vk.dev, &cpci, NULL, &g.cmdpool));

	VkCommandBufferAllocateInfo cbai = {0};
	cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cbai.commandBufferCount = 1;
	cbai.commandPool = g.cmdpool;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	AVK_CHECK_RESULT(vkAllocateCommandBuffers(a_vk.dev, &cbai, &g.cmd_primary));

	cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cbai.commandBufferCount = COUNTOF(g.cmd_materials);
	cbai.commandPool = g.cmdpool;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
	AVK_CHECK_RESULT(vkAllocateCommandBuffers(a_vk.dev, &cbai, g.cmd_materials));
}

static void createPipeline(struct AVkMaterial *material, const VkVertexInputAttributeDescription *attribs, size_t n_attribs) {
	VkPipelineShaderStageCreateInfo shader_stages[2] = {0};
	shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

	shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_stages[0].module = material->module_vertex;
	shader_stages[0].pName = "main";

	shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shader_stages[1].module = material->module_fragment;
	shader_stages[1].pName = "main";

	VkVertexInputBindingDescription vibd = {0};
	vibd.binding = 0;
	vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vibd.stride = sizeof(struct BSPModelVertex);

	VkPipelineVertexInputStateCreateInfo vertex_input = {0};
	vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input.vertexBindingDescriptionCount = 1;
	vertex_input.pVertexBindingDescriptions = &vibd;
	vertex_input.vertexAttributeDescriptionCount = n_attribs;
	vertex_input.pVertexAttributeDescriptions = attribs;

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {0};
	input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport = {
		.x = 0, .y = 0,
		.width = a_app_state->width, .height = a_app_state->height,
		.minDepth = 0.f, .maxDepth = 1.f
	};
	VkRect2D scissor = {
		.offset = {0},
		.extent = {a_app_state->width, a_app_state->height}
	};
	VkPipelineViewportStateCreateInfo viewport_state = {0};
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.viewportCount = 1;
	viewport_state.scissorCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo raster_state = {0};
	raster_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster_state.polygonMode = VK_POLYGON_MODE_FILL;
	raster_state.cullMode = VK_CULL_MODE_BACK_BIT;
	raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster_state.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo multi_state = {0};
	multi_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multi_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blend_attachment = {0};
	blend_attachment.blendEnable = VK_FALSE;
	blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo color_blend = {0};
	color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color_blend.attachmentCount = 1;
	color_blend.pAttachments = &blend_attachment;

	VkPipelineDepthStencilStateCreateInfo depth = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	depth.depthTestEnable = VK_TRUE;
	depth.depthWriteEnable = VK_TRUE;
	depth.depthCompareOp = VK_COMPARE_OP_LESS;

	VkGraphicsPipelineCreateInfo gpci = {0};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = COUNTOF(shader_stages);
	gpci.pStages = shader_stages;
	gpci.pVertexInputState = &vertex_input;
	gpci.pInputAssemblyState = &input_assembly;
	gpci.pViewportState = &viewport_state;
	gpci.pRasterizationState = &raster_state;
	gpci.pMultisampleState = &multi_state;
	gpci.pColorBlendState = &color_blend;
	gpci.pDepthStencilState = &depth;
	gpci.layout = material->pipeline_layout;
	gpci.renderPass = g.render_pass;
	gpci.subpass = 0;
	AVK_CHECK_RESULT(vkCreateGraphicsPipelines(a_vk.dev, NULL, 1, &gpci, NULL, &material->pipeline));
}

static void createPipelines() {
	{
		VkVertexInputAttributeDescription attribs[] = {
			{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(struct BSPModelVertex, vertex)},
			//{.binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(struct BSPModelVertex, lightmap_uv)},
			//{.binding = 0, .location = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(struct BSPModelVertex, tex_uv)},
			//{.binding = 0, .location = 3, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(struct BSPModelVertex, average_color)},
		};
		createPipeline(&g.materials[MShader_Unknown], attribs, COUNTOF(attribs));
	}

	{
		VkVertexInputAttributeDescription attribs[] = {
			{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(struct BSPModelVertex, vertex)},
			{.binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(struct BSPModelVertex, lightmap_uv)},
			{.binding = 0, .location = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(struct BSPModelVertex, tex_uv)},
			//{.binding = 0, .location = 3, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(struct BSPModelVertex, average_color)},
		};
		createPipeline(&g.materials[MShader_LightmappedGeneric], attribs, COUNTOF(attribs));
	}
}

static void createShaders() {
	createShader(&g.materials[MShader_Unknown], "unknown.vert.spv", "unknown.frag.spv");
	createShader(&g.materials[MShader_LightmappedGeneric], "lightmapped.vert.spv", "lightmapped.frag.spv");
}

//#define renderTextureInit(texture_ptr) do { (texture_ptr)->gl_name = -1; } while (0)
void renderTextureUpload(RTexture *texture, RTextureUploadParams params) {
	(void)(texture); (void)(params);

	if (params.mip_level > 0)
		return;

	if (params.format != RTexFormat_RGB565)
		return;

	// 1. Create VkImage w/ usage = DST|SAMPLED, layout=UNDEFINED
	// 2. Alloc mem for VkImage and bind it (DEV_LOCAL)
	const VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VkImage image = createImage(params.width, params.height, VK_FORMAT_R5G6B5_UNORM_PACK16, tiling, usage);
	VkDeviceMemory devmem;

	VkMemoryRequirements memreq;
	vkGetImageMemoryRequirements(a_vk.dev, image, &memreq);

	VkMemoryAllocateInfo mai={.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	mai.allocationSize = memreq.size;
	mai.memoryTypeIndex = aVkFindMemoryWithType(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	AVK_CHECK_RESULT(vkAllocateMemory(a_vk.dev, &mai, NULL, &devmem));
	AVK_CHECK_RESULT(vkBindImageMemory(a_vk.dev, image, devmem, 0));

	const int bytes_per_pixel = 2;
	int size = params.width * params.height * bytes_per_pixel;

	// 3. Create/get staging buffer
	// 4. memcpy image data to mapped staging buffer
	stagingUpload(size, params.pixels);

	// 5. Create/get cmdbuf for transitions
	// 	5.1 upload buf -> image:layout:DST
	// 		5.1.1 transitionToLayout(UNDEFINED -> DST)
	// 		5.1.2 copyBufferToImage
	// 	5.2 image:layout:DST -> image:layout:SAMPLED
	// 		5.2.1 transitionToLayout(DST -> SHADER_READ_ONLY)

	{
		VkCommandBufferBeginInfo beginfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		beginfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		AVK_CHECK_RESULT(vkBeginCommandBuffer(g.cmd_primary, &beginfo));

		// 5.1.1
		VkImageMemoryBarrier image_barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		image_barrier.image = image;
		image_barrier.srcAccessMask = 0;
		image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_barrier.subresourceRange = (VkImageSubresourceRange){
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};
		vkCmdPipelineBarrier(g.cmd_primary,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, NULL, 0, NULL, 1, &image_barrier);

		// 5.1.2
		VkBufferImageCopy region = {0};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource = (VkImageSubresourceLayers){
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};
		region.imageExtent = (VkExtent3D){
			.width = params.width,
			.height = params.height,
			.depth = 1,
		};
		vkCmdCopyBufferToImage(g.cmd_primary, g.staging.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		// 5.2.1
		image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_barrier.subresourceRange = (VkImageSubresourceRange){
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};
		vkCmdPipelineBarrier(g.cmd_primary,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, NULL, 0, NULL, 1, &image_barrier);

		AVK_CHECK_RESULT(vkEndCommandBuffer(g.cmd_primary));
	}

	{
		VkSubmitInfo subinfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
		subinfo.commandBufferCount = 1;
		subinfo.pCommandBuffers = &g.cmd_primary;
		AVK_CHECK_RESULT(vkQueueSubmit(a_vk.main_queue, 1, &subinfo, NULL));
		AVK_CHECK_RESULT(vkQueueWaitIdle(a_vk.main_queue));
	}

	VkImageView imview;
	VkImageViewCreateInfo ivci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
	ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ivci.format = VK_FORMAT_R5G6B5_UNORM_PACK16;
	ivci.image = image;
	ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	ivci.subresourceRange.levelCount = 1;
	ivci.subresourceRange.layerCount = 1;
	AVK_CHECK_RESULT(vkCreateImageView(a_vk.dev, &ivci, NULL, &imview));

	texture->vkDevMem = devmem;
	texture->vkImage = image;
	texture->vkImageView = imview;
	texture->width = params.width;
	texture->height = params.height;
	texture->format = params.format;
}

static void createStagingBuffer() {
#define STAGING_SIZE (16384*1024)
	VkBufferCreateInfo bci = {0};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = STAGING_SIZE;
	bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	AVK_CHECK_RESULT(vkCreateBuffer(a_vk.dev, &bci, NULL, &g.staging.buffer));

	VkMemoryRequirements memreq;
	vkGetBufferMemoryRequirements(a_vk.dev, g.staging.buffer, &memreq);
	aAppDebugPrintf("memreq: memoryTypeBits=0x%x alignment=%zu size=%zu", memreq.memoryTypeBits, memreq.alignment, memreq.size);

	VkMemoryAllocateInfo mai={0};
	mai.allocationSize = memreq.size;
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.memoryTypeIndex = aVkFindMemoryWithType(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	AVK_CHECK_RESULT(vkAllocateMemory(a_vk.dev, &mai, NULL, &g.staging.devmem));
	AVK_CHECK_RESULT(vkBindBufferMemory(a_vk.dev, g.staging.buffer, g.staging.devmem, 0));
}

int renderInit() {
	VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	sci.magFilter = VK_FILTER_LINEAR;
	sci.minFilter = VK_FILTER_NEAREST;
	sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sci.anisotropyEnable = VK_FALSE;
	sci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	sci.unnormalizedCoordinates = VK_FALSE;
	sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	AVK_CHECK_RESULT(vkCreateSampler(a_vk.dev, &sci, NULL, &g.default_sampler));

	createStagingBuffer();
	createDesriptorSets();
	createShaders();

	createCommandPool();

	VkFenceCreateInfo fci = {0};
	fci.flags = 0;
	fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	AVK_CHECK_RESULT(vkCreateFence(a_vk.dev, &fci, NULL, &g.fence));

	g.done = aVkCreateSemaphore();

	return 1;
}

static VkFormat findSupportedImageFormat(const VkFormat *candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
	for (int i = 0; candidates[i] != VK_FORMAT_UNDEFINED; ++i) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(a_vk.phys_dev, candidates[i], &props);
		VkFormatFeatureFlags props_format;
		switch (tiling) {
			case VK_IMAGE_TILING_OPTIMAL:
				props_format = props.optimalTilingFeatures; break;
			case VK_IMAGE_TILING_LINEAR:
				props_format = props.linearTilingFeatures; break;
			default:
				return VK_FORMAT_UNDEFINED;
		}
		if ((props_format & features) == features)
			return candidates[i];
	}

	return VK_FORMAT_UNDEFINED;
}

static const VkFormat depth_formats[] = {
	VK_FORMAT_D32_SFLOAT,
	VK_FORMAT_D32_SFLOAT_S8_UINT,
	VK_FORMAT_D24_UNORM_S8_UINT,
	VK_FORMAT_D16_UNORM,
	VK_FORMAT_D16_UNORM_S8_UINT,
	VK_FORMAT_UNDEFINED
};

static void createDepthImage(int w, int h) {
	const VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	const VkFormatFeatureFlags feature = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	const VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	g.depth_format = findSupportedImageFormat(depth_formats, tiling, feature);
	g.depth_image = createImage(w, h, g.depth_format, tiling, usage);

	VkMemoryRequirements memreq;
	vkGetImageMemoryRequirements(a_vk.dev, g.depth_image, &memreq);

	VkMemoryAllocateInfo mai={.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	mai.allocationSize = memreq.size;
	mai.memoryTypeIndex = aVkFindMemoryWithType(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	AVK_CHECK_RESULT(vkAllocateMemory(a_vk.dev, &mai, NULL, &g.depth_memory));
	AVK_CHECK_RESULT(vkBindImageMemory(a_vk.dev, g.depth_image, g.depth_memory, 0));

	VkImageViewCreateInfo ivci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
	ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ivci.format = g.depth_format;
	ivci.image = g.depth_image;
	ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	ivci.subresourceRange.levelCount = 1;
	ivci.subresourceRange.layerCount = 1;
	AVK_CHECK_RESULT(vkCreateImageView(a_vk.dev, &ivci, NULL, &g.depth_image_view));
}

static void destroyDepthImage() {
	vkDestroyImageView(a_vk.dev, g.depth_image_view, NULL);
	vkDestroyImage(a_vk.dev, g.depth_image, NULL);
	vkFreeMemory(a_vk.dev, g.depth_memory, NULL);
}

void renderVkSwapchainCreated(int w, int h) {
	createDepthImage(w, h);

	// Needs to know depth and color formats
	if (!g.render_pass)
		createRenderPass();

	const uint32_t num_images = a_vk.swapchain.num_images;
	// FIXME malloc ;_;
	g.image_views = malloc(num_images * sizeof(VkImageView));
	g.framebuffers = malloc(num_images * sizeof(VkFramebuffer));
	for (uint32_t i = 0; i < num_images; ++i) {
		VkImageViewCreateInfo ivci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivci.format = a_vk.swapchain.info.imageFormat;
		ivci.image = a_vk.swapchain.images[i];
		ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ivci.subresourceRange.levelCount = 1;
		ivci.subresourceRange.layerCount = 1;
		AVK_CHECK_RESULT(vkCreateImageView(a_vk.dev, &ivci, NULL, g.image_views + i));

		const VkImageView attachments[2] = {
			g.image_views[i],
			g.depth_image_view
		};
		VkFramebufferCreateInfo fbci = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
		fbci.renderPass = g.render_pass;
		fbci.attachmentCount = COUNTOF(attachments);
		fbci.pAttachments = attachments;
		fbci.width = w;
		fbci.height = h;
		fbci.layers = 1;
		AVK_CHECK_RESULT(vkCreateFramebuffer(a_vk.dev, &fbci, NULL, g.framebuffers + i));
	}

	createPipelines();
}

void renderVkSwapchainDestroy() {
	vkDeviceWaitIdle(a_vk.dev);

	for (int i = 0; i < (int)COUNTOF(g.materials); ++i) {
		if (!g.materials[i].pipeline)
			continue;

		vkDestroyPipeline(a_vk.dev, g.materials[i].pipeline, NULL);
	}

	for (uint32_t i = 0; i < a_vk.swapchain.num_images; ++i) {
		vkDestroyFramebuffer(a_vk.dev, g.framebuffers[i], NULL);
		vkDestroyImageView(a_vk.dev, g.image_views[i], NULL);
	}

	free(g.image_views);
	free(g.framebuffers);

	destroyDepthImage();
}

void renderBufferCreate(RBuffer *buffer, RBufferType type, int size, const void *data) {
	VkBufferUsageFlags usage;
	switch (type) {
		case RBufferType_Index:
			usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			break;
		case RBufferType_Vertex:
			usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			break;
		default:
			return;
	}
	struct AVkMemBuffer buf = createBufferWithData(usage, size, data);

	buffer->vkBuf = buf.buf;
}

void renderBegin() {
	VkCommandBufferInheritanceInfo inherit = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
	inherit.framebuffer = g.framebuffers[a_vk.swapchain.current_frame_image_index];
	inherit.renderPass = g.render_pass;
	inherit.subpass = 0;
	VkCommandBufferBeginInfo beginfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	beginfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
	beginfo.pInheritanceInfo = &inherit;

	for (int i = 0; i < (int)COUNTOF(g.cmd_materials); ++i) {
		VkCommandBuffer cb = g.cmd_materials[i];
		AVK_CHECK_RESULT(vkBeginCommandBuffer(cb, &beginfo));

		struct AVkMaterial *m = g.materials + i;
		if (m->pipeline)
			vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m->pipeline);
	}

	g.next_free_set = 0;
}

void renderModelDraw(const RDrawParams *params, const struct BSPModel *model) {
	if (!model->detailed.draws_count) return;

	// Vulkan has Y pointing down, and z should end up in (0, 1)
	const struct AMat4f vk_fixup = {
		aVec4f(1, 0, 0, 0),
		aVec4f(0, -1, 0, 0),
		aVec4f(0, 0, .5, 0),
		aVec4f(0, 0, .5, 1)
	};
	const struct AMat4f mvp = aMat4fMul(vk_fixup, aMat4fMul(params->camera->view_projection,
			aMat4fTranslation(params->translation)));

	int seen_material[MShader_COUNT] = {0};

	for (int i = 0; i < model->detailed.draws_count; ++i) {
		const struct BSPDraw* draw = model->detailed.draws + i;

		const int mat_index = draw->material->shader;
		VkCommandBuffer cb = g.cmd_materials[mat_index];
		struct AVkMaterial *m = g.materials + mat_index;

		if (!m->pipeline)
			continue;

		// FIXME why
		if (mat_index == MShader_LightmappedGeneric && !draw->material->base_texture.texture)
			continue;

		if (!seen_material[mat_index]) {
			if (g.next_free_set >= MAX_DESC_SETS) {
				aAppDebugPrintf("FIXME ran out of descriptor sets");
				break;
			}

			vkCmdPushConstants(cb, m->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);
			seen_material[mat_index] = 1;
		}

		VkDescriptorSet set = g.desc_sets[g.next_free_set];
		g.next_free_set++;

		// FIXME proper binding of textures to slots depending on material
		if (mat_index != MShader_Unknown) {
			VkDescriptorImageInfo dii_tex = {
				.imageView = draw->material->base_texture.texture->texture.vkImageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkDescriptorImageInfo dii_lm = {
				.imageView = model->lightmap.vkImageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			// TODO: sort by textures to decrease amount of descriptor updates
			VkWriteDescriptorSet wds[] = {
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = set,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = &dii_tex,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = set,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = &dii_lm,
				},
			};
			vkUpdateDescriptorSets(a_vk.dev, COUNTOF(wds), wds, 0, NULL);

			vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m->pipeline_layout, 0, 1, &set, 0, NULL);
		}

		const VkDeviceSize offset = draw->vbo_offset;
		vkCmdBindVertexBuffers(cb, 0, 1, (VkBuffer*)&model->vbo.vkBuf, &offset);
		vkCmdBindIndexBuffer(cb, (VkBuffer)model->ibo.vkBuf, 0, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(cb, draw->count, 1, draw->start, 0, 0);
	}
}

void renderEnd(const struct Camera *camera) {
	(void)(camera);

	for (int i = 0; i < (int)COUNTOF(g.cmd_materials); ++i) {
		AVK_CHECK_RESULT(vkEndCommandBuffer(g.cmd_materials[i]));
	}

	VkCommandBufferBeginInfo beginfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	beginfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	AVK_CHECK_RESULT(vkBeginCommandBuffer(g.cmd_primary, &beginfo));

	VkClearValue clear_value[2] = {
		{.color = {{0., 0., 0., 0.}}},
		{.depthStencil = {1., 0.}}
	};
	VkRenderPassBeginInfo rpbi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
	rpbi.renderPass = g.render_pass;
	rpbi.framebuffer = g.framebuffers[a_vk.swapchain.current_frame_image_index];
	rpbi.renderArea.offset.x = rpbi.renderArea.offset.y = 0;
	rpbi.renderArea.extent.width = a_app_state->width;
	rpbi.renderArea.extent.height = a_app_state->height;
	rpbi.clearValueCount = COUNTOF(clear_value);
	rpbi.pClearValues = clear_value;
	vkCmdBeginRenderPass(g.cmd_primary, &rpbi, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
	vkCmdExecuteCommands(g.cmd_primary, COUNTOF(g.cmd_materials), g.cmd_materials);
	vkCmdEndRenderPass(g.cmd_primary);

	AVK_CHECK_RESULT(vkEndCommandBuffer(g.cmd_primary));

	VkPipelineStageFlags stageflags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo subinfo = {0};
	subinfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	subinfo.pNext = NULL;
	subinfo.commandBufferCount = 1;
	subinfo.pCommandBuffers = &g.cmd_primary;
	subinfo.waitSemaphoreCount = 1;
	subinfo.pWaitSemaphores = &a_vk.swapchain.image_available;
	subinfo.signalSemaphoreCount = 1;
	subinfo.pSignalSemaphores = &g.done,
	subinfo.pWaitDstStageMask = &stageflags;
	AVK_CHECK_RESULT(vkQueueSubmit(a_vk.main_queue, 1, &subinfo, g.fence));

	VkPresentInfoKHR presinfo = {0};
	presinfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presinfo.pSwapchains = &a_vk.swapchain.handle;
	presinfo.pImageIndices = &a_vk.swapchain.current_frame_image_index;
	presinfo.swapchainCount = 1;
	presinfo.pWaitSemaphores = &g.done;
	presinfo.waitSemaphoreCount = 1;
	AVK_CHECK_RESULT(vkQueuePresentKHR(a_vk.main_queue, &presinfo));

	// FIXME bad sync
	AVK_CHECK_RESULT(vkWaitForFences(a_vk.dev, 1, &g.fence, VK_TRUE, INT64_MAX));
	AVK_CHECK_RESULT(vkResetFences(a_vk.dev, 1, &g.fence));
}

void renderDestroy() {
	vkDeviceWaitIdle(a_vk.dev);

	vkDestroyBuffer(a_vk.dev, g.staging.buffer, NULL);
	vkFreeMemory(a_vk.dev, g.staging.devmem, NULL);

	// TODO do we need this?
	vkFreeCommandBuffers(a_vk.dev, g.cmdpool, 1, &g.cmd_primary);
	vkFreeCommandBuffers(a_vk.dev, g.cmdpool, COUNTOF(g.cmd_materials), g.cmd_materials);
	vkDestroyCommandPool(a_vk.dev, g.cmdpool, NULL);

	for (int i = 0; i < (int)COUNTOF(g.materials); ++i) {
		struct AVkMaterial *m = g.materials + i;
		if (!m->pipeline_layout)
			continue;

		vkDestroyPipelineLayout(a_vk.dev, m->pipeline_layout, NULL);
		vkDestroyShaderModule(a_vk.dev, m->module_fragment, NULL);
		vkDestroyShaderModule(a_vk.dev, m->module_vertex, NULL);
	}

	vkDestroyDescriptorSetLayout(a_vk.dev, g.descset_layout, NULL);

	vkDestroyRenderPass(a_vk.dev, g.render_pass, NULL);
	aVkDestroySemaphore(g.done);
	vkDestroyFence(a_vk.dev, g.fence, NULL);
}
