// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Renderer.cpp]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　レンダラー
// 更新履歴
// 2024/12/27 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Renderer.h"
#include "GraphicsPlatformInterface.h"
#include "Direct3D12/D3D12Interface.h"
#include "Vulkan/VulkanInterface.h"
#include "OpenGL/OpenGLInterface.h"

namespace dxforge::graphics
{
	namespace
	{
		platform_interface gfx{};

		bool set_platform_interface(graphics_platform platform)
		{
			switch (platform)
			{
			case graphics_platform::direct3d12:
				d3d12::get_platform_interface(gfx);
				break;
			case graphics_platform::vulkan:
				vulkan::get_platform_interface(gfx);
				break;
			case graphics_platform::opengl:
				opengl::get_platform_interface(gfx);
				break;
			default:
				return false;
			}
			return true;
		}
	}	// 匿名名前空間

	bool initialize(graphics_platform platform)
	{
		return set_platform_interface(platform) && gfx.initialize();
	}

	void shutdown()
	{
		gfx.shutdown();
	}
}	// namespace dxforge::graphics
