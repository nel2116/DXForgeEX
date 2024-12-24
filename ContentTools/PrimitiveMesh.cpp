// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [PrimitiveMesh.cpp]
// 作成日 : 2024/12/24
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2024/12/24 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "PrimitiveMesh.h"
#include "Geometry.h"

namespace dxforge::tools
{
	namespace
	{
		using primitive_mesh_creator = void(*)(scene&, const primitive_init_info& info);

		void create_plane(scene& scene, const primitive_init_info& info);
		void create_cube(scene& scene, const primitive_init_info& info);
		void create_uv_sphere(scene& scene, const primitive_init_info& info);
		void create_ico_sphere(scene& scene, const primitive_init_info& info);
		void create_cylinder(scene& scene, const primitive_init_info& info);
		void create_capsule(scene& scene, const primitive_init_info& info);

		primitive_mesh_creator creators[]
		{
			create_plane,
			create_cube,
			create_uv_sphere,
			create_ico_sphere,
			create_cylinder,
			create_capsule
		};

		static_assert(_countof(creators) == primitive_mesh_type::count);

	} // 匿名名前空間


	EDITOR_INTERFACE void CreatePrimitiveMesh(scene_data* data, primitive_init_info* info)
	{
		assert(data && info);
		assert(info->type < primitive_mesh_type::count);
		scene scene{};
	}





} // namespace dxforge::tools


