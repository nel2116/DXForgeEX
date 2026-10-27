// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ShaderCompilation.h]
// 作成日 : 2025/01/07
// 作成者 : 田中ミノル
// 概要
// 　シェーダーのコンパイルを行う
// 更新履歴
// 2025/01/07 新規作成
// 2025/01/13 ユーザー定義のシェーダーをコンパイルする処理を追加
// 2026/10/27 EngineTestからDXForgeEngineDLLへ移動し、メモリ上のソースをコンパイルする関数を追加(Primal Day 076)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "Graphics/Renderer.h"

struct shader_file_info
{
	const char* file_name;
	const char* function;
	dxforge::graphics::shader_type::type type;
};

// コンパイルしたシェーダーを content::compiled_shader の形式(u64 バイトコードのサイズ, ハッシュ, バイトコード)で返す。
// include_errors_and_disassembly が true のときは、その後ろに u64 エラーのサイズ, u64 逆アセンブルのサイズ,
// エラー, 逆アセンブルを続ける。このときは、コンパイルに失敗してもエラーを返すため、バイトコードのサイズが0の
// バッファを返すことがある。失敗の理由がわからない場合(DXCを読み込めないなど)は nullptr を返す。
std::unique_ptr<u8[]> compile_shader(shader_file_info info, const u8* code, u32 code_size, dxforge::utl::vector<std::wstring>& extra_args,
	bool include_errors_and_disassembly = false);
std::unique_ptr<u8[]> compile_shader(shader_file_info info, const char* file_path, dxforge::utl::vector<std::wstring>& extra_args,
	bool include_errors_and_disassembly = false);
bool compile_shaders();

// UTF-8 の文字列をワイド文字列に変換する。
std::wstring to_wstring(const char* c);
