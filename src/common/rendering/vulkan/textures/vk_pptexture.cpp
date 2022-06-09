/*
**  Vulkan backend
**  Copyright (c) 2016-2020 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#include "vk_pptexture.h"
#include "vulkan/system/vk_framebuffer.h"
#include "vulkan/system/vk_commandbuffer.h"

VkPPTexture::VkPPTexture(PPTexture *texture)
{
	auto fb = GetVulkanFrameBuffer();

	VkFormat format;
	int pixelsize;
	switch (texture->Format)
	{
	default:
	case PixelFormat::Rgba8: format = VK_FORMAT_R8G8B8A8_UNORM; pixelsize = 4; break;
	case PixelFormat::Rgba16f: format = VK_FORMAT_R16G16B16A16_SFLOAT; pixelsize = 8; break;
	case PixelFormat::R32f: format = VK_FORMAT_R32_SFLOAT; pixelsize = 4; break;
	case PixelFormat::Rg16f: format = VK_FORMAT_R16G16_SFLOAT; pixelsize = 4; break;
	case PixelFormat::Rgba16_snorm: format = VK_FORMAT_R16G16B16A16_SNORM; pixelsize = 8; break;
	}

	ImageBuilder imgbuilder;
	imgbuilder.setFormat(format);
	imgbuilder.setSize(texture->Width, texture->Height);
	if (texture->Data)
		imgbuilder.setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	else
		imgbuilder.setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	if (!imgbuilder.isFormatSupported(fb->device))
		I_FatalError("Vulkan device does not support the image format required by a postprocess texture\n");
	TexImage.Image = imgbuilder.create(fb->device);
	TexImage.Image->SetDebugName("VkPPTexture");
	Format = format;

	ImageViewBuilder viewbuilder;
	viewbuilder.setImage(TexImage.Image.get(), format);
	TexImage.View = viewbuilder.create(fb->device);
	TexImage.View->SetDebugName("VkPPTextureView");

	if (texture->Data)
	{
		size_t totalsize = texture->Width * texture->Height * pixelsize;
		BufferBuilder stagingbuilder;
		stagingbuilder.setSize(totalsize);
		stagingbuilder.setUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
		Staging = stagingbuilder.create(fb->device);
		Staging->SetDebugName("VkPPTextureStaging");

		VkImageTransition barrier0;
		barrier0.addImage(&TexImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
		barrier0.execute(fb->GetCommands()->GetTransferCommands());

		void *data = Staging->Map(0, totalsize);
		memcpy(data, texture->Data.get(), totalsize);
		Staging->Unmap();

		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		region.imageExtent.depth = 1;
		region.imageExtent.width = texture->Width;
		region.imageExtent.height = texture->Height;
		fb->GetCommands()->GetTransferCommands()->copyBufferToImage(Staging->buffer, TexImage.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		VkImageTransition barrier1;
		barrier1.addImage(&TexImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);
		barrier1.execute(fb->GetCommands()->GetTransferCommands());
	}
	else
	{
		VkImageTransition barrier;
		barrier.addImage(&TexImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
		barrier.execute(fb->GetCommands()->GetTransferCommands());
	}
}

VkPPTexture::~VkPPTexture()
{
	if (auto fb = GetVulkanFrameBuffer())
	{
		if (TexImage.Image) fb->GetCommands()->FrameDeleteList.Images.push_back(std::move(TexImage.Image));
		if (TexImage.View) fb->GetCommands()->FrameDeleteList.ImageViews.push_back(std::move(TexImage.View));
		if (TexImage.DepthOnlyView) fb->GetCommands()->FrameDeleteList.ImageViews.push_back(std::move(TexImage.DepthOnlyView));
		if (TexImage.PPFramebuffer) fb->GetCommands()->FrameDeleteList.Framebuffers.push_back(std::move(TexImage.PPFramebuffer));
		if (Staging) fb->GetCommands()->FrameDeleteList.Buffers.push_back(std::move(Staging));
	}
}
