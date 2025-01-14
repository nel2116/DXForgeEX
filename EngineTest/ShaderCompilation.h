// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ShaderCompilation.h]
// 作成日 : 2025/01/07
// 作成者 : 田中ミノル
// 概要
// 　シェーダーのコンパイルを行う
// 更新履歴
// 2025/01/07 新規作成
// 2025/01/13 ユーザー定義のシェーダーをコンパイルする処理を追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"

struct shader_type {
	enum type : u32 {
		vertex = 0,
		hull,
		domain,
		geometry,
		pixel,
		compute,
		amplification,
		mesh,

		count
	};
};

struct shader_file_info
{
	const char* file_name;
	const char* function;
	shader_type::type   type;
};

std::unique_ptr<u8[]> compile_shader(shader_file_info info, const char* file_path);
bool compile_shaders();