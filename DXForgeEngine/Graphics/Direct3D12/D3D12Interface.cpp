// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Interface.cpp]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　Direct3D12のインターフェース
// 更新履歴
// 2024/12/27 新規作成
// 2025/01/13 マテリアルの追加と削除の関数を追加
// 2025/01/14 レンダーアイテムの追加と削除の関数を追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "D3D12Interface.h"
#include "D3D12Core.h"
#include "D3D12Content.h"
#include "D3D12Light.h"
#include "D3D12Camera.h"
#include "Graphics/GraphicsPlatformInterface.h"

namespace dxforge::graphics::d3d12
{
	void get_platform_interface(platform_interface& pi)
	{
		pi.initialize = core::initialize;
		pi.shutdown = core::shutdown;

		pi.surface.create = core::create_surface;
		pi.surface.remove = core::remove_surface;
		pi.surface.resize = core::resize_surface;
		pi.surface.width = core::surface_width;
		pi.surface.height = core::surface_height;
		pi.surface.render = core::render_surface;

		pi.light.create_light_set = light::create_light_set;
		pi.light.remove_light_set = light::remove_light_set;
		pi.light.create = light::create;
		pi.light.remove = light::remove;
		pi.light.set_parameter = light::set_parameter;
		pi.light.get_parameter = light::get_parameter;

		pi.camera.create = camera::create;
		pi.camera.remove = camera::remove;
		pi.camera.set_parameter = camera::set_parameter;
		pi.camera.get_parameter = camera::get_parameter;

		pi.resources.add_submesh = content::submesh::add;
		pi.resources.remove_submesh = content::submesh::remove;
		pi.resources.add_texture = content::texture::add;
		pi.resources.remove_texture = content::texture::remove;
		pi.resources.add_material = content::material::add;
		pi.resources.remove_material = content::material::remove;
		pi.resources.add_render_item = content::render_item::add;
		pi.resources.remove_render_item = content::render_item::remove;

		pi.platform = graphics_platform::direct3d12;
	}

}	// namespace dxforge::graphics

