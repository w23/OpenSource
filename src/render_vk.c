#include "render.h"
#include "bsp.h"
#include "camera.h"
#include "texture.h"
#include "material.h"

#include "atto/app.h"

#define RTX 1

#if RTX
#define ATTO_VK_INSTANCE_EXTENSIONS \
		VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, \

#define ATTO_VK_DEVICE_EXTENSIONS \
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, \
		VK_KHR_RAY_QUERY_EXTENSION_NAME, \
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, \

		//VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, \

#define AVK_VK_VERSION VK_MAKE_VERSION(1, 2, 0)
#else
#define AVK_VK_VERSION VK_MAKE_VERSION(1, 1, 0)
#endif

#define ATTO_VK_IMPLEMENT
#include "atto/worobushek.h"

#include <stddef.h>
#include <stdlib.h>
#undef NDEBUG
#include <assert.h>

#define MAX_DESC_SETS 1024 // TODO how many textures can we have?
#define MAX_HEAPS 16
#define MAX_LEVELS 128
#define HEAP_SIZE (64*1024*1024)
#define IMAGE_HEAP_SIZE (256*1024*1024)
#define STAGING_SIZE (16*1024*1024)

struct DeviceMemoryBumpAllocator {
	uint32_t type_bit;
	VkMemoryPropertyFlags props;
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

		const uint32_t heap_index = a_vk.mem_props.memoryTypes[i].heapIndex;
		if (a_vk.mem_props.memoryHeaps[heap_index].size < size)
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
	ret.props = flags;
	ret.size = size;
	ret.offset = 0;
	return ret;
}

static size_t allocateFromAllocator(struct DeviceMemoryBumpAllocator* alloc, size_t size, size_t align) {
	align = align > 0 ? align : 1;
	const size_t offset = ((alloc->offset + align - 1) / align) * align;

	if (offset + size > alloc->size)
		return (size_t)-1;

	alloc->offset = offset + size;
	return offset;
}

struct AVkMaterial {
	VkShaderModule module_vertex;
	VkShaderModule module_fragment;

	VkPipelineLayout pipeline_layout;

	VkPipeline pipeline;
};

typedef enum {
	DMC_Image,
	DMC_Buffer,
	DMC_COUNT
} DeviceMemoryClass;

struct GiantBuffer {
	uint32_t size;
	VkDeviceMemory devmem;
	VkBuffer buffer;
	void *mapped;
	uint32_t offset;
};

static struct GiantBuffer createGiantBuffer(uint32_t size, VkBufferUsageFlags usage) {
	struct GiantBuffer gb;
	gb.size = size;
	gb.offset = 0;

	VkBufferCreateInfo bci = {0};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = gb.size;
	bci.usage = usage;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	AVK_CHECK_RESULT(vkCreateBuffer(a_vk.dev, &bci, NULL, &gb.buffer));

	VkMemoryRequirements memreq;
	vkGetBufferMemoryRequirements(a_vk.dev, gb.buffer, &memreq);
	aAppDebugPrintf("memreq: memoryTypeBits=0x%x alignment=%zu size=%zu", memreq.memoryTypeBits, memreq.alignment, memreq.size);

	VkMemoryAllocateInfo mai={0};
	mai.allocationSize = memreq.size;
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.memoryTypeIndex = aVkFindMemoryWithType(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	AVK_CHECK_RESULT(vkAllocateMemory(a_vk.dev, &mai, NULL, &gb.devmem));
	AVK_CHECK_RESULT(vkBindBufferMemory(a_vk.dev, gb.buffer, gb.devmem, 0));

	AVK_CHECK_RESULT(vkMapMemory(a_vk.dev, gb.devmem, 0, bci.size, 0, &gb.mapped));

	return gb;
}

struct DescriptorKasha {
		VkDescriptorSetLayout layout;
		int count;
		int next_free;
		VkDescriptorSet descriptors[];
};

enum {
		Descriptors_Global,
		Descriptors_Lightmaps,
		Descriptors_Textures,
		Descriptors_COUNT
};

#if RTX
struct Buffer {
	VkBuffer buffer;
	VkDeviceMemory devmem;
	void *data;
	size_t size;
};

struct Accel {
	struct Buffer buffer;
	VkAccelerationStructureKHR handle;
};
#endif

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
		void *mapped;
	} staging;

	struct {
		struct DeviceMemoryBumpAllocator heaps[MAX_HEAPS];
	} class[DMC_COUNT];

	struct GiantBuffer buffers[MAX_HEAPS];

	VkBuffer ubo;

	VkDescriptorPool descriptor_pool;
	struct DescriptorKasha *descriptors[Descriptors_COUNT];

	struct AVkMaterial materials[MShader_COUNT];

	VkCommandPool cmdpool;
	VkCommandBuffer cmd_primary;
	VkCommandBuffer cmd_materials[MShader_COUNT];

#if RTX
	struct {
			struct Accel tlas;
			// VkPipeline ray_pipeline;

			//struct {
			//	VkShaderModule raygen;
			//	VkShaderModule raymiss;
			//	VkShaderModule rayclosesthit;
			//} modules;

			//VkDescriptorSetLayout desc_layout;
			//VkDescriptorPool descriptor_pool;
			//VkDescriptorSet desc_set;

			//struct Buffer sbt_buf, aabb_buf, tri_buf, tl_geom_buffer;
	} rtx;
#endif
} g;

enum {
		DescriptorIndex_Ubo = 0,
#if RTX
		DescriptorIndex_Tlas,
#endif
		DescriptorIndex_TexturesStart,
};

enum {
		DescriptorBinding_Ubo = 0,
		DescriptorBinding_Lightmap,
		DescriptorBinding_BaseMaterialTexture,
};

struct Ubo {
		AMat4f model_view;
		AMat4f projection;
};

struct AllocatedMemory {
	VkDeviceMemory devmem;
	size_t offset;
};

static struct AllocatedMemory allocateDeviceMemory(DeviceMemoryClass class, VkMemoryRequirements req, VkMemoryPropertyFlags props) {
	struct DeviceMemoryBumpAllocator *heaps = g.class[class].heaps;
	const size_t new_heap_size = class == DMC_Image ? IMAGE_HEAP_SIZE : HEAP_SIZE;

	for (int i = 0; i < MAX_HEAPS; ++i) {
		struct DeviceMemoryBumpAllocator *heap = heaps + i;

		if (!heap->devmem) {
			const size_t size = req.size > new_heap_size ? req.size : new_heap_size;
			*heap = createDeviceMemory(size, req.memoryTypeBits, props);
			assert(heap->devmem);
			assert(heap->type_bit & req.memoryTypeBits);
			assert((heap->props & props) == props);
		}

		if (!(heap->type_bit & req.memoryTypeBits) && ((heap->props & props) == props))
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

static struct DescriptorKasha* createDescriptorKasha(const VkDescriptorSetLayoutBinding *bindings, uint32_t num_bindings, uint32_t num_descriptors) {
	struct DescriptorKasha* kasha = malloc(sizeof(struct DescriptorKasha) + sizeof(VkDescriptorSet) * num_descriptors);
	kasha->count = (int)num_descriptors;
	kasha->next_free = 0;

	VkDescriptorSetLayoutCreateInfo dslci = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = num_bindings,
		.pBindings = bindings,
	};
	AVK_CHECK_RESULT(vkCreateDescriptorSetLayout(a_vk.dev, &dslci, NULL, &kasha->layout));

	VkDescriptorSetLayout* tmp_layouts = malloc(sizeof(VkDescriptorSetLayout) * num_descriptors);
	for (int i = 0; i < (int)num_descriptors; ++i)
			tmp_layouts[i] = kasha->layout;

	VkDescriptorSetAllocateInfo dsai = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = g.descriptor_pool,
		.descriptorSetCount = num_descriptors,
		.pSetLayouts = tmp_layouts,
	};
	AVK_CHECK_RESULT(vkAllocateDescriptorSets(a_vk.dev, &dsai, kasha->descriptors));

	free(tmp_layouts);
	return kasha;
}

static void createDesriptorSets() {
	VkDescriptorPoolSize dps[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_DESC_SETS + MAX_LEVELS,
		}, {
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
#if RTX
		}, {
			.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			.descriptorCount = 1,
#endif
		},
	};
	VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	dpci.pPoolSizes = dps;
	dpci.poolSizeCount = COUNTOF(dps);
	dpci.maxSets = MAX_DESC_SETS + MAX_LEVELS + 1;
	AVK_CHECK_RESULT(vkCreateDescriptorPool(a_vk.dev, &dpci, NULL, &g.descriptor_pool));

	{
			VkDescriptorSetLayoutBinding bindings[] = { {
					.binding = DescriptorBinding_Ubo,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		#if RTX
				}, {
					.binding = 1, // TLAS for rays
					.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		#endif
		} };

			g.descriptors[Descriptors_Global] = createDescriptorKasha(bindings, COUNTOF(bindings), 1);
	}

	{
			VkDescriptorSetLayoutBinding bindings[] = { {
					.binding = DescriptorBinding_Lightmap,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
					.pImmutableSamplers = &g.default_sampler,
			}};

			g.descriptors[Descriptors_Lightmaps] = createDescriptorKasha(bindings, COUNTOF(bindings), MAX_LEVELS);
	}

	{
			VkDescriptorSetLayoutBinding bindings[] = { {
					.binding = DescriptorBinding_BaseMaterialTexture,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
					.pImmutableSamplers = &g.default_sampler,
			}};

			g.descriptors[Descriptors_Textures] = createDescriptorKasha(bindings, COUNTOF(bindings), MAX_DESC_SETS);
	}
}

static void createShader(struct AVkMaterial *material, const char *vertex, const char* fragment) {
	material->module_vertex = loadShaderFromFile(vertex);
	material->module_fragment = loadShaderFromFile(fragment);

	VkPushConstantRange push_const = {0};
	push_const.offset = 0;
	push_const.size = sizeof(AMat4f);
	push_const.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayout descriptor_layouts[] = {
			g.descriptors[Descriptors_Global]->layout,
			g.descriptors[Descriptors_Lightmaps]->layout,
			g.descriptors[Descriptors_Textures]->layout,
	};

	VkPipelineLayoutCreateInfo plci = {0};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = COUNTOF(descriptor_layouts);
	plci.pSetLayouts = descriptor_layouts;
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

static void createPipeline(struct AVkMaterial *material, const VkVertexInputAttributeDescription *attribs, uint32_t n_attribs) {
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
		.width = (float)a_app_state->width, .height = (float)a_app_state->height,
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
		/*
	{
		VkVertexInputAttributeDescription attribs[] = {
			{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(struct BSPModelVertex, vertex)},
			//{.binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(struct BSPModelVertex, lightmap_uv)},
			//{.binding = 0, .location = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(struct BSPModelVertex, tex_uv)},
			//{.binding = 0, .location = 3, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(struct BSPModelVertex, average_color)},
		};
		createPipeline(&g.materials[MShader_Unknown], attribs, COUNTOF(attribs));
	}
	*/

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

/*
static void rayCreateLayouts() {
  VkDescriptorSetLayoutBinding bindings[] = {{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		}, {
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		}, {
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
		},
	};

	VkDescriptorSetLayoutCreateInfo dslci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = COUNTOF(bindings), .pBindings = bindings, };

	AVK_CHECK_RESULT(vkCreateDescriptorSetLayout(a_vk.dev, &dslci, NULL, &g.rtx.desc_layout));

	VkPushConstantRange push_const = {0};
	push_const.offset = 0;
	push_const.size = sizeof(float);
	push_const.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	VkPipelineLayoutCreateInfo plci = {0};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &g.rtx.desc_layout;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &push_const;
	AVK_CHECK_RESULT(vkCreatePipelineLayout(a_vk.dev, &plci, NULL, &g.rtx.pipeline_layout));

	VkDescriptorPoolSize pools[] = {
			{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1},
			{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1},
			{.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, .descriptorCount = 1},
	};

	VkDescriptorPoolCreateInfo dpci = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1, .poolSizeCount = COUNTOF(pools), .pPoolSizes = pools,
	};
	AVK_CHECK_RESULT(vkCreateDescriptorPool(a_vk.dev, &dpci, NULL, &g.rtx.descriptor_pool));

	VkDescriptorSetAllocateInfo dsai = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = g.rtx.descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &g.rtx.desc_layout,
	};
	AVK_CHECK_RESULT(vkAllocateDescriptorSets(a_vk.dev, &dsai, &g.rtx.desc_set));

	g.rtx.modules.raygen = loadShaderFromFile("ray.rgen.spv");
	g.rtx.modules.raymiss = loadShaderFromFile("ray.rmiss.spv");
	g.rtx.modules.rayclosesthit = loadShaderFromFile("ray.rchit.spv");
}
*/

RStagingMemory renderGetStagingBuffer(size_t size) {
	// TODO staging allocation strategy?
	// - multiple requests
	// - buffer is too small?
	// - multiple threads?
	assert(size <= g.staging.size);
	return (RStagingMemory){
		g.staging.mapped,
		size
	};
}

void renderFreeStagingBuffer(RStagingMemory mem) {
	(void)mem;
	// TODO handle multiple requests
}

RTexture renderTextureCreateAndUpload(RTextureUploadParams params) {
	VkFormat format;
	switch (params.format) {
		case RTexFormat_RGB565: format = VK_FORMAT_R5G6B5_UNORM_PACK16; break;
		case RTexFormat_Compressed_DXT1: format = VK_FORMAT_BC1_RGB_UNORM_BLOCK; break;
		case RTexFormat_Compressed_DXT1_A1: format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK; break;
		case RTexFormat_Compressed_DXT3: format = VK_FORMAT_BC2_UNORM_BLOCK; break;
		case RTexFormat_Compressed_DXT5: format = VK_FORMAT_BC3_UNORM_BLOCK; break;
		default:
			return (RTexture){0};
	}

	// 1. Create VkImage w/ usage = DST|SAMPLED, layout=UNDEFINED
	const VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VkImageCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.extent.width = params.width;
	ici.extent.height = params.height;
	ici.extent.depth = 1;
	ici.mipLevels = params.mipmaps_count;
	ici.arrayLayers = 1;
	ici.format = format;
	ici.tiling = tiling;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ici.usage = usage;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkImage image;
	AVK_CHECK_RESULT(vkCreateImage(a_vk.dev, &ici, NULL, &image));

	// 2. Alloc mem for VkImage and bind it (DEV_LOCAL)
	VkMemoryRequirements memreq;
	vkGetImageMemoryRequirements(a_vk.dev, image, &memreq);
	struct AllocatedMemory mem = allocateDeviceMemory(DMC_Image, memreq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	AVK_CHECK_RESULT(vkBindImageMemory(a_vk.dev, image, mem.devmem, mem.offset));

	{
		// 5. Create/get cmdbuf for transitions
		VkCommandBufferBeginInfo beginfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		beginfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		AVK_CHECK_RESULT(vkBeginCommandBuffer(g.cmd_primary, &beginfo));

		// 	5.1 upload buf -> image:layout:DST
		// 		5.1.1 transitionToLayout(UNDEFINED -> DST)
		VkImageMemoryBarrier image_barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		image_barrier.image = image;
		image_barrier.srcAccessMask = 0;
		image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_barrier.subresourceRange = (VkImageSubresourceRange){
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = params.mipmaps_count,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};
		vkCmdPipelineBarrier(g.cmd_primary,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, NULL, 0, NULL, 1, &image_barrier);

		// 		5.1.2 copyBufferToImage for all mip levels
		const size_t staging_offset = 0; // TODO multiple staging buffer users params.staging->ptr
		for (int i = 0; i < params.mipmaps_count; ++i) {
			const RTextureUploadMipmapData *mip = params.mipmaps + i;
			VkBufferImageCopy region = {0};
			region.bufferOffset = mip->offset + staging_offset;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource = (VkImageSubresourceLayers){
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = mip->mip_level,
				.baseArrayLayer = 0,
				.layerCount = 1,
			};
			region.imageExtent = (VkExtent3D){
				.width = mip->width,
				.height = mip->height,
				.depth = 1,
			};
			vkCmdCopyBufferToImage(g.cmd_primary, g.staging.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		}

		// 	5.2 image:layout:DST -> image:layout:SAMPLED
		// 		5.2.1 transitionToLayout(DST -> SHADER_READ_ONLY)
		image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_barrier.subresourceRange = (VkImageSubresourceRange){
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = params.mipmaps_count,
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
	ivci.format = format;
	ivci.image = image;
	ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	ivci.subresourceRange.baseMipLevel = 0;
	ivci.subresourceRange.levelCount = params.mipmaps_count;
	ivci.subresourceRange.baseArrayLayer = 0;
	ivci.subresourceRange.layerCount = 1;
	AVK_CHECK_RESULT(vkCreateImageView(a_vk.dev, &ivci, NULL, &imview));

	VkDescriptorSet set = NULL;
	{
			struct DescriptorKasha* descriptors = NULL;
			uint32_t binding = 0;
			switch (params.kind) {
			case RTexKind_Lightmap:
					descriptors = g.descriptors[Descriptors_Lightmaps];
					binding = DescriptorBinding_Lightmap; break;
			case RTexKind_Material0:
					descriptors = g.descriptors[Descriptors_Textures];
					binding = DescriptorBinding_BaseMaterialTexture; break;
			default:
					ATTO_ASSERT(!"Unexpected texture kind");
			}
			ATTO_ASSERT(descriptors->count > descriptors->next_free);
			set = descriptors->descriptors[descriptors->next_free++];
			VkDescriptorImageInfo dii_tex = {
				.imageView = imview,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			VkWriteDescriptorSet wds[] = { {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = set,
				.dstBinding = binding,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &dii_tex,
			}};
			vkUpdateDescriptorSets(a_vk.dev, COUNTOF(wds), wds, 0, NULL);
	}

	return (RTexture){
		params.width,
		params.height,
		params.format,
		image, imview, set,
	};
}

static void createStagingBuffer() {
	g.staging.size = STAGING_SIZE;

	VkBufferCreateInfo bci = {0};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = g.staging.size;
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

	AVK_CHECK_RESULT(vkMapMemory(a_vk.dev, g.staging.devmem, 0, bci.size, 0, &g.staging.mapped));
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
	(void)type;
	assert(size <= HEAP_SIZE);
	struct GiantBuffer *gb = NULL;
	for (int i = 0; i  < MAX_HEAPS; ++i) {
		gb = g.buffers + i;
		if (gb->size == 0)
			break;

		if (gb->offset + size <= gb->size)
			break;
	}

	assert(gb);

	if (gb->size == 0) {
			*gb = createGiantBuffer(HEAP_SIZE, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
#if RTX
					| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
#endif
		);
	}

	memcpy((uint8_t*)gb->mapped + gb->offset, data, size);
	buffer->index = (uint32_t)(gb - g.buffers);
	buffer->offset = gb->offset;

	const uint32_t align = sizeof(struct BSPModelVertex);
	//gb->offset += (size + (align - 1)) & ~(align - 1);
	gb->offset += ((size + (align - 1)) / align) * align;
}

static VkBuffer createDeviceLocalBuffer(size_t size, VkBufferUsageFlags usage) {
	VkBuffer ret;
	VkBufferCreateInfo bci = {0};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = size;
	bci.usage = usage;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	AVK_CHECK_RESULT(vkCreateBuffer(a_vk.dev, &bci, NULL, &ret));

	VkMemoryRequirements memreq;
	vkGetBufferMemoryRequirements(a_vk.dev, ret, &memreq);
	aAppDebugPrintf("%s: memreq: memoryTypeBits=0x%x alignment=%zu size=%zu", __FUNCTION__, memreq.memoryTypeBits, memreq.alignment, memreq.size);
	struct AllocatedMemory mem = allocateDeviceMemory(DMC_Buffer, memreq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	AVK_CHECK_RESULT(vkBindBufferMemory(a_vk.dev, ret, mem.devmem, mem.offset));
	return ret;
}

#if RTX

static struct Buffer createBuffer(size_t size, VkBufferUsageFlags usage) {
	struct Buffer ret = {.size = size};
	VkBufferCreateInfo bci = {0};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = size;
	bci.usage = usage;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	AVK_CHECK_RESULT(vkCreateBuffer(a_vk.dev, &bci, NULL, &ret.buffer));

	VkMemoryRequirements memreq;
	vkGetBufferMemoryRequirements(a_vk.dev, ret.buffer, &memreq);
	aAppDebugPrintf("memreq: memoryTypeBits=0x%x alignment=%zu size=%zu", memreq.memoryTypeBits, memreq.alignment, memreq.size);

	VkMemoryAllocateInfo mai={0};
	mai.allocationSize = memreq.size;
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.memoryTypeIndex = aVkFindMemoryWithType(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	AVK_CHECK_RESULT(vkAllocateMemory(a_vk.dev, &mai, NULL, &ret.devmem));
	AVK_CHECK_RESULT(vkBindBufferMemory(a_vk.dev, ret.buffer, ret.devmem, 0));

	AVK_CHECK_RESULT(vkMapMemory(a_vk.dev, ret.devmem, 0, bci.size, 0, &ret.data));
	//vkUnmapMemory(a_vk.dev, g.devmem);
	return ret;
}

static void destroyBuffer(struct Buffer *buf) {
	vkUnmapMemory(a_vk.dev, buf->devmem);
	vkDestroyBuffer(a_vk.dev, buf->buffer, NULL);
	vkFreeMemory(a_vk.dev, buf->devmem, NULL);
	*buf = (struct Buffer){0};
}

VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer) {
	const VkBufferDeviceAddressInfo bdai = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer};
	return vkGetBufferDeviceAddress(a_vk.dev, &bdai);
}

static struct Accel createAccelerationStructure(const VkAccelerationStructureGeometryKHR* geoms, const uint32_t* max_prim_counts, const VkAccelerationStructureBuildRangeInfoKHR** build_ranges, uint32_t n_geoms, VkAccelerationStructureTypeKHR type) {
		struct Accel accel;
		VkAccelerationStructureBuildGeometryInfoKHR build_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = type,
			.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
			.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
			.geometryCount = n_geoms,
			.pGeometries = geoms,
		};

		VkAccelerationStructureBuildSizesInfoKHR build_size = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		AVK_DEV_FUNC(vkGetAccelerationStructureBuildSizesKHR)(
				a_vk.dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, max_prim_counts, &build_size);

		aAppDebugPrintf(
				"AS build size: %d, scratch size: %d", build_size.accelerationStructureSize, build_size.buildScratchSize);

		accel.buffer = createBuffer(build_size.accelerationStructureSize,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
		struct Buffer scratch_buffer = createBuffer(build_size.buildScratchSize,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

		const VkAccelerationStructureCreateInfoKHR asci = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.buffer = accel.buffer.buffer,
			.size = accel.buffer.size,
			.type = type,
		};
		AVK_CHECK_RESULT(AVK_DEV_FUNC(vkCreateAccelerationStructureKHR)(a_vk.dev, &asci, NULL, &accel.handle));

		build_info.dstAccelerationStructure = accel.handle;
		build_info.scratchData.deviceAddress = getBufferDeviceAddress(scratch_buffer.buffer);

		//VkCommandBufferBeginInfo beginfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		//beginfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		//AVK_CHECK_RESULT(vkBeginCommandBuffer(g.cmd_primary, &beginfo));
		AVK_DEV_FUNC(vkCmdBuildAccelerationStructuresKHR)(g.cmd_primary, 1, &build_info, build_ranges);
		//AVK_CHECK_RESULT(vkEndCommandBuffer(g.cmd_primary));

	VkMemoryBarrier mem_barrier = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
	};
	vkCmdPipelineBarrier(g.cmd_primary,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_DEPENDENCY_DEVICE_GROUP_BIT, 1, &mem_barrier, 0, NULL, 0, NULL);

		//VkSubmitInfo subinfo = {
		//	.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		//	.commandBufferCount = 1,
		//	.pCommandBuffers = &g.cmd_primary,
		//};
		//AVK_CHECK_RESULT(vkQueueSubmit(a_vk.main_queue, 1, &subinfo, NULL));
		//AVK_CHECK_RESULT(vkQueueWaitIdle(a_vk.main_queue));

		// FIXME do it later? destroyBuffer(&scratch_buffer);

		return accel;
}

static VkDeviceAddress getASAddress(VkAccelerationStructureKHR as) {
	VkAccelerationStructureDeviceAddressInfoKHR asdai = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = as,
	};
	return AVK_DEV_FUNC(vkGetAccelerationStructureDeviceAddressKHR)(a_vk.dev, &asdai);
}

static void renderLoadModelBlas(struct BSPModel* model) {
	const struct BSPDrawSet* draws = &model->detailed;

	VkAccelerationStructureGeometryKHR *geom = malloc(draws->draws_count * sizeof(VkAccelerationStructureGeometryKHR)); // TODO zero allocations
	VkAccelerationStructureBuildRangeInfoKHR *build_ranges = malloc(draws->draws_count * sizeof(VkAccelerationStructureBuildRangeInfoKHR)); // TODO zero alloc
	VkAccelerationStructureBuildRangeInfoKHR **build_ranges_ptrs = malloc(draws->draws_count * sizeof(void*)); // TODO zero alloc
	uint32_t *max_prim_counts = malloc(draws->draws_count * sizeof(uint32_t)); // TODO zero alloc

	const VkDeviceAddress vtx_addr = getBufferDeviceAddress(g.buffers[0].buffer); // FIXME real buffer
	const VkDeviceAddress idx_addr = getBufferDeviceAddress(g.buffers[0].buffer); // FIXME real buffer

	for (int i = 0; i < draws->draws_count; ++i) {
			const struct BSPDraw* draw = draws->draws + i;
			geom[i] = (VkAccelerationStructureGeometryKHR){
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
				.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
				.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
				.geometry.triangles =
					(VkAccelerationStructureGeometryTrianglesDataKHR){
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
						.indexType = VK_INDEX_TYPE_UINT16,
						.indexData.deviceAddress = idx_addr + model->ibo.offset + draw->start * sizeof(uint16_t),
						.maxVertex = draw->count,
						.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
						.vertexStride = sizeof(struct BSPModelVertex),
						.vertexData.deviceAddress = vtx_addr + draw->vbo_offset * sizeof(struct BSPModelVertex) + model->vbo.offset,
					},
			};

			max_prim_counts[i] = draw->count / 3;
			build_ranges[i] = (VkAccelerationStructureBuildRangeInfoKHR){
					.primitiveCount = max_prim_counts[i],
			};
			build_ranges_ptrs[i] = build_ranges + i;
	}

	// FIXME what about buffer allocation
	model->vk.blas = createAccelerationStructure(
		geom, max_prim_counts, build_ranges_ptrs, draws->draws_count, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR).handle;

	free(geom);
	free(max_prim_counts);
	free(build_ranges);
	free(build_ranges_ptrs);

	// FIXME it is not correct to have per-map tlas at all
	const VkAccelerationStructureInstanceKHR inst[] = {{
		.transform = (VkTransformMatrixKHR){1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}, // FIXME real map translation matrix
		.instanceCustomIndex = 0,
		.mask = 0xff,
		.instanceShaderBindingTableRecordOffset = 0,
		.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
		.accelerationStructureReference = getASAddress(model->vk.blas),
	},
	};

	struct Buffer tl_geom_buffer = createBuffer(sizeof(inst),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
	memcpy(tl_geom_buffer.data, &inst, sizeof(inst));

	const VkAccelerationStructureGeometryKHR tl_geom[] = {
		{
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			//.flags = VK_GEOMETRY_OPAQUE_BIT,
			.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
			.geometry.instances =
				(VkAccelerationStructureGeometryInstancesDataKHR){
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
					.data.deviceAddress = getBufferDeviceAddress(tl_geom_buffer.buffer),
					.arrayOfPointers = VK_FALSE,
				},
		},
	};

	const uint32_t tl_max_prim_counts[COUNTOF(tl_geom)] = {COUNTOF(inst)};
	const VkAccelerationStructureBuildRangeInfoKHR tl_build_range = {
			.primitiveCount = COUNTOF(inst),
	};
	const VkAccelerationStructureBuildRangeInfoKHR *tl_build_ranges[] = {&tl_build_range};
	g.rtx.tlas = createAccelerationStructure(
			tl_geom, tl_max_prim_counts, tl_build_ranges, COUNTOF(tl_geom), VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);
}
#endif //ifdef RTX

	static void rebindGlobalDescriptor() {
			VkDescriptorBufferInfo dbi = {
				.buffer = g.ubo,
				.offset = 0,
				.range = VK_WHOLE_SIZE
			};
#if RTX
			const VkWriteDescriptorSetAccelerationStructureKHR wdsas = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
				.accelerationStructureCount = 1,
				.pAccelerationStructures = &g.rtx.tlas.handle,
			};
#endif
			VkWriteDescriptorSet wds[] = { {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = g.descriptors[Descriptors_Global]->descriptors[0],
				.dstBinding = DescriptorBinding_Ubo,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &dbi,
#if RTX
			}, {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
				.dstSet = g.descriptors[Descriptors_Global]->descriptors[0],
				.dstBinding = 1,
				.dstArrayElement = 0,
				.pNext = &wdsas,
			},
#endif
		};
		vkUpdateDescriptorSets(a_vk.dev, COUNTOF(wds), wds, 0, NULL);
	}

void renderBegin(const struct Camera* camera) {
	aVkAcquireNextImage();

	{
			VkCommandBufferBeginInfo beginfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			beginfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			AVK_CHECK_RESULT(vkBeginCommandBuffer(g.cmd_primary, &beginfo));
	}

	{
			// Vulkan has Y pointing down, and z should end up in (0, 1)
			const struct AMat4f vk_proj_fixup = {
				aVec4f(1, 0, 0, 0),
				aVec4f(0, -1, 0, 0),
				aVec4f(0, 0, .5, 0),
				aVec4f(0, 0, .5, 1)
			};

			struct Ubo ubo = {
					.projection = aMat4fMul(vk_proj_fixup, camera->projection),
					.model_view = camera->view,
			};
			vkCmdUpdateBuffer(g.cmd_primary, g.ubo, 0, sizeof(ubo), &ubo);

			VkMemoryBarrier mem_barrier = {
					.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
					.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			};
			vkCmdPipelineBarrier(g.cmd_primary, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT // | Accel build???
					, VK_DEPENDENCY_DEVICE_GROUP_BIT, 1, &mem_barrier, 0, NULL, 0, NULL);
	}

	VkCommandBufferInheritanceInfo inherit = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
	inherit.framebuffer = g.framebuffers[a_vk.swapchain.current_frame_image_index];
	inherit.renderPass = g.render_pass;
	inherit.subpass = 0;
	VkCommandBufferBeginInfo mat_beginfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
		.pInheritanceInfo = &inherit,
	};

	for (int i = 0; i < (int)COUNTOF(g.cmd_materials); ++i) {
		VkCommandBuffer cb = g.cmd_materials[i];
		AVK_CHECK_RESULT(vkBeginCommandBuffer(cb, &mat_beginfo));

		struct AVkMaterial *m = g.materials + i;
		if (m->pipeline) {
			vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m->pipeline);
			const VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cb, 0, 1, &g.buffers[0].buffer, &offset);
			vkCmdBindIndexBuffer(cb, g.buffers[0].buffer, 0, VK_INDEX_TYPE_UINT16);

#if RTX
			if (g.rtx.tlas.handle)
#endif
			vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m->pipeline_layout, 0, 1, &g.descriptors[Descriptors_Global]->descriptors[0], 0, NULL);
		}
	}
}

void renderModelDraw(const RDrawParams *params, struct BSPModel *model) {
	(void)params;
	if (!model->detailed.draws_count) return;

	// FIXME aMat4fTranslation(params->translation)));

	int seen_material[MShader_COUNT] = {0};

#if RTX
	if (!model->vk.blas) {
			renderLoadModelBlas(model);
			// FIXME this should really be done on model load?
			rebindGlobalDescriptor();

			// HACK: as we're updating TLAS only once here, we need to (re)bind global descriptor here too
			for (int i = 0; i < (int)COUNTOF(g.cmd_materials); ++i) {
					VkCommandBuffer cb = g.cmd_materials[i];
					struct AVkMaterial* m = g.materials + i;
					if (m->pipeline) {
							vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m->pipeline_layout, 0, 1, &g.descriptors[Descriptors_Global]->descriptors[0], 0, NULL);
					}
			}
	}
#endif

	for (int i = 0; i < model->detailed.draws_count; ++i) {
		const struct BSPDraw* draw = model->detailed.draws + i;

		const int mat_index = draw->material->shader;
		VkCommandBuffer cb = g.cmd_materials[mat_index];
		struct AVkMaterial *m = g.materials + mat_index;

		if (!m->pipeline)
			continue;

		// FIXME figure out why we don't have a texture, and what to do
		if (mat_index == MShader_LightmappedGeneric && !draw->material->base_texture.texture)
			continue;

		if (!seen_material[mat_index]) {
			seen_material[mat_index] = 1;


			// TODO we should update lightmap desc set only once per model+cmdbuf (?)
			vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m->pipeline_layout, 1, 1, (const VkDescriptorSet*)&model->lightmap.descriptor, 0, NULL);
		}

		if (draw->material->base_texture.texture)
			vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m->pipeline_layout, 2, 1, (const VkDescriptorSet*)&draw->material->base_texture.texture->texture.descriptor, 0, NULL);

		const int32_t offset = draw->vbo_offset + model->vbo.offset / sizeof(struct BSPModelVertex);
		vkCmdDrawIndexed(cb, draw->count, 1, draw->start + model->ibo.offset/sizeof(uint16_t), offset, 0);
	}
}

void renderEnd(const struct Camera *camera) {
	(void)(camera);

	for (int i = 0; i < (int)COUNTOF(g.cmd_materials); ++i) {
		AVK_CHECK_RESULT(vkEndCommandBuffer(g.cmd_materials[i]));
	}

	VkClearValue clear_value[2] = {
		{.color = {{0., 0., 0., 0.}}},
		{.depthStencil = {1., 0}}
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

int renderInit() {
	aVkInitInstance();
	aVkCreateSurface();

#if RTX
	VkPhysicalDeviceBufferDeviceAddressFeaturesEXT pdbdaf = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT,
		.bufferDeviceAddress = VK_TRUE,
	};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR pdasf = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
		.accelerationStructure = VK_TRUE,
		// not supported by nv .accelerationStructureHostCommands = VK_TRUE,
		.pNext = &pdbdaf,
	};
	//VkPhysicalDeviceRayTracingPipelineFeaturesKHR pdrtf = {
	//	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
	//	.rayTracingPipeline = VK_TRUE,
	//	.pNext = &pdasf,
	//};
	VkPhysicalDeviceRayQueryFeaturesKHR pdrqf = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
		.rayQuery = VK_TRUE,
		.pNext = &pdasf,
	};
	const void* create_info = &pdrqf;
#else
	const void* create_info = NULL;
#endif

	aVkInitDevice(create_info, NULL, NULL);

	aVkPokePresentModes();

	VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	sci.magFilter = VK_FILTER_LINEAR;
	sci.minFilter = VK_FILTER_LINEAR;
	sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sci.anisotropyEnable = VK_FALSE;
	sci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	sci.unnormalizedCoordinates = VK_FALSE;
	sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sci.minLod = 0.f;
	sci.maxLod = 16.;
	AVK_CHECK_RESULT(vkCreateSampler(a_vk.dev, &sci, NULL, &g.default_sampler));

	createStagingBuffer();
	createDesriptorSets();
	createShaders();

	renderResize(16, 16);

	createCommandPool();

	g.ubo = createDeviceLocalBuffer(sizeof(struct Ubo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
#if !RTX
		rebindGlobalDescriptor();
#endif

	VkFenceCreateInfo fci = {0};
	fci.flags = 0;
	fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	AVK_CHECK_RESULT(vkCreateFence(a_vk.dev, &fci, NULL, &g.fence));

	g.done = aVkCreateSemaphore();

	return 1;
}

void renderDestroy() {
	vkDeviceWaitIdle(a_vk.dev);

	vkDestroyBuffer(a_vk.dev, g.ubo, NULL);

	vkUnmapMemory(a_vk.dev, g.staging.devmem);
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

	for (int i = 0; i < (int)COUNTOF(g.descriptors); ++i) {
			vkDestroyDescriptorSetLayout(a_vk.dev, g.descriptors[i]->layout, NULL);
			free(g.descriptors[i]);
	}

	vkDestroyRenderPass(a_vk.dev, g.render_pass, NULL);
	aVkDestroySemaphore(g.done);
	vkDestroyFence(a_vk.dev, g.fence, NULL);
}

void renderResize(int w, int h) {
	aVkCreateSwapchain(w, h);
	renderVkSwapchainCreated(a_vk.surface_width, a_vk.surface_height);
}
