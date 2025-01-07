// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Resource.cpp]
// 作成日 : 2024/12/30
// 作成者 : 田中ミノル
// 概要 :
// Direct3D12のリソース管理
// 更新履歴
// 2024/12/30 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "D3D12Resource.h"
#include "D3D12Core.h"

namespace dxforge::graphics::d3d12
{
	//_/_/_/_/_/_/_/_/ DESCRIPTOR HEAP _/_/_/_/_/_/_/_/
	/// @brief 初期化処理
	/// @param capacity キャパシティ
	/// @param is_shader_visible シェーダーが見えるかどうか
	bool descriptor_heap::initialize(u32 capacity, bool is_shader_visible)
	{
		// ロック
		std::lock_guard lock{ _mutex };

		// キャパシティが正しいか確認
		assert(capacity && capacity < D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2);
		assert(!(_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER && capacity > D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE));

		// シェーダーが見えるかどうかを確認
		if (_type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV || D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
		{
			is_shader_visible = false;
		}

		// 前のヒープがあれば解放
		release();

		// デバイスを取得
		auto* const device{ core::device() };
		assert(device);

		// ヒープの設定
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Flags = is_shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NumDescriptors = capacity;
		desc.Type = _type;
		desc.NodeMask = 0;

		// ヒープを作成
		HRESULT hr{ S_OK };
		DXCall(hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_heap)));
		if (FAILED(hr)) return false;

		// ヒープの開始アドレスを取得
		_free_handles = std::move(std::make_unique<u32[]>(capacity));
		_capacity = capacity;
		_size = 0;

		for (u32 i{ 0 }; i < capacity; ++i)_free_handles[i] = i;
		DEBUG_OP(for (u32 i{ 0 }; i < frame_buffer_count; ++i) assert(_deferred_free_indices[i].empty()));

		_descriptor_size = device->GetDescriptorHandleIncrementSize(_type);
		_cpu_start = _heap->GetCPUDescriptorHandleForHeapStart();
		_gpu_start = is_shader_visible ? _heap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{ 0 };


		return true;
	}

	/// @brief 開放処理
	void descriptor_heap::release()
	{
		assert(!_size);
		// 遅延解放を行う
		core::deferred_release(_heap);
	}

	/// @brief 遅延解放フラグを設定
	/// @param frame_index フレームインデックス
	void descriptor_heap::process_deferred_free(u32 frame_index)
	{
		// ロック
		std::lock_guard lock{ _mutex };
		assert(frame_index < frame_buffer_count);


		utl::vector<u32>& indices{ _deferred_free_indices[frame_index] };
		if (!indices.empty())
		{
			for (u32 index : indices)
			{
				--_size;
				_free_handles[_size] = index;
			}
			indices.clear();
		}
	}

	/// @brief ディスクリプタの割り当て
	/// @return ディスクリプタハンドル
	descriptor_handle descriptor_heap::allocate()
	{
		// ロック
		std::lock_guard lock{ _mutex };
		// ヒープがあるか確認
		assert(_heap);
		assert(_size < _capacity);

		// ハンドルを取得
		const u32 index{ _free_handles[_size] };
		const u32 offset{ index * _descriptor_size };
		++_size;

		// ハンドルを設定
		descriptor_handle handle{};
		handle.cpu.ptr = _cpu_start.ptr + offset;
		if (is_shader_visible()) handle.gpu.ptr = _gpu_start.ptr + offset;

		// デバッグ情報を設定
		DEBUG_OP(handle.container = this);
		DEBUG_OP(handle.index = index);
		return handle;
	}

	/// @brief ディスクリプタの解放
	/// @param handle ディスクリプタハンドル
	void descriptor_heap::free(descriptor_handle& handle)
	{
		// ハンドルが有効か確認
		if (!handle.is_valid()) return;
		std::lock_guard lock{ _mutex };
		// ヒープがあるか確認
		assert(_heap && _size);
		assert(handle.container == this);
		assert(handle.cpu.ptr >= _cpu_start.ptr);
		assert((handle.cpu.ptr - _cpu_start.ptr) % _descriptor_size == 0);
		assert(handle.index < _capacity);
		// インデックスを取得
		const u32 index{ (u32)(handle.cpu.ptr - _cpu_start.ptr) / _descriptor_size };
		assert(handle.index == index);

		// 遅延解放
		const u32 frame_idx{ core::current_frame_index() };
		_deferred_free_indices[frame_idx].push_back(index);
		core::set_deferred_releases_flag();
		handle = {};
	}

	//_/_/_/_/_/_/_/_/ D3D12 TEXTURE _/_/_/_/_/_/_/_/
	/// @brief コンストラクタ
	/// @param info テクスチャ初期化情報
	d3d12_texture::d3d12_texture(d3d12_texture_init_info info)
	{
		// デバイスを取得
		auto* const device{ core::device() };
		assert(device);

		// クリア値を取得
		D3D12_CLEAR_VALUE* const clear_value
		{
			(info.desc &&
			(info.desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET ||
			info.desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
			? &info.clear_value : nullptr
		};

		// リソースを作成
		if (info.resource)
		{	// リソースがある場合
			assert(!info.heap);
			_resource = info.resource;
		}
		else if (info.heap && info.desc)
		{	// ヒープとリソース設定がある場合
			assert(!info.resource);
			DXCall(device->CreatePlacedResource(
				info.heap, info.allocation_info.Offset, info.desc,
				info.initial_state, clear_value, IID_PPV_ARGS(&_resource)));
		}
		else if (info.desc)
		{	// リソース設定がある場合
			assert(!info.heap && !info.resource);
			DXCall(device->CreateCommittedResource(
				&d3dx::heap_properties.default_heap, D3D12_HEAP_FLAG_NONE, info.desc,
				info.initial_state, clear_value, IID_PPV_ARGS(&_resource)));
		}

		// シェーダーリソースビューを作成
		assert(_resource);
		_srv = core::srv_heap().allocate();
		device->CreateShaderResourceView(_resource, info.srv_desc, _srv.cpu);
	}

	/// @brief 開放処理
	void d3d12_texture::release()
	{
		core::srv_heap().free(_srv);
		core::deferred_release(_resource);
	}

	//_/_/_/_/_/_/_/_/ RENDER TEXTURE _/_/_/_/_/_/_/_/

	/// @brief コンストラクタ
	/// @param info テクスチャ初期化情報
	d3d12_render_texture::d3d12_render_texture(d3d12_texture_init_info info)
		: _texture{ info }
	{
		// ミップレベルを取得
		assert(info.desc);
		_mip_count = resource()->GetDesc().MipLevels;
		assert(_mip_count && _mip_count <= d3d12_texture::max_mip);

		// レンダーターゲットビューを作成
		descriptor_heap& rtv_heap{ core::rtv_heap() };
		D3D12_RENDER_TARGET_VIEW_DESC desc{};
		desc.Format = info.desc->Format;
		desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipSlice = 0;

		auto* const device{ core::device() };
		assert(device);

		for (u32 i{ 0 }; i < _mip_count; ++i)
		{
			_rtv[i] = rtv_heap.allocate();
			device->CreateRenderTargetView(resource(), &desc, _rtv[i].cpu);
			++desc.Texture2D.MipSlice;
		}
	}

	/// @brief 開放処理
	void d3d12_render_texture::release()
	{
		for (u32 i{ 0 }; i < _mip_count; ++i)core::rtv_heap().free(_rtv[i]);
		_texture.release();
		_mip_count = 0;
	}

	//_/_/_/_/_/_/_/_/ DEPTH BUFFER _/_/_/_/_/_/_/_/

	/// @brief コンストラクタ
	/// @param info テクスチャ初期化情報
	d3d12_depth_buffer::d3d12_depth_buffer(d3d12_texture_init_info info)
	{
		// テクスチャを作成
		assert(info.desc);
		const DXGI_FORMAT dsv_format{ info.desc->Format };

		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
		if (info.desc->Format == DXGI_FORMAT_D32_FLOAT)
		{
			info.desc->Format = DXGI_FORMAT_R32_TYPELESS;
			srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
		}

		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MipLevels = 1;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.PlaneSlice = 0;
		srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;

		// テクスチャを設定
		assert(!info.srv_desc && !info.resource);
		info.srv_desc = &srv_desc;
		_texture = d3d12_texture(info);

		// デプスステンシルビューを作成
		D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
		dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
		dsv_desc.Format = dsv_format;
		dsv_desc.Texture2D.MipSlice = 0;

		_dsv = core::dsv_heap().allocate();

		auto* const device{ core::device() };
		assert(device);
		device->CreateDepthStencilView(resource(), &dsv_desc, _dsv.cpu);
	}

	/// @brief 開放処理
	void d3d12_depth_buffer::release()
	{
		core::dsv_heap().free(_dsv);
		_texture.release();
	}
}	// namespace dxforge::graphics::d3d12

