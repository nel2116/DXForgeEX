// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Upload.h]
// 作成日 : 2025/01/12
// 作成者 : 田中ミノル
// 概要 :
// アップロードヒープのサブモジュール
// 更新履歴
// 2025/01/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"

namespace dxforge::graphics::d3d12::upload
{
	class d3d12_upload_context
	{
	public:	// パブリック関数
		d3d12_upload_context(u32 aligned_size);
		DISABLE_COPY_AND_MOVE(d3d12_upload_context);
		~d3d12_upload_context() { assert(_frame_index == u32_invalid_id); }

		void end_upload();

		// ====== アクセサ ======
		[[nodiscard]] constexpr id3d12_graphics_command_list* const command_list() const { return _cmd_list; }
		[[nodiscard]] constexpr ID3D12Resource* const upload_buffer() const { return _upload_buffer; }
		[[nodiscard]] constexpr void* const cpu_address() const { return _cpu_address; }

	private:	// メンバ変数
		DEBUG_OP(d3d12_upload_context() = default);
		id3d12_graphics_command_list* _cmd_list{ nullptr };	// コマンドリスト
		ID3D12Resource* _upload_buffer{ nullptr };			// アップロードバッファ
		void* _cpu_address{ nullptr };						// CPUアドレス
		u32 _frame_index{ u32_invalid_id };					// フレームインデックス
	};

	bool initialize();
	void shutdown();
}	// namespace dxforge::graphics::d3d12::upload

