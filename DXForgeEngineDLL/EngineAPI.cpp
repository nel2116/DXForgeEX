// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EngineAPI.cpp]
// 作成日 : 2024/11/14
// 作成者 : 田中ミノル
// 概要 :
// エンジンのAPI
// 更新履歴
// 2024/11/14 新規作成
// 2026/10/27 シェーダーのコンパイルとシェーダーグループの登録・削除を追加(Primal Day 076)
// 2026/10/30 エディタからアセットのリソースを作る・破棄する CreateResource / DestroyResource を追加(Primal Day 077, #30)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Common.h"
#include "CommonHeaders.h"
#include "Components/Script.h"
#include "Graphics/Renderer.h"
#include "Platform/PlatformType.h"
#include "Platform/Platform.h"
#include "Content/ContentToEngine.h"
#include "Utilities/IOStream.h"
#include "ShaderCompilation.h"
#include "ResourceUpload.h"

#ifdef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <atlsafe.h>
#include <string_view>

using namespace dxforge;

namespace
{
	HMODULE game_code_dll{ nullptr };
	using _get_script_creater = script::detail::script_creator(*)(size_t);
	_get_script_creater get_script_creator{ nullptr };
	using _get_script_names = LPSAFEARRAY(*)(void);
	_get_script_names get_script_names{ nullptr };
	utl::vector<graphics::render_surface> surfaces;
} // 匿名名前空間

EDITOR_INTERFACE u32 LoadGameCodeDll(const char* dll_path)
{
	if (game_code_dll) return FALSE;
	game_code_dll = LoadLibraryA(dll_path);
	assert(game_code_dll);

	get_script_creator = (_get_script_creater)GetProcAddress(game_code_dll, "get_script_creator");
	get_script_names = (_get_script_names)GetProcAddress(game_code_dll, "get_script_names");

	return (game_code_dll && get_script_creator && get_script_names) ? TRUE : FALSE;
}

EDITOR_INTERFACE u32 UnloadGameCodeDll()
{
	if (!game_code_dll) return FALSE;
	assert(game_code_dll);
	[[maybe_unused]] int result{ FreeLibrary(game_code_dll) };
	assert(result);
	game_code_dll = nullptr;
	return TRUE;
}

EDITOR_INTERFACE script::detail::script_creator GetScriptCreater(const char* name)
{
	return(game_code_dll && get_script_creator) ? get_script_creator(script::detail::string_hash()(name)) : nullptr;
}

EDITOR_INTERFACE LPSAFEARRAY GetScriptNames()
{
	return (game_code_dll && get_script_names) ? get_script_names() : nullptr;
};

EDITOR_INTERFACE u32 CreateRenderSurface(HWND host, s32 width, s32 height)
{
	assert(host);
	platform::window_init_info info{ nullptr,host,nullptr,0,0,(u32)width,(u32)height };
	graphics::render_surface surface{ platform::create_window(&info),{} };
	assert(surface.window.is_valid());
	surfaces.emplace_back(surface);
	return (u32)surfaces.size() - 1;
}

EDITOR_INTERFACE void RemoveRenderSurface(u32 id)
{
	assert(id < surfaces.size());
	platform::remove_window(surfaces[id].window.get_id());
}

EDITOR_INTERFACE intptr_t GetWindowHandle(u32 id)
{
	assert(id < surfaces.size());
	return reinterpret_cast<intptr_t>(surfaces[id].window.handle());
}

EDITOR_INTERFACE void ResizeRenderSurface(u32 id)
{
	assert(id < surfaces.size());
	surfaces[id].window.resize(0, 0);
}

// エディタのアセットの PackForEngine() のデータから、エンジンのリソースを作る(Primal Day 077)。
// type は content::asset_type::type(エディタの AssetType と同じ値)。material、mesh、texture だけを扱う。
// 作れなければ id::invalid_id。マテリアルのデータの並びは ResourceUpload.h を参照。
// NOTE: Primal Day 077 は id::invalid_id を返すだけで、content::create_resource を呼ぶのは Day 078 から。
//       テストで確かめられるよう、ここでは Day 078 の形まで入れる(エンジンの初期化 InitializeEngine は #31)。
EDITOR_INTERFACE id::id_type CreateResource(const u8* data, u32 type)
{
	return editor::create_resource(data, type);
}

EDITOR_INTERFACE void DestroyResource(id::id_type id, u32 type)
{
	editor::destroy_resource(id, type);
}
namespace
{
	// エディタの EngineAPIStructs.ShaderData と同じ並び
	struct shader_data
	{
		u32 type;
		u32 code_size;
		u32 byte_code_size;
		u32 errors_size;
		u32 assembly_size;
		u32 hash_size;
		u8* code;
		u8* byte_code_error_assembly_hash;
		const char* function_name;	// UTF-8
		const char* extra_args;		// UTF-8。引数を ; で区切ったもの
	};

	// エディタの EngineAPIStructs.ShaderGroupData と同じ並び
	struct shader_group_data
	{
		u32 type;
		u32 count;
		u32 data_size;
		u8* data;
	};

	utl::vector<std::wstring> split_args(const char* args)
	{
		utl::vector<std::wstring> result{};
		if (!args) return result;

		std::string_view rest{ args };
		while (!rest.empty())
		{
			const size_t end{ rest.find(';') };
			const std::string arg{ rest.substr(0, end) };
			if (!arg.empty()) result.emplace_back(to_wstring(arg.c_str()));
			if (end == std::string_view::npos) break;
			rest.remove_prefix(end + 1);
		}

		return result;
	}
} // 匿名名前空間

// コンパイル済みのシェーダーを、キーごとに1つのシェーダーグループとしてエンジンに登録する(Primal Day 076)。
EDITOR_INTERFACE id::id_type AddShaderGroup(shader_group_data* data)
{
	if (!data || data->type >= graphics::shader_type::count || !data->count || !data->data_size || !data->data) return id::invalid_id;
	const u32 count{ data->count };

	// data->data =
	// {
	//    u32 keys[count];
	//    struct {
	//      u64 bytecode_length;
	//      u8  hash[hash_length];
	//      u8  bytecode[bytecode_length];
	//    } blocks[count];
	// }
	const u8* const end{ data->data + data->data_size };
	if ((u64)count * sizeof(u32) > data->data_size) return id::invalid_id;
	utl::blob_stream_reader blob{ data->data };
	const u32* const keys{ (const u32*)blob.position() };
	blob.skip(count * sizeof(u32));

	utl::vector<const u8*> shader_pointers(count);
	for (u32 i{ 0 }; i < count; ++i)
	{
		if (blob.position() + sizeof(u64) > end) return id::invalid_id;
		const u64 byte_code_length{ *(const u64*)blob.position() };
		const u64 block_size{ sizeof(u64) + content::compiled_shader::hash_length + byte_code_length };
		if (!byte_code_length || block_size > (u64)(end - blob.position())) return id::invalid_id;
		shader_pointers[i] = blob.position();
		blob.skip(block_size);
	}

	if (blob.position() != end) return id::invalid_id;

	return content::add_shader_group(shader_pointers.data(), count, keys);
}

EDITOR_INTERFACE void RemoveShaderGroup(id::id_type id)
{
	if (!id::is_valid(id)) return;
	content::remove_shader_group(id);
}

// エディタから渡されたシェーダーのソースをコンパイルする(Primal Day 076)。
// コンパイルできたら TRUE。失敗しても、DXCのエラーがあれば byte_code_size = 0 でエラーを返す。
// 返すバッファ(byte_code_error_assembly_hash)は CoTaskMemAlloc で確保するので、呼び出し側が CoTaskMemFree で解放する。
// 並び: バイトコード, エラー, 逆アセンブル, ハッシュ
EDITOR_INTERFACE u32 CompileShader(shader_data* data)
{
	if (!data) return FALSE;
	data->byte_code_size = data->errors_size = data->assembly_size = data->hash_size = 0;
	data->byte_code_error_assembly_hash = nullptr;
	if (!data->code || !data->code_size || !data->function_name || !*data->function_name || data->type >= graphics::shader_type::count) return FALSE;

	shader_file_info info{};
	info.function = data->function_name;
	info.type = (graphics::shader_type::type)data->type;

	utl::vector<std::wstring> extra_args{ split_args(data->extra_args) };

	std::unique_ptr<u8[]> compiled_shader{ compile_shader(info, data->code, data->code_size, extra_args, true) };
	if (!compiled_shader) return FALSE;

	u64 byte_code_size{ 0 };
	u64 errors_size{ 0 };
	u64 assembly_size{ 0 };
	constexpr u64 hash_size{ content::compiled_shader::hash_length };
	{
		utl::blob_stream_reader blob{ compiled_shader.get() };
		byte_code_size = blob.read<u64>();
		blob.skip(hash_size + byte_code_size);
		errors_size = blob.read<u64>();
		assembly_size = blob.read<u64>();
	}

	const u64 buffer_size{ byte_code_size + errors_size + assembly_size + hash_size };
	if (buffer_size > UINT32_MAX) return FALSE;
	u8* const buffer{ (u8*)CoTaskMemAlloc(buffer_size) };
	if (!buffer) return FALSE;

	{
		utl::blob_stream_reader blob{ compiled_shader.get() };
		blob.skip(sizeof(u64)); // バイトコードのサイズを飛ばす
		blob.read(&buffer[buffer_size - hash_size], hash_size);
		blob.read(buffer, byte_code_size);
		blob.skip(2 * sizeof(u64)); // エラーと逆アセンブルのサイズを飛ばす
		blob.read(&buffer[byte_code_size], errors_size + assembly_size);
	}

	data->byte_code_error_assembly_hash = buffer;
	data->byte_code_size = (u32)byte_code_size;
	data->errors_size = (u32)errors_size;
	data->assembly_size = (u32)assembly_size;
	data->hash_size = byte_code_size ? (u32)hash_size : 0;

	return byte_code_size ? TRUE : FALSE;
}
