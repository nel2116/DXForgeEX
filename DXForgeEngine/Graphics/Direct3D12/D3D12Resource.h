// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Resource.h]
// 作成日 : 2024/12/30
// 作成者 : 田中ミノル
// 概要 :
// Direct3D12のリソース管理
// 更新履歴
// 2024/12/30 新規作成
// 2025/01/02 d3d12_textureクラスの追加
// 2025/01/02 d3d12_render_textureクラスの追加
// 2025/01/02 d3d12_depth_bufferクラスの追加
// 2025/01/14 d3d12_bufferクラスの追加
// 2025/01/14 constant_bufferクラスの追加
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
		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};												// CPUディスクリプタハンドル
		D3D12_GPU_DESCRIPTOR_HANDLE gpu{};												// GPUディスクリプタハンドル
		u32 index{ u32_invalid_id };													// インデックス

		[[nodiscard]] constexpr bool is_valid() const { return cpu.ptr != 0; }			// 有効かどうか
		[[nodiscard]] constexpr bool is_shader_visible() const { return gpu.ptr != 0; }	// シェーダーから見えるかどうか

#ifdef _DEBUG
		friend class descriptor_heap;													// デバッグ用にフレンド化
		descriptor_heap* container{ nullptr };											// ディスクリプタヒープ

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
		[[nodiscard]] constexpr D3D12_DESCRIPTOR_HEAP_TYPE type() const { return _type; }
		[[nodiscard]] constexpr D3D12_CPU_DESCRIPTOR_HANDLE cpu_start() const { return _cpu_start; }
		[[nodiscard]] constexpr D3D12_GPU_DESCRIPTOR_HANDLE gpu_start() const { return _gpu_start; }
		[[nodiscard]] constexpr ID3D12DescriptorHeap* const heap() const { return _heap; }
		[[nodiscard]] constexpr u32 capacity() const { return _capacity; }
		[[nodiscard]] constexpr u32 size() const { return _size; }
		[[nodiscard]] constexpr u32 descriptor_size() const { return _descriptor_size; }
		[[nodiscard]] constexpr bool is_shader_visible() const { return _gpu_start.ptr != 0; }

	private:
		ID3D12DescriptorHeap* _heap;
		D3D12_CPU_DESCRIPTOR_HANDLE         _cpu_start{};									// CPUディスクリプタハンドルの開始位置
		D3D12_GPU_DESCRIPTOR_HANDLE         _gpu_start{};									// GPUディスクリプタハンドルの開始位置
		std::unique_ptr<u32[]>              _free_handles{};								// 空きハンドル
		utl::vector<u32>                    _deferred_free_indices[frame_buffer_count]{};	// 遅延解放インデックス
		std::mutex                          _mutex{};										// ミューテックス
		u32                                 _capacity{ 0 };									// 容量
		u32                                 _size{ 0 };										// サイズ
		u32                                 _descriptor_size{};								// ディスクリプタサイズ
		const D3D12_DESCRIPTOR_HEAP_TYPE    _type{};										// ヒープの種類
	};

	struct d3d12_buffer_init_info
	{
		ID3D12Heap1* heap{ nullptr };														// ヒープ
		const void* data{ nullptr };														// データ
		D3D12_RESOURCE_ALLOCATION_INFO1 allocation_info{ };									// リソースのアロケーション情報
		D3D12_RESOURCE_STATES initial_state{};												// 初期状態
		D3D12_RESOURCE_FLAGS flags{ D3D12_RESOURCE_FLAG_NONE };								// フラグ
		u32 size{ 0 };																		// サイズ
		u32 alignment{ 0 };																	// アライメント
	};

	class d3d12_buffer
	{
	public:		// パブリック関数
		d3d12_buffer() = default;
		explicit d3d12_buffer(d3d12_buffer_init_info info, bool is_cpu_accessible);
		DISABLE_COPY(d3d12_buffer);
		constexpr d3d12_buffer(d3d12_buffer&& o)
			: _buffer{ o._buffer }, _gpu_address{ o._gpu_address }, _size{ o._size }
		{
			o.reset();
		}

		constexpr d3d12_buffer& operator=(d3d12_buffer&& o)
		{
			assert(this != &o);
			if (this != &o)
			{
				release();
				move(o);
			}
			return *this;
		}

		~d3d12_buffer() { release(); }

		void release();
		// ------ アクセサ ------
		[[nodiscard]] constexpr ID3D12Resource* const buffer() const { return _buffer; }
		[[nodiscard]] constexpr D3D12_GPU_VIRTUAL_ADDRESS gpu_address() const { return _gpu_address; }
		[[nodiscard]] constexpr u32 size() const { return _size; }

	private:	// プライベート関数

		constexpr void move(d3d12_buffer& o)
		{
			_buffer = o._buffer;
			_gpu_address = o._gpu_address;
			_size = o._size;
			o.reset();
		}

		constexpr void reset()
		{
			_buffer = nullptr;
			_gpu_address = 0;
			_size = 0;
		}

	private:	// メンバ変数
		ID3D12Resource* _buffer{ nullptr };												// バッファ
		D3D12_GPU_VIRTUAL_ADDRESS _gpu_address{ 0 };									// GPUアドレス
		u32 _size{ 0 };																	// サイズ
	};

	class constant_buffer
	{
	public:		// パブリック関数
		constant_buffer() = default;
		explicit constant_buffer(d3d12_buffer_init_info info);
		DISABLE_COPY_AND_MOVE(constant_buffer);
		~constant_buffer() { release(); }

		void release()
		{
			_buffer.release();
			_cpu_address = nullptr;
			_cpu_offset = 0;
		}

		constexpr void clear() { _cpu_offset = 0; }
		[[nodiscard]] u8* const allocate(u32 size);

		template<typename T>
		[[nodiscard]] T* allocate()
		{
			return (T* const)(allocate(sizeof(T)));
		}

		// ------ アクセサ ------
		[[nodiscard]] constexpr ID3D12Resource* const buffer() const { return _buffer.buffer(); }
		[[nodiscard]] constexpr D3D12_GPU_VIRTUAL_ADDRESS gpu_address() const { return _buffer.gpu_address(); }
		[[nodiscard]] constexpr u32 size() const { return _buffer.size(); }
		[[nodiscard]] constexpr u8* cpu_address() const { return _cpu_address; }
		[[nodiscard]] constexpr u32 offset() const { return _cpu_offset; }

		template<typename T>
		[[nodiscard]] constexpr D3D12_GPU_VIRTUAL_ADDRESS gpu_address(T* const allocation)
		{
			std::lock_guard lock{ _mutex };
			assert(_cpu_address);
			if (!_cpu_address)return {};
			const u8* const address{ (const u8* const)allocation };
			assert(address <= _cpu_address + _cpu_offset);
			assert(address >= _cpu_address);
			const u64 offset{ (u64)(address - _cpu_address) };
			return _buffer.gpu_address() + offset;
		}

		[[nodiscard]] constexpr static d3d12_buffer_init_info get_default_init_info(u32 size)
		{
			assert(size);
			d3d12_buffer_init_info info{};
			info.size = size;
			info.alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
			return info;
		}

	private:	// メンバ変数
		d3d12_buffer _buffer{};															// バッファ
		u8* _cpu_address{ nullptr };													// CPUアドレス
		u32 _cpu_offset{ 0 };															// CPUオフセット
		std::mutex _mutex{};															// ミューテックス
	};

	class uav_clearble_buffer
	{
	public:		// パブリック関数
		uav_clearble_buffer() = default;
		explicit uav_clearble_buffer(const d3d12_buffer_init_info& info);
		DISABLE_COPY(uav_clearble_buffer);
		constexpr uav_clearble_buffer(uav_clearble_buffer&& o)
			: _buffer{ std::move(o._buffer) }, _uav{ o._uav }, _uav_shader_visible{ o._uav_shader_visible }
		{
			o.reset();
		}

		constexpr uav_clearble_buffer& operator=(uav_clearble_buffer&& o)
		{
			assert(this != &o);
			if (this != &o)
			{
				release();
				move(o);
			}
			return *this;
		}

		~uav_clearble_buffer() { release(); }

		void release();

		void clear_uav(id3d12_graphics_command_list* const cmd_list, const u32* const values) const
		{
			assert(buffer());
			assert(_uav.is_valid() && _uav_shader_visible.is_valid() && _uav_shader_visible.is_shader_visible());
			cmd_list->ClearUnorderedAccessViewUint(_uav_shader_visible.gpu, _uav.cpu, buffer(), values, 0, nullptr);
		}

		void clear_uav(id3d12_graphics_command_list* const cmd_list, const f32* const values) const
		{
			assert(buffer());
			assert(_uav.is_valid() && _uav_shader_visible.is_valid() && _uav_shader_visible.is_shader_visible());
			cmd_list->ClearUnorderedAccessViewFloat(_uav_shader_visible.gpu, _uav.cpu, buffer(), values, 0, nullptr);
		}

		[[nodiscard]] constexpr ID3D12Resource* buffer() const { return _buffer.buffer(); }
		[[nodiscard]] constexpr D3D12_GPU_VIRTUAL_ADDRESS gpu_address() const { return _buffer.gpu_address(); }
		[[nodiscard]] constexpr u32 size() const { return _buffer.size(); }
		[[nodiscard]] constexpr descriptor_handle uav() const { return _uav; }
		[[nodiscard]] constexpr descriptor_handle uav_shader_visible() const { return _uav_shader_visible; }

		[[nodiscard]] constexpr static d3d12_buffer_init_info get_default_init_info(u32 size)
		{
			assert(size);
			d3d12_buffer_init_info info{};
			info.size = size;
			info.alignment = sizeof(math::v4);
			info.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			return info;
		}

	private:	// プライベート関数
		constexpr void move(uav_clearble_buffer& o)
		{
			_buffer = std::move(o._buffer);
			_uav = o._uav;
			_uav_shader_visible = o._uav_shader_visible;
			o.reset();
		}

		constexpr void reset()
		{
			_uav = {};
			_uav_shader_visible = {};
		}

	private:	// メンバ変数
		d3d12_buffer _buffer{};
		descriptor_handle _uav{};
		descriptor_handle _uav_shader_visible{};
	};


	struct d3d12_texture_init_info
	{
		ID3D12Heap1* heap{ nullptr };													// ヒープ
		ID3D12Resource* resource{ nullptr };											// リソース
		D3D12_SHADER_RESOURCE_VIEW_DESC* srv_desc{ nullptr };							// シェーダーリソースビューの設定
		D3D12_RESOURCE_DESC* desc{ nullptr };											// リソースの設定
		D3D12_RESOURCE_ALLOCATION_INFO1 allocation_info{ };								// リソースのアロケーション情報
		D3D12_RESOURCE_STATES initial_state{};											// 初期状態
		D3D12_CLEAR_VALUE clear_value{};												// クリア値
	};

	class d3d12_texture
	{
	public:		// 定数定義
		constexpr static u32 max_mips{ 14 };	// 16K解像度までサポート

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
		[[nodiscard]] constexpr ID3D12Resource* const resource() const { return _resource; }
		[[nodiscard]] constexpr descriptor_handle srv() const { return _srv; }

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
		ID3D12Resource* _resource{ nullptr };											// リソース
		descriptor_handle _srv;															// シェーダーリソースビュー
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

		// ------ アクセサ ------
		[[nodiscard]] constexpr u32 mip_count() const { return _mip_count; }
		[[nodiscard]] constexpr D3D12_CPU_DESCRIPTOR_HANDLE rtv(u32 mip_index) const { assert(mip_index < _mip_count); return _rtv[mip_index].cpu; }
		[[nodiscard]] constexpr descriptor_handle srv() const { return _texture.srv(); }
		[[nodiscard]] constexpr ID3D12Resource* const resource() const { return _texture.resource(); }

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
		d3d12_texture _texture;															// テクスチャ
		descriptor_handle _rtv[d3d12_texture::max_mips]{};								// レンダーターゲットビュー
		u32 _mip_count{ 0 };															// ミップレベル
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

		// ------ アクセサ ------
		[[nodiscard]] constexpr D3D12_CPU_DESCRIPTOR_HANDLE dsv() const { return _dsv.cpu; }
		[[nodiscard]] constexpr descriptor_handle srv() const { return _texture.srv(); }
		[[nodiscard]] constexpr ID3D12Resource* const resource() const { return _texture.resource(); };

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
		d3d12_texture _texture{};														// テクスチャ
		descriptor_handle _dsv{};														// デプスステンシルビュー
	};

}	// namespace dxforge::graphics::d3d12





