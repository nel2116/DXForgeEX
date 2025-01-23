// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [TextureImpoter.cpp]
// 作成日 : 2025/01/23
// 作成者 : 田中ミノル
// 概要 :
// テクスチャのインポートを行う
// 更新履歴
// 2025/01/23 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "ToolsCommon.h"
#include <DirectXTex.h>

using namespace DirectX;
namespace dxforge::tools
{
	namespace
	{
		struct import_error
		{
			enum error_code : u32
			{
				succeeded = 0,
				unknown,
				compress,
				decompress,
				load,
				mipmap_generation,
				max_size_exceeded,
				size_mismatch,
				format_mismatch,
				file_not_found,
			};
		};

		struct texture_dimension
		{
			enum dimension : u32
			{
				texture_1d,
				texture_2d,
				texture_3d,
				texture_cube
			};
		};

		struct texture_import_settings
		{
			char* sources;		// セミコロン ';' で区切られた1つ以上のファイルパスの文字列。
			u32 source_count;	// ファイルパスの数
			u32 dimension;
			u32 mip_levels;
			u32 array_size;
			f32 alpha_threshold;
			u32 prefer_bc7;
			u32 output_format;
			u32 compress;
		};

		struct texture_info
		{
			u32 width;
			u32 height;
			u32 array_size;
			u32 mip_levels;
			u32 format;
			u32 import_error;
			u32 flags;
		};

		struct texture_data
		{
			constexpr static u32 max_mips{ 14 };	// 8Kテクスチャまでサポート
			u8* subresource_data;
			u32 subresource_size;
			u8* icon;
			u32 icon_size;
			texture_info info;
			texture_import_settings import_settings;
		};

		[[nodiscard]] ScratchImage load_from_fole(texture_data* const data, const char* file_name)
		{
			using namespace primal::content;
			assert(file_exists(file_name));
			ScratchImage scratch;
			if (!file_exists(file_name))
			{
				data->info.import_error = import_error::file_not_found;
				return scratch;
			}

			data->info.import_error = import_error::load;

			WIC_FLAGS wic_flags{ WIC_FLAGS_NONE };
			TGA_FLAGS tga_flags{ TGA_FLAGS_NONE };

			if (data->import_settings.output_format == DXGI_FORMAT_BC4_UNORM ||
				data->import_settings.output_format == DXGI_FORMAT_BC5_UNORM)
			{
				wic_flags |= WIC_FLAGS_IGNORE_SRGB;
				tga_flags |= TGA_FLAGS_IGNORE_SRGB;
			}

			const std::wstring wfile{ to_wstring(file_name) };
			const wchar_t* const file{ wfile.c_str() };

			// まずWICフォーマット（BMP、JPEG、PNGなど）を試す
			wic_flags |= WIC_FLAGS_FORCE_RGB;
			HRESULT hr{ LoadFromWICFile(file, wic_flags, nullptr, scratch) };

			// WICのフォーマットではなかった。 TGAを試す
			if (FAILED(hr))
			{
				hr = LoadFromTGAFile(file, tga_flags, nullptr, scratch);
			}

			// TGAでもなかった。 HDRを試す
			if (FAILED(hr))
			{
				hr = LoadFromHDRFile(file, nullptr, scratch);
				if (SUCCEEDED(hr)) data->info.flags |= texture_flags::is_hdr;
			}

			// HDRではなかった。 DDSを試す
			if (FAILED(hr))
			{
				hr = LoadFromDDSFile(file, DDS_FLAGS_FORCE_RGB, nullptr, scratch);
				if (SUCCEEDED(hr))
				{
					data->info.import_error = import_error::decompress;
					ScratchImage mip_scratch;
					hr = Decompress(scratch.GetImages(), scratch.GetImageCount(), scratch.GetMetadata(),
						DXGI_FORMAT_UNKNOWN, mip_scratch);

					if (SUCCEEDED(hr))
					{
						scratch = std::move(mip_scratch);
					}
				}
			}

			if (SUCCEEDED(hr))
			{
				data->info.import_error = import_error::succeeded;
			}

			return scratch;
		}

	}	// 匿名名前空間

	EDITOR_INTERFACE void DecompressMipmaps(texture_data* const data)
	{

	}

	EDITOR_INTERFACE void Import(texture_data* const data)
	{
		const texture_import_settings& settings{ data->import_settings };
		assert(settings.sources && settings.source_count);

		utl::vector<ScratchImage> scratch_images;
		utl::vector<Image> images;

		u32 width{ 0 };
		u32 height{ 0 };
		DXGI_FORMAT format{};
		utl::vector<std::string> files = split(settings.sources, ';');
		assert(files.size() == settings.source_count);

		for (u32 i{ 0 }; i < settings.source_count; ++i)
		{
			scratch_images.emplace_back(load_from_file(data, files[i].c_str()));
			if (data->info.import_error) return;

			const ScratchImage& scratch{ scratch_images.back() };
			const TexMetadata& metadata{ scratch.GetMetadata() };

			if (i == 0)
			{
				width = (u32)metadata.width;
				height = (u32)metadata.height;
				format = metadata.format;
			}

			// すべての画像ソースは同じサイズでなければならない
			if (width != metadata.width || height != metadata.height)
			{
				data->info.import_error = import_error::size_mismatch;
				return;
			}

			// すべての画像ソースは同じフォーマットでならない
			if (format != metadata.format)
			{
				data->info.import_error = import_error::format_mismatch;
				return;
			}

			const u32 array_size{ (u32)metadata.arraySize };
			const u32 depth{ (u32)metadata.depth };

			for (u32 array_index{ 0 }; array_index < array_size; ++array_index)
				for (u32 depth_index{ 0 }; depth_index < depth; ++depth_index)
				{
					const Image* image{ scratch.GetImage(0, array_index, depth_index) };
					assert(image);

					if (!image)
					{
						data->info.import_error = import_error::unknown;
						return;
					}

					if (width != image->width || height != image->height)
					{
						data->info.import_error = import_error::size_mismatch;
						return;
					}

					images.emplace_back(*image);
				}
		}

		ScratchImage scratch{ initialize_from_images(data, images) };
		if (data->info.import_error) return;

		if (settings.compress)
		{
			// NOTE: エディタがアイコンを生成するために、最初の非圧縮画像のコピーを作成します。
			//		これは圧縮されたインポートに対してのみ行います。
			//		圧縮されていない場合、エディタは返されたsubresourceから最初の画像を選ぶことができます。
			copy_icon(scratch, data);
			ScratchImage bc_scratch{ compress_image(data, scratch) };

			if (data->info.import_error) return;

			scratch = std::move(bc_scratch);
		}

		copy_subresources(scratch, data);
		texture_info_from_metadata(scratch.GetMetadata(), data->info);
	}

}

