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
		ID3D12Device* const device{ core::device() };
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

	void descriptor_heap::release()
	{
		assert(!_size);
		core::deferred_release(_heap);
	}

	void descriptor_heap::process_deferred_free(u32 frame_index)
	{
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

		descriptor_handle handle{};
		handle.cpu.ptr = _cpu_start.ptr + offset;
		if (is_shader_visible()) handle.gpu.ptr = _gpu_start.ptr + offset;

		// デバッグ情報を設定
		DEBUG_OP(handle.container = this);
		DEBUG_OP(handle.index = index);
		return handle;
	}

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
}	// namespace dxforge::graphics::d3d12

