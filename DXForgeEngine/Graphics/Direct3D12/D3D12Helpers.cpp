// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Helpers.cpp]
// 作成日 : 2025/01/02
// 作成者 : 田中ミノル
// 概要 :
// Direct3D12のヘルパー関数
// 更新履歴
// 2025/01/02 新規作成
// 2025/01/12 D3D12Upload.hの追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12Helpers.h"
#include "D3D12Core.h"
#include "D3D12Upload.h"

namespace dxforge::graphics::d3d12::d3dx
{
	namespace
	{

	}	// 匿名名前空間

	/// @brief リソースを遷移
	/// @param cmd_list コマンドリスト
	/// @param resource リソース
	/// @param before 変更前の状態
	/// @param after 変更後の状態
	/// @param flags フラグ
	/// @param subresource サブリソース
	void transition_resource(id3d12_graphics_command_list* cmd_list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, D3D12_RESOURCE_BARRIER_FLAGS flags, u32 subresource)
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = flags;
		barrier.Transition.pResource = resource;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		barrier.Transition.Subresource = subresource;

		cmd_list->ResourceBarrier(1, &barrier);
	}

	/// @brief ルートシグネチャを作成
	/// @param desc ルートシグネチャ記述子
	/// @return ID3D12RootSignature* ルートシグネチャ
	ID3D12RootSignature* create_root_signature(const D3D12_ROOT_SIGNATURE_DESC1& desc)
	{
		// バージョン付きのルートシグネチャ記述子を作成
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_desc{};
		versioned_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		versioned_desc.Desc_1_1 = desc;

		// シリアライズ
		using namespace Microsoft::WRL;
		ComPtr<ID3DBlob> signature_blob{ nullptr };
		ComPtr<ID3DBlob> error_blob{ nullptr };
		HRESULT hr{ S_OK };
		if (FAILED(hr = D3D12SerializeVersionedRootSignature(&versioned_desc, &signature_blob, &error_blob)))
		{
			// エラーメッセージを出力
			DEBUG_OP(const char* error_msg{ error_blob ? (const char*)error_blob->GetBufferPointer() : "" });
			DEBUG_OP(OutputDebugStringA(error_msg));
			return nullptr;
		}

		// ルートシグネチャを作成
		ID3D12RootSignature* signature{ nullptr };
		DXCall(hr = core::device()->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&signature)));

		if (FAILED(hr))
		{	// 失敗した場合は解放
			core::release(signature);
		}

		// ルートシグネチャを返す
		return signature;
	}

	/// @brief パイプラインステートを作成
	/// @param desc パイプラインステートストリーム記述子
	/// @return ID3D12PipelineState* パイプラインステート
	ID3D12PipelineState* create_pipeline_state(D3D12_PIPELINE_STATE_STREAM_DESC desc)
	{
		assert(desc.pPipelineStateSubobjectStream && desc.SizeInBytes);
		ID3D12PipelineState* pso{ nullptr };
		DXCall(core::device()->CreatePipelineState(&desc, IID_PPV_ARGS(&pso)));
		assert(pso);
		return pso;
	}

	/// @brief パイプラインステートを作成
	/// @param stream パイプラインステートストリーム
	/// @param stream_size ストリームサイズ
	/// @return ID3D12PipelineState* パイプラインステート
	ID3D12PipelineState* create_pipeline_state(void* stream, u64 stream_size)
	{
		assert(stream && stream_size);
		D3D12_PIPELINE_STATE_STREAM_DESC desc{};
		desc.SizeInBytes = stream_size;
		desc.pPipelineStateSubobjectStream = stream;
		return create_pipeline_state(desc);
	}

	/// @brief バッファを作成
	/// @param data データ
	/// @param buffer_size バッファサイズ
	/// @param is_cpu_accessible CPUからアクセス可能か
	/// @param state リソース状態
	/// @param flags リソースフラグ
	/// @param heap ヒープ
	/// @param heap_offset ヒープオフセット
	/// @return ID3D12Resource* リソース
	ID3D12Resource* create_buffer(const void* data, u32 buffer_size, bool is_cpu_accessible, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags, ID3D12Heap* heap, u64 heap_offset)
	{
		assert(buffer_size);

		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0;
		desc.Width = buffer_size;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc = { 1,0 };
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = is_cpu_accessible ? D3D12_RESOURCE_FLAG_NONE : flags;

		// バッファはアップロードにのみ使用されるか、定数バッファ/UAVとして使用される。
		assert(desc.Flags == D3D12_RESOURCE_FLAG_NONE ||
			desc.Flags == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

		ID3D12Resource* resource{ nullptr };
		const D3D12_RESOURCE_STATES resource_state{ is_cpu_accessible ? D3D12_RESOURCE_STATE_GENERIC_READ : state };

		if (heap)
		{
			DXCall(core::device()->CreatePlacedResource(heap, heap_offset, &desc, resource_state, nullptr, IID_PPV_ARGS(&resource)));
		}
		else
		{
			DXCall(core::device()->CreateCommittedResource(is_cpu_accessible ? &heap_properties.upload_heap : &heap_properties.default_heap, D3D12_HEAP_FLAG_NONE, &desc, resource_state, nullptr, IID_PPV_ARGS(&resource)));
		}

		if (data)
		{
			// 後で変更できるようにしたい初期データがある場合は、is_cpu_accessibleをtrueに設定する。
			// GPUが使用するデータを一度だけアップロードしたい場合は、is_cpu_accessibleをfalseに設定します。
			if (is_cpu_accessible)
			{
				// NOTE: 範囲のBeginフィールドとEndフィールドに0がセットされ、
				//		CPUがデータを読み込んでいない（つまり書き込み専用）ことを示す。
				const D3D12_RANGE range{};
				void* cpu_address{ nullptr };
				DXCall(resource->Map(0, &range, reinterpret_cast<void**>(&cpu_address)));
				assert(cpu_address);
				memcpy(cpu_address, data, buffer_size);
				resource->Unmap(0, nullptr);
			}
			else
			{
				upload::d3d12_upload_context context{ buffer_size };
				memcpy(context.cpu_address(), data, buffer_size);
				context.command_list()->CopyResource(resource, context.upload_buffer());
				context.end_upload();
			}
		}

		assert(resource);
		return resource;
	}
}	// namespace dxforge::graphics::d3d12::d3dx
