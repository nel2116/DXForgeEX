// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Light.cpp]
// 作成日 : 2025/01/18
// 作成者 : 田中ミノル
// 概要 :
// Direct3D12のライトクラス
// 更新履歴
// 2025/01/18 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "D3D12Light.h"
#include "D3D12Core.h"
#include "Shaders/SharedTypes.h"
#include "EngineAPI/GameEntity.h"

namespace dxforge::graphics::d3d12::light
{
	namespace
	{
		/// @brief ライトの所有者
		struct light_owener
		{
			game_entity::entity_id entity_id{ id::invalid_id };
			u32 data_index{ 0 };
			graphics::light::type type;
			bool is_enabled;
		};


#if USE_STL_VECTOR
#define		CONSTEXPR
#else
#define		CONSTEXPR constexpr
#endif

		/// @brief ライトセット
		class light_set
		{
		public:		// パブリック関数
			/// @brief ライトを追加する
			/// @param info ライトの初期化情報
			/// @return 追加したライトのID
			constexpr graphics::light add(const light_init_info& info)
			{
				if (info.type == graphics::light::directional)
				{
					u32 index{ u32_invalid_id };
					// 配列に空いているスロットがあれば探す。
					for (u32 i{ 0 }; i < _non_cullable_owners.size(); ++i)
					{
						if (!id::is_valid(_non_cullable_owners[i]))
						{
							index = i;
							break;
						}
					}

					if (index == u32_invalid_id)
					{
						// 空いているスロットがない場合は新しく追加する。
						index = (u32)_non_cullable_owners.size();
						_non_cullable_owners.emplace_back();
						_non_cullable_lights.emplace_back();
					}

					hlsl::DirectionalLightParameters& params{ _non_cullable_lights[index] };
					params.Color = info.color;
					params.Intensity = info.intensity;

					light_owener owner{ game_entity::entity_id{info.entity_id },index,info.type,info.is_enabled };
					const light_id id{ _owners.add(owner) };
					_non_cullable_owners[index] = id;

					return graphics::light{ id, info.light_set_key };
				}
				else
				{
					// TODO: その他のライトの処理
					return {};
				}
			}

			/// @brief ライトを削除する
			/// @param id 削除するライトのID
			constexpr void remove(light_id id)
			{
				enable(id, false);
				const light_owener& owner{ _owners[id] };

				if (owner.type == graphics::light::directional)
				{
					_non_cullable_owners[owner.data_index] = light_id{ u32_invalid_id };
				}
				else
				{
					// TODO: その他のライトの処理
				}
				_owners.remove(id);
			}

			void uptdate_transforms()
			{
				//Update direction for non-cullable lights
				for (const auto& id : _non_cullable_owners)
				{
					if (!id::is_valid(id))continue;

					const light_owener& owner{ _owners[id] };
					if (owner.is_enabled)
					{
						const game_entity::entity entity{ game_entity::entity_id{owner.entity_id } };
						hlsl::DirectionalLightParameters& params{ _non_cullable_lights[owner.data_index] };
						params.Direction = entity.orientation();
					}
				}
				// TODO: その他のライトの処理
			}

			/// @brief ライトの有効無効を設定する
			/// @param id ライトID
			/// @param is_enabled ライトが有効かどうか
			constexpr void enable(light_id id, bool is_enabled)
			{
				_owners[id].is_enabled = is_enabled;
				if (_owners[id].type == graphics::light::directional)
				{
					return;
				}

				// TODO: その他のライトの処理
			}

			/// @brief ライトの強度を設定する
			/// @param id ライトID
			/// @param intensity ライトの強度
			constexpr void Intensity(light_id id, f32 intensity)
			{
				if (intensity < 0.0f)intensity = 0.0f;
				const light_owener& owner{ _owners[id] };
				const u32 index{ owner.data_index };

				if (owner.type == graphics::light::directional)
				{
					assert(index < _non_cullable_lights.size());
					_non_cullable_lights[index].Intensity = intensity;
				}
				else
				{
					// TODO: その他のライトの処理
				}
			}

			/// @brief ライトの色を設定する
			/// @param id ライトID
			/// @param color ライトの色
			constexpr void color(light_id id, math::v3 color)
			{
				assert(color.x >= 0.0f && color.y >= 0.0f && color.z >= 0.0f);
				assert(color.x <= 1.0f && color.y <= 1.0f && color.z <= 1.0f);

				const light_owener& owner{ _owners[id] };
				const u32 index{ owner.data_index };

				if (owner.type == graphics::light::directional)
				{
					assert(index < _non_cullable_lights.size());
					_non_cullable_lights[index].Color = color;
				}
				else
				{
					// TODO: その他のライトの処理
				}
			}

			/// @brief ライトが有効かどうかを取得する
			/// @param id ライトID
			/// @return ライトが有効かどうか
			constexpr bool is_enabled(light_id id) const
			{
				return _owners[id].is_enabled;
			}

			/// @brief ライトの強度を取得する
			/// @param id ライトID
			/// @return ライトの強度
			constexpr f32 intensity(light_id id) const
			{
				const light_owener& owner{ _owners[id] };
				const u32 index{ owner.data_index };

				if (owner.type == graphics::light::directional)
				{
					assert(index < _non_cullable_lights.size());
					return _non_cullable_lights[index].Intensity;
				}

				// TODO: その他のライトの処理
				return 0.0f;
			}

			/// @brief ライトの色を取得する
			/// @param id ライトID
			/// @return ライトの色
			constexpr math::v3 color(light_id id) const
			{
				const light_owener& owner{ _owners[id] };
				const u32 index{ owner.data_index };

				if (owner.type == graphics::light::directional)
				{
					assert(index < _non_cullable_lights.size());
					return _non_cullable_lights[index].Color;
				}

				// TODO: その他のライトの処理
				return {};
			}

			/// @brief ライトの種類を取得する
			/// @param id ライトID
			/// @return ライトの種類
			constexpr graphics::light::type type(light_id id) const
			{
				return _owners[id].type;
			}

			/// @brief ライトがアタッチされているエンティティのIDを取得する
			/// @param id ライトID
			/// @return ライトがアタッチされているエンティティのID
			constexpr id::id_type entity_id(light_id id) const
			{
				return _owners[id].entity_id;
			}

			/// @brief 有効なライトの数を取得する
			/// @return　有効なライトの数
			CONSTEXPR u32 non_cullable_light_count() const
			{
				u32 count{ 0 };
				for (const auto& id : _non_cullable_owners)
				{
					if (id::is_valid(id) && _owners[id].is_enabled) ++count;
				}
				return count;
			}

			CONSTEXPR void non_cullable_lights(hlsl::DirectionalLightParameters* const lights, [[maybe_unused]] u32 buffer_size)
			{
				assert(buffer_size == math::align_size_up<D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>(non_cullable_light_count() * sizeof(hlsl::DirectionalLightParameters)));
				const u32 count{ (u32)_non_cullable_owners.size() };
				u32 index{ 0 };
				for (u32 i{ 0 }; i < count; ++i)
				{
					if (!id::is_valid(_non_cullable_owners[i]))continue;

					const light_owener& owner{ _owners[_non_cullable_owners[i]] };
					if (owner.is_enabled)
					{
						assert(_owners[_non_cullable_owners[i]].data_index == i);
						lights[index] = _non_cullable_lights[i];
						++index;
					}
				}
			}

			constexpr bool has_light() const
			{
				return _owners.size() > 0;
			}

		private:	// メンバ変数
			// NOTE: これはパッキングされていない
			utl::free_list<light_owener> _owners;								///< ライトの所有者
			utl::vector<hlsl::DirectionalLightParameters> _non_cullable_lights;	///< 並行光源
			utl::vector<light_id> _non_cullable_owners;							///< カリングされていないライトの所有者
		};

		class d3d12_light_buffer
		{
		public:		// パブリック関数
			d3d12_light_buffer() = default;
			CONSTEXPR void update_light_buffers(light_set& set, u64 light_set_key, u32 frame_index)
			{
				u32 sizes[light_buffer::count]{};
				sizes[light_buffer::non_cullable_light] = set.non_cullable_light_count() * sizeof(hlsl::DirectionalLightParameters);

				u32 current_sizes[light_buffer::count]{};
				current_sizes[light_buffer::non_cullable_light] = _buffers[light_buffer::non_cullable_light].buffer.size();

				if (current_sizes[light_buffer::non_cullable_light] < sizes[light_buffer::non_cullable_light])
				{
					resize_buffer(light_buffer::non_cullable_light, sizes[light_buffer::non_cullable_light], frame_index);
				}

				set.non_cullable_lights((hlsl::DirectionalLightParameters* const)_buffers[light_buffer::non_cullable_light].cpu_address,
					_buffers[light_buffer::non_cullable_light].buffer.size());

				// TODO: その他のライトの処理
			}

			constexpr void release()
			{
				for (u32 i{ 0 }; i < light_buffer::count; ++i)
				{
					_buffers[i].buffer.release();
					_buffers[i].cpu_address = nullptr;
				}
			}

			constexpr D3D12_GPU_VIRTUAL_ADDRESS non_cullable_lights() const
			{
				return _buffers[light_buffer::non_cullable_light].buffer.gpu_address();
			}

		private:	// 構造体定義
			struct light_buffer
			{
				enum type :u32
				{
					non_cullable_light,
					cullable_light,
					culling_info,

					count
				};

				d3d12_buffer buffer{};
				u8* cpu_address{ nullptr };
			};

		private:	// プライベート関数
			void resize_buffer(light_buffer::type type, u32 size, [[maybe_unused]] u32 frame_index)
			{
				assert(type < light_buffer::count);
				if (!size) return;

				_buffers[type].buffer.release();
				_buffers[type].buffer = d3d12_buffer{ constant_buffer::get_default_init_info(size),true };
				NAME_D3D12_OBJECT_INDEXED(_buffers[type].buffer.buffer(), frame_index,
					type == light_buffer::non_cullable_light ? L"Non-cullable Light Buffer" :
					type == light_buffer::cullable_light ? L"Cullable Light Buffer" : L"Light Culling Info Buffer");

				D3D12_RANGE range{};
				DXCall(_buffers[type].buffer.buffer()->Map(0, &range, (void**)(&_buffers[type].cpu_address)));
				assert(_buffers[type].cpu_address);
			}

		private:	// メンバ変数
			light_buffer _buffers[light_buffer::count];
			u64 _current_light_set_key{ 0 };
		};

#undef		CONSTEXPR

		std::unordered_map<u64, light_set> light_sets;			///< ライトセット
		d3d12_light_buffer light_buffers[frame_buffer_count];	///< ライトバッファ

		constexpr void set_is_enabled(light_set& set, light_id id, const void* const data, [[maybe_unused]] u32 size)
		{
			bool is_enabled{ *(bool*)data };
			assert(size == sizeof(is_enabled));
			set.enable(id, is_enabled);
		}

		constexpr void set_intensity(light_set& set, light_id id, const void* const data, [[maybe_unused]] u32 size)
		{
			f32 intensity{ *(f32*)data };
			assert(size == sizeof(intensity));
			set.Intensity(id, intensity);
		}

		constexpr void set_color(light_set& set, light_id id, const void* const data, [[maybe_unused]] u32 size)
		{
			math::v3 color{ *(math::v3*)data };
			assert(size == sizeof(color));
			set.color(id, color);
		}

		constexpr void get_is_enabled(light_set& set, light_id id, void* const data, [[maybe_unused]] u32 size)
		{
			bool* const is_enabled{ (bool* const)data };
			assert(sizeof(bool) == size);
			*is_enabled = set.is_enabled(id);
		}

		constexpr void get_intensity(light_set& set, light_id id, void* const data, [[maybe_unused]] u32 size)
		{
			f32* const intensity{ (f32* const)data };
			assert(sizeof(f32) == size);
			*intensity = set.intensity(id);
		}

		constexpr void get_color(light_set& set, light_id id, void* const data, [[maybe_unused]] u32 size)
		{
			math::v3* const color{ (math::v3* const)data };
			assert(sizeof(math::v3) == size);
			*color = set.color(id);
		}

		constexpr void get_type(light_set& set, light_id id, void* const data, [[maybe_unused]] u32 size)
		{
			graphics::light::type* const type{ (graphics::light::type* const)data };
			assert(sizeof(graphics::light::type) == size);
			*type = set.type(id);
		}

		constexpr void get_entity_id(light_set& set, light_id id, void* const data, [[maybe_unused]] u32 size)
		{
			id::id_type* const entity_id{ (id::id_type* const)data };
			assert(sizeof(id::id_type) == size);
			*entity_id = set.entity_id(id);
		}

		constexpr void dummy_set(light_set&, light_id, const void* const, u32)
		{
		}

		// ライトのパラメータ設定関数
		using set_function = void(*)(light_set&, light_id, const void* const, u32);
		// ライトのパラメータ取得関数
		using get_function = void(*)(light_set&, light_id, void* const, u32);
		// ライトのパラメータ設定関数配列
		constexpr set_function set_functions[]
		{
			set_is_enabled,
			set_intensity,
			set_color,
			dummy_set,
			dummy_set,
		};

		static_assert(_countof(set_functions) == light_parameter::count);

		// ライトのパラメータ取得関数配列
		constexpr get_function get_functions[]
		{
			get_is_enabled,
			get_intensity,
			get_color,
			get_type,
			get_entity_id,
		};

		static_assert(_countof(get_functions) == light_parameter::count);

	}	// 匿名名前空間

	bool initialize()
	{
		return true;
	}

	void shutdown()
	{
		// グラフィックスをシャットダウンする前に、すべてのライトを削除することを確認してください。

		assert([] {
			bool has_light{ false };
			for (const auto& it : light_sets)
			{
				has_light |= it.second.has_light();
			}
			return !has_light;
			}());

		for (u32 i{ 0 }; i < frame_buffer_count; ++i)
		{
			light_buffers[i].release();
		}
	}

	graphics::light create(light_init_info info)
	{
		assert(id::is_valid(info.entity_id));
		return light_sets[info.light_set_key].add(info);
	}

	void remove(light_id id, u64 light_set_key)
	{
		assert(light_sets.count(light_set_key));
		light_sets[light_set_key].remove(id);
	}

	void set_parameter(light_id id, u64 light_set_key, light_parameter::parameter parameter, const void* const data, u32 data_size)
	{
		assert(data && data_size);
		assert(parameter < light_parameter::count && set_functions[parameter] != dummy_set);
		set_functions[parameter](light_sets[light_set_key], id, data, data_size);
	}

	void get_parameter(light_id id, u64 light_set_key, light_parameter::parameter parameter, void* const data, u32 data_size)
	{
		assert(data && data_size);
		assert(light_sets.count(light_set_key));
		assert(parameter < light_parameter::count);
		get_functions[parameter](light_sets[light_set_key], id, data, data_size);
	}

	void update_light_buffers(const d3d12_frame_info& frame_info)
	{
		const u64 light_set_key{ frame_info.info->light_set_key };
		assert(light_sets.count(light_set_key));
		light_set& set{ light_sets[light_set_key] };
		if (!set.has_light())return;

		set.uptdate_transforms();
		const u32 frame_index{ frame_info.frame_index };
		d3d12_light_buffer& light_buffer{ light_buffers[frame_index] };
		light_buffer.update_light_buffers(set, light_set_key, frame_index);
	}

	D3D12_GPU_VIRTUAL_ADDRESS non_cullable_light_buffer(u32 frame_index)
	{
		const d3d12_light_buffer& light_buffer{ light_buffers[frame_index] };
		return light_buffer.non_cullable_lights();
	}

	u32 non_cullable_light_count(u64 light_set_key)
	{
		assert(light_sets.count(light_set_key));
		return light_sets[light_set_key].non_cullable_light_count();
	}

}	// namespace dxforge::graphics::d3d12::light
