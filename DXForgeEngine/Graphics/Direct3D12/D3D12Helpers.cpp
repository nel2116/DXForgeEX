// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Helpers.cpp]
// 作成日 : 2025/01/02
// 作成者 : 田中ミノル
// 概要 :
// Direct3D12のヘルパー関数
// 更新履歴
// 2025/01/02 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12Helpers.h"
#include "D3D12Core.h"

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
			DEBUG_OP(const char* error_msg{ error_blob ? static_cast<const char*>(error_blob->GetBufferPointer()) : "" });
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
}
