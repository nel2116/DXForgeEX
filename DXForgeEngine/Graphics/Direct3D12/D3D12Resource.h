// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Resource.h]
// 作成日 : 2024/12/30
// 作成者 : 田中ミノル
// 概要 :
// Direct3D12のリソース管理
// 更新履歴
// 2024/12/30 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"


namespace dxforge::graphics::d3d12
{
	// ====== 構造体定義 ======
	/// @brief ディスクリプタハンドル
	struct descriptor_handle
	{
		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};									// CPUディスクリプタハンドル
		D3D12_GPU_DESCRIPTOR_HANDLE gpu{};									// GPUディスクリプタハンドル

		constexpr bool is_valid() const { return cpu.ptr != 0; }			// 有効かどうか
		constexpr bool is_shader_visible() const { return gpu.ptr != 0; }	// シェーダーから見えるかどうか

#ifdef _DEBUG
		friend class descriptor_heap;										// デバッグ用にフレンド化
		descriptor_heap* container{ nullptr };								// ディスクリプタヒープ
		u32 index{ u32_invalid_id };										// インデックス

#endif // _DEBUG
	};

	class descriptor_heap
	{
	public:
		// ------ コンストラクタ ------
		explicit descriptor_heap(D3D12_DESCRIPTOR_HEAP_TYPE type) : _type{ type } {}
		DISABLE_COPY_AND_MOVE(descriptor_heap);
		~descriptor_heap() { assert(!_heap); }

		// ------ 関数 ------
		/// @brief ディスクリプタヒープの初期化
		bool initialize(u32 capacity, bool is_shader_visible);
		/// @brief ディスクリプタヒープの解放
		void release();
		/// @brief 遅延解放フラグを設定
		/// @param frame_index フレームインデックス
		void process_deferred_free(u32 frame_index);

		/// @brief ディスクリプタの割り当て
		[[nodiscard]] descriptor_handle allocate();
		/// @brief ディスクリプタの解放
		void free(descriptor_handle& handle);

		// ------ アクセサ ------
		constexpr D3D12_DESCRIPTOR_HEAP_TYPE type() const { return _type; }
		constexpr D3D12_CPU_DESCRIPTOR_HANDLE cpu_start() const { return _cpu_start; }
		constexpr D3D12_GPU_DESCRIPTOR_HANDLE gpu_start() const { return _gpu_start; }
		constexpr ID3D12DescriptorHeap* const heap() const { return _heap; }
		constexpr u32 capacity() const { return _capacity; }
		constexpr u32 size() const { return _size; }
		constexpr u32 descriptor_size() const { return _descriptor_size; }
		constexpr bool is_shader_visible() const { return _gpu_start.ptr != 0; }

	private:
		ID3D12DescriptorHeap* _heap;										// ヒープ
		D3D12_CPU_DESCRIPTOR_HANDLE _cpu_start{};							// CPUディスクリプタヒープの開始アドレス
		D3D12_GPU_DESCRIPTOR_HANDLE _gpu_start{};							// GPUディスクリプタヒープの開始アドレス
		std::unique_ptr<u32[]> _free_handles{};								// 空きハンドル
		utl::vector<u32> _deferred_free_indices[frame_buffer_count]{};		// 遅延解放インデックス
		std::mutex _mutex{};												// ミューテックス
		u32 _capacity{ 0 };													// 容量
		u32 _size{ 0 };														// サイズ
		u32 _descriptor_size{};												// ディスクリプタサイズ
		const D3D12_DESCRIPTOR_HEAP_TYPE _type{};							// ヒープの種類
	};

	struct d3d12_texture_init_info
	{
		ID3D12Heap1* heap{ nullptr };										// ヒープ
		ID3D12Resource* resource{ nullptr };								// リソース
		D3D12_SHADER_RESOURCE_VIEW_DESC* srv_desc{ nullptr };				// シェーダーリソースビューの設定
		D3D12_RESOURCE_DESC* desc{ nullptr };								// リソースの設定
		D3D12_RESOURCE_ALLOCATION_INFO1 allocation_info{ };					// リソースのアロケーション情報
		D3D12_RESOURCE_STATES initial_state{};								// 初期状態
		D3D12_CLEAR_VALUE clear_value{};									// クリア値
	};

	class d3d12_texture
	{
	public:		// 定数定義
		constexpr static u32 max_mip{ 14 };	// 16K解像度までサポート

	public:		// パブリック関数
		// ------ コンストラクタ ------
		d3d12_texture() = default;
		explicit d3d12_texture(d3d12_texture_init_info info);
		DISABLE_COPY(d3d12_texture);
		constexpr d3d12_texture(d3d12_texture&& o)
			: _resource{ o._resource }, _srv{ o._srv }
		{
			o.reset();
		}

		constexpr d3d12_texture& operator=(d3d12_texture&& o)
		{
			assert(this != &o);
			if (this != &o)
			{
				release();
				move(o);
			}
			return *this;
		}

		~d3d12_texture() { release(); }

		void release();

		// ------ アクセサ ------
		constexpr ID3D12Resource* const resource() const { return _resource; }
		constexpr descriptor_handle srv() const { return _srv; }

	private:	// プライベート関数
		constexpr void move(d3d12_texture& o)
		{
			_resource = o._resource;
			_srv = o._srv;
			o.reset();
		}

		constexpr void reset()
		{
			_resource = nullptr;
			_srv = {};
		}

	private:	// メンバ変数
		ID3D12Resource* _resource{ nullptr };								// リソース
		descriptor_handle _srv;												// シェーダーリソースビュー
	};

	class d3d12_render_texture
	{
	public:		// パブリック関数
		// ------ コンストラクタ ------
		d3d12_render_texture() = default;
		explicit d3d12_render_texture(d3d12_texture_init_info info);
		DISABLE_COPY(d3d12_render_texture);
		constexpr d3d12_render_texture(d3d12_render_texture&& o)
			: _texture{ std::move(o._texture) }, _mip_count{ o._mip_count }
		{
			for (u32 i{ 0 }; i < _mip_count; ++i)_rtv[i] = o._rtv[i];
			o.reset();
		}

		constexpr d3d12_render_texture& operator=(d3d12_render_texture&& o)
		{
			assert(this != &o);
			if (this != &o)
			{
				release();
				move(o);
			}
			return *this;
		}

		~d3d12_render_texture() { release(); }

		void release();
		constexpr u32 mip_count() const { return _mip_count; }
		constexpr D3D12_CPU_DESCRIPTOR_HANDLE rtv(u32 mip_index) const { assert(mip_index < _mip_count); return _rtv[mip_index].cpu; }
		constexpr descriptor_handle srv() const { return _texture.srv(); }
		constexpr ID3D12Resource* const resource() const { return _texture.resource(); }

	private:	// プライベート関数
		constexpr void move(d3d12_render_texture& o)
		{
			_texture = std::move(o._texture);
			_mip_count = o._mip_count;
			for (u32 i{ 0 }; i < _mip_count; ++i)_rtv[i] = o._rtv[i];
			o.reset();
		}

		constexpr void reset()
		{
			for (u32 i{ 0 }; i < _mip_count; ++i)_rtv[i] = {};
			_mip_count = 0;
		}

	private:	// メンバ変数
		d3d12_texture _texture;												// テクスチャ
		descriptor_handle _rtv[d3d12_texture::max_mip]{};					// レンダーターゲットビュー
		u32 _mip_count{ 0 };												// ミップレベル
	};

	class d3d12_depth_buffer
	{
	public:		// パブリック関数
		// ------ コンストラクタ ------
		d3d12_depth_buffer() = default;
		explicit d3d12_depth_buffer(d3d12_texture_init_info info);
		DISABLE_COPY(d3d12_depth_buffer);
		constexpr d3d12_depth_buffer(d3d12_depth_buffer&& o)
			: _texture{ std::move(o._texture) }, _dsv{ o._dsv }
		{
			o._dsv = {};
		}

		constexpr d3d12_depth_buffer& operator=(d3d12_depth_buffer&& o)
		{
			assert(this != &o);
			if (this != &o)
			{
				_texture = std::move(o._texture);
				_dsv = o._dsv;
				o._dsv = {};
			}
			return *this;
		}

		~d3d12_depth_buffer() { release(); }

		void release();
		constexpr D3D12_CPU_DESCRIPTOR_HANDLE dsv() const { return _dsv.cpu; }
		constexpr descriptor_handle srv() const { return _texture.srv(); }
		constexpr ID3D12Resource* const resource() const { return _texture.resource(); };

	private:	// プライベート関数
		constexpr void move(d3d12_depth_buffer& o)
		{
			_texture = std::move(o._texture);
			_dsv = o._dsv;
			o.reset();
		}
		constexpr void reset()
		{
			_dsv = {};
		}

	private:	// メンバ変数
		d3d12_texture _texture{};											// テクスチャ
		descriptor_handle _dsv{};											// デプスステンシルビュー
	};

}	// namespace dxforge::graphics::d3d12





