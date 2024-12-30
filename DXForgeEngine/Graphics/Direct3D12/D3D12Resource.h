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
}	// namespace dxforge::graphics::d3d12





