#include "render.h"
#include "bsp.h"
#include "camera.h"

#include "atto/app.h"
#include "atto/worobushek.h"

#include <stddef.h>
#include <stdlib.h>

static struct {
	VkFence fence;
	VkSemaphore done;
	VkRenderPass render_pass;

	VkPipelineLayout pipeline_layout;
	VkShaderModule module_vertex;
	VkShaderModule module_fragment;

	VkImageView *image_views;
	VkFramebuffer *framebuffers;
	VkPipeline pipeline;

	//VkDeviceMemory devmem;

	VkCommandPool cmdpool;
	VkCommandBuffer cmdbuf;
} g;

static void createRenderPass() {
	VkAttachmentDescription attachment = {0};
	attachment.format = VK_FORMAT_B8G8R8A8_SRGB;// FIXME too early a_vk.swapchain.info.imageFormat;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	//attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	//attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment = {0};
	color_attachment.attachment = 0;
	color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subdesc = {0};
	subdesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subdesc.colorAttachmentCount = 1;
	subdesc.pColorAttachments = &color_attachment;

	VkRenderPassCreateInfo rpci = {0};
	rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpci.attachmentCount = 1;
	rpci.pAttachments = &attachment;
	rpci.subpassCount = 1;
	rpci.pSubpasses = &subdesc;
	AVK_CHECK_RESULT(vkCreateRenderPass(a_vk.dev, &rpci, NULL, &g.render_pass));
}

static void createShaders() {
	VkPushConstantRange push_const = {0};
	push_const.offset = 0;
	push_const.size = sizeof(AMat4f);
	push_const.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkPipelineLayoutCreateInfo plci = {0};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 0;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &push_const;
	AVK_CHECK_RESULT(vkCreatePipelineLayout(a_vk.dev, &plci, NULL, &g.pipeline_layout));

	g.module_vertex = loadShaderFromFile("m_unknown.vert.spv");
	g.module_fragment = loadShaderFromFile("m_unknown.frag.spv");
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
	AVK_CHECK_RESULT(vkAllocateCommandBuffers(a_vk.dev, &cbai, &g.cmdbuf));
}

static void createPipeline() {
	VkPipelineShaderStageCreateInfo shader_stages[2] = {0};
	shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

	shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_stages[0].module = g.module_vertex;
	shader_stages[0].pName = "main";

	shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shader_stages[1].module = g.module_fragment;
	shader_stages[1].pName = "main";

	VkVertexInputBindingDescription vibd = {0};
	vibd.binding = 0;
	vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vibd.stride = sizeof(struct BSPModelVertex);

	VkVertexInputAttributeDescription attribs[] = {
		{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(struct BSPModelVertex, vertex)},
		/* {.binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(struct BSPModelVertex, lightmap_uv)}, */
		/* {.binding = 0, .location = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(struct BSPModelVertex, tex_uv)}, */
		/* {.binding = 0, .location = 3, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(struct BSPModelVertex, average_color)}, */
	};

	VkPipelineVertexInputStateCreateInfo vertex_input = {0};
	vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input.vertexBindingDescriptionCount = 1;
	vertex_input.pVertexBindingDescriptions = &vibd;
	vertex_input.vertexAttributeDescriptionCount = COUNTOF(attribs);
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
	raster_state.cullMode = VK_CULL_MODE_NONE;
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
	gpci.layout = g.pipeline_layout;
	gpci.renderPass = g.render_pass;
	gpci.subpass = 0;
	AVK_CHECK_RESULT(vkCreateGraphicsPipelines(a_vk.dev, NULL, 1, &gpci, NULL, &g.pipeline));
}

//#define renderTextureInit(texture_ptr) do { (texture_ptr)->gl_name = -1; } while (0)
void renderTextureUpload(RTexture *texture, RTextureUploadParams params) {
	(void)(texture); (void)(params);
}

int renderInit() {
	g.done = aVkCreateSemaphore();
	createRenderPass();
	createShaders();
	createCommandPool();

	VkFenceCreateInfo fci = {0};
	fci.flags = 0;
	fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	AVK_CHECK_RESULT(vkCreateFence(a_vk.dev, &fci, NULL, &g.fence));
	return 1;
}

void renderVkSwapchainCreated(int w, int h) {
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

		VkFramebufferCreateInfo fbci = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
		fbci.renderPass = g.render_pass;
		fbci.attachmentCount = 1;
		fbci.pAttachments = g.image_views + i;
		fbci.width = w;
		fbci.height = h;
		fbci.layers = 1;
		AVK_CHECK_RESULT(vkCreateFramebuffer(a_vk.dev, &fbci, NULL, g.framebuffers + i));
	}

	createPipeline();
}

void renderVkSwapchainDestroy() {
	vkDestroyPipeline(a_vk.dev, g.pipeline, NULL);
	for (uint32_t i = 0; i < a_vk.swapchain.num_images; ++i) {
		vkDestroyFramebuffer(a_vk.dev, g.framebuffers[i], NULL);
		vkDestroyImageView(a_vk.dev, g.image_views[i], NULL);
	}
	free(g.image_views);
	free(g.framebuffers);
}

void renderBufferCreate(RBuffer *buffer, RBufferType type, int size, const void *data) {
	VkBuffer buf;
	VkBufferCreateInfo bci = {0};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = size;
	switch (type) {
		case RBufferType_Index:
			bci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			break;
		case RBufferType_Vertex:
			bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			break;
	}
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	AVK_CHECK_RESULT(vkCreateBuffer(a_vk.dev, &bci, NULL, &buf));

	VkMemoryRequirements memreq;
	vkGetBufferMemoryRequirements(a_vk.dev, buf, &memreq);
	aAppDebugPrintf("memreq: memoryTypeBits=0x%x alignment=%zu size=%zu", memreq.memoryTypeBits, memreq.alignment, memreq.size);

	VkDeviceMemory devmem;
	VkMemoryAllocateInfo mai={0};
	mai.allocationSize = memreq.size;
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.memoryTypeIndex = aVkFindMemoryWithType(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	AVK_CHECK_RESULT(vkAllocateMemory(a_vk.dev, &mai, NULL, &devmem));
	AVK_CHECK_RESULT(vkBindBufferMemory(a_vk.dev, buf, devmem, 0));

	void *ptr = NULL;
	AVK_CHECK_RESULT(vkMapMemory(a_vk.dev, devmem, 0, bci.size, 0, &ptr));
		memcpy(ptr, data, size);
	vkUnmapMemory(a_vk.dev, devmem);

	buffer->vkBuf = buf;
	buffer->vkDevMem = devmem;
}

void renderBegin() {
	VkCommandBufferBeginInfo beginfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	beginfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	AVK_CHECK_RESULT(vkBeginCommandBuffer(g.cmdbuf, &beginfo));

	VkClearValue clear_value = {0};
	/* clear_value.color.float32[0] = .5f + .5f * sinf(t); */
	/* clear_value.color.float32[1] = .5f + .5f * sinf(t*3); */
	/* clear_value.color.float32[2] = .5f + .5f * sinf(t*5); */
	/* clear_value.color.float32[3] = 1.f; */
	/* clear_value.color.uint32[0] = 0xffffffff; */
	/* clear_value.color.uint32[3] = 0xffffffff; */
	VkRenderPassBeginInfo rpbi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
	rpbi.renderPass = g.render_pass;
	rpbi.framebuffer = g.framebuffers[a_vk.swapchain.current_frame_image_index];
	rpbi.renderArea.offset.x = rpbi.renderArea.offset.y = 0;
	rpbi.renderArea.extent.width = a_app_state->width;
	rpbi.renderArea.extent.height = a_app_state->height;
	rpbi.clearValueCount = 1;
	rpbi.pClearValues = &clear_value;
	vkCmdBeginRenderPass(g.cmdbuf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(g.cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
}

void renderModelDraw(const RDrawParams *params, const struct BSPModel *model) {
	if (!model->detailed.draws_count) return;

	const struct AMat4f mvp = aMat4fMul(params->camera->view_projection,
			aMat4fTranslation(params->translation));

	vkCmdPushConstants(g.cmdbuf, g.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);

	for (int i = 0; i < model->detailed.draws_count; ++i) {
		const struct BSPDraw* draw = model->detailed.draws + i;
		const VkDeviceSize offset = draw->vbo_offset;
		vkCmdBindVertexBuffers(g.cmdbuf, 0, 1, (VkBuffer*)&model->vbo.vkBuf, &offset);
		vkCmdBindIndexBuffer(g.cmdbuf, (VkBuffer)model->ibo.vkBuf, 0, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(g.cmdbuf, draw->count, 1, draw->start, 0, 0);
	}

	/* const int N = 32; */
	/* for (int i = 0; i < N; ++i) { */
	/* 	const float it = i / (float)N; */
	/* 	const float tt = t*(.8+it*.2) + it * ((1. + sin(t*.3))*.15 + .7) * 20.; */
	/* 	vkCmdPushConstants(cmdbuf, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &tt); */
	/* 	vkCmdDraw(cmdbuf, 3, 1, 0, 0); */
	/* } */
}

void renderEnd(const struct Camera *camera)
{
	(void)(camera);

	vkCmdEndRenderPass(g.cmdbuf);

	AVK_CHECK_RESULT(vkEndCommandBuffer(g.cmdbuf));

	VkPipelineStageFlags stageflags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo subinfo = {0};
	subinfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	subinfo.pNext = NULL;
	subinfo.commandBufferCount = 1;
	subinfo.pCommandBuffers = &g.cmdbuf;
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
	vkFreeCommandBuffers(a_vk.dev, g.cmdpool, 1, &g.cmdbuf);
	vkDestroyCommandPool(a_vk.dev, g.cmdpool, NULL);

	vkDestroyPipelineLayout(a_vk.dev, g.pipeline_layout, NULL);

	vkDestroyShaderModule(a_vk.dev, g.module_fragment, NULL);
	vkDestroyShaderModule(a_vk.dev, g.module_vertex, NULL);

	/* vkFreeMemory(a_vk.dev, g.devmem, NULL); */
	/* vkDestroyBuffer(a_vk.dev, g.vertex_buf, NULL); */

	vkDestroyRenderPass(a_vk.dev, g.render_pass, NULL);
	aVkDestroySemaphore(g.done);
	vkDestroyFence(a_vk.dev, g.fence, NULL);
}
