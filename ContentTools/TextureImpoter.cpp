// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [TextureImpoter.cpp]
// 作成日 : 2025/01/23
// 作成者 : 田中ミノル
// 概要 :
// テクスチャのインポートを行う
// 更新履歴
// 2025/01/23 新規作成
// 2026/10/18 正距円筒のHDR画像からキューブマップへの変換に対応 (Primal Day 072)
// 2026/10/18 D3D11の機能レベル11_0のアダプタも使えるよう修正
// 2026/10/18 拡散光のIBL用の事前フィルタ(PrefilterDiffuseIBL)を追加し、プリフィルタするキューブマップの圧縮をフィルタの後に回すよう変更 (Primal Day 073)
// 2026/10/19 鏡面反射のIBL用の事前フィルタ(PrefilterSpecularIBL)で、ラフネスごとのミップへのフィルタを行うよう変更 (Primal Day 074)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "ToolsCommon.h"
#include "EnvMapProcessing.h"
#include "Content/ContentToEngine.h"
#include "Utilities/IOStream.h"
#include <DirectXTex.h>
#include <dxgi1_6.h>

using namespace DirectX;
using namespace Microsoft::WRL;

namespace dxforge::tools
{
	bool is_normal_map(const Image* const image);

	namespace
	{
		struct ibl_filter
		{
			enum type : u32
			{
				diffuse = 0,
				specular,
			};
		};

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
				need_six_images,
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
			f32 alpha_threshold;
			u32 prefer_bc7;
			u32 output_format;
			u32 compress;
			u32 cubemap_size;
			u32 mirror_cubemap;
			u32 prefilter_cubemap;
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
			constexpr static u32    max_mips{ 14 }; // 最大8Kテクスチャをサポートしています。
			u8* subresource_data;
			u32                     subresource_size;
			u8* icon;
			u32                     icon_size;
			texture_info            info;
			texture_import_settings import_settings;
		};

		struct d3d11_device
		{
			ComPtr<ID3D11Device>    device;
			std::mutex              hw_compression_mutex;
		};

		std::mutex                  device_creation_mutex;
		utl::vector<d3d11_device>   d3d11_devices;

		HMODULE dxgi_module{ nullptr };
		HMODULE d3d11_module{ nullptr };

		/// @brief ハードウェアアダプタを取得
		/// @return ハードウェアアダプタのリスト
		utl::vector<ComPtr<IDXGIAdapter>> get_adapters_by_performance()
		{
			if (!dxgi_module)
			{
				dxgi_module = LoadLibrary(L"dxgi.dll");
				if (!dxgi_module) return {};
			}

			using PFN_CreateDXGIFactory1 = HRESULT(WINAPI*)(REFIID, void**);
			const PFN_CreateDXGIFactory1 create_dxgi_factory1{ (PFN_CreateDXGIFactory1)((void*)GetProcAddress(dxgi_module, "CreateDXGIFactory1")) };
			if (!create_dxgi_factory1) return {};


			ComPtr<IDXGIFactory7> factory;
			utl::vector<ComPtr<IDXGIAdapter>> adapters;

			if (SUCCEEDED(create_dxgi_factory1(IID_PPV_ARGS(factory.GetAddressOf()))))
			{
				constexpr u32 warp_id{ 0x1414 };

				ComPtr<IDXGIAdapter> adapter;
				for (u32 i{ 0 }; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i)
				{
					if (!adapter) continue;

					DXGI_ADAPTER_DESC desc;
					adapter->GetDesc(&desc);

					if (desc.VendorId != warp_id) adapters.emplace_back(adapter);

					adapter.Reset();
				}
			}

			return adapters;
		}

		/// @brief デバイスを作成
		void create_device()
		{
			if (d3d11_devices.size()) return;

			if (!d3d11_module)
			{
				d3d11_module = LoadLibrary(L"d3d11.dll");
				if (!d3d11_module) return;
			}

			const PFN_D3D11_CREATE_DEVICE d3d11_create_device{ (PFN_D3D11_CREATE_DEVICE)((void*)GetProcAddress(d3d11_module, "D3D11CreateDevice")) };
			if (!d3d11_create_device) return;

			u32 create_device_flags{ 0 };
#ifdef _DEBUG
			create_device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

			utl::vector<ComPtr<IDXGIAdapter>> adapters{ get_adapters_by_performance() };
			utl::vector<ComPtr<ID3D11Device>> devices(adapters.size(), nullptr);
			// NOTE: Primalは11_1のみを要求しているが、11_0までしか対応しないアダプタでもGPUでの圧縮・変換を使えるよう11_0も許可する。
			constexpr D3D_FEATURE_LEVEL feature_levels[]{ D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

			for (u32 i{ 0 }; i < adapters.size(); ++i)
			{
				ID3D11Device** device{ &devices[i] };
				D3D_FEATURE_LEVEL feature_level;
				[[maybe_unused]]
				HRESULT hr{ d3d11_create_device(adapters[i].Get(), adapters[i] ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
											   nullptr, create_device_flags, feature_levels, _countof(feature_levels),
											   D3D11_SDK_VERSION, device, &feature_level, nullptr) };
				assert(SUCCEEDED(hr));
			}

			for (u32 i{ 0 }; i < devices.size(); ++i)
			{
				// NOTE: 要求された機能レベル(D3D_FEATURE_LEVEL_11_0以上)をサポートしていないアダプタでは、デバイスの作成に失敗することがあるためです。
				if (devices[i])
				{
					d3d11_devices.emplace_back();
					d3d11_devices.back().device = devices[i];
				}
			}
		}

		/// @brief D3D11デバイスの作成を一度だけ試みる
		/// @return 使用可能なデバイスがある場合はtrue
		bool try_create_device()
		{
			std::lock_guard lock{ device_creation_mutex };
			static bool try_once = false;
			if (!try_once)
			{
				try_once = true;
				create_device();
			}

			return d3d11_devices.size() > 0;
		}

		/// @brief 空いているD3D11デバイスを使って処理を実行する
		/// @param func 実行する処理(引数はID3D11Device*)
		/// @return デバイスが使用できず実行しなかった場合はfalse
		template<typename T> bool run_on_gpu(T func)
		{
			if (!try_create_device())
			{
				return false;
			}

			bool wait{ true };
			while (wait)
			{
				for (u32 i{ 0 }; i < d3d11_devices.size(); ++i)
				{
					if (d3d11_devices[i].hw_compression_mutex.try_lock())
					{
						func(d3d11_devices[i].device.Get());
						d3d11_devices[i].hw_compression_mutex.unlock();
						wait = false;
						break;
					}
				}
				if (wait) std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}

			return true;
		}

		/// @brief デバイスを取得
		/// @param flags フラグ
		/// @param flag フラグ
		/// @param set フラグを設定するかどうか
		constexpr void set_or_clear_flag(u32& flags, u32 flag, bool set)
		{
			if (set) flags |= flag; else flags &= ~flag;
		}

		/// @brief テクスチャの最大ミップマップ数を取得
		/// @param width 幅
		/// @param height 高さ
		/// @param depth 深度
		/// @return ミップマップ数
		constexpr u32 get_max_mip_count(u32 width, u32 height, u32 depth)
		{
			u32 mip_levels{ 1 };
			while (width > 1 || height > 1 || depth > 1)
			{
				width >>= 1;
				height >>= 1;
				depth >>= 1;

				++mip_levels;
			}

			return mip_levels;
		}

		/// @brief HDRのフォーマットかどうかを判定
		/// @param format フォーマット
		/// @return HDRのフォーマットの場合はtrue
		bool is_hdr(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_BC6H_UF16:
			case DXGI_FORMAT_BC6H_SF16:
			case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
			case DXGI_FORMAT_R10G10B10A2_UINT:
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
			case DXGI_FORMAT_R32G32B32_FLOAT:
				return true;
			default:
				break;
			}

			return false;
		}

		/// @brief テクスチャ情報をメタデータから取得
		/// @param metadata メタデータ
		/// @param info テクスチャ情報
		void texture_info_from_metadata(const TexMetadata& metadata, texture_info& info)
		{
			using namespace dxforge::content;
			const DXGI_FORMAT format{ metadata.format };
			info.format = format;
			info.width = (u32)metadata.width;
			info.height = (u32)metadata.height;
			info.array_size = metadata.IsVolumemap() ? (u32)metadata.depth : (u32)metadata.arraySize;
			info.mip_levels = (u32)metadata.mipLevels;
			set_or_clear_flag(info.flags, texture_flags::has_alpha, HasAlpha(format));
			set_or_clear_flag(info.flags, texture_flags::is_hdr, is_hdr(format));
			set_or_clear_flag(info.flags, texture_flags::is_premultiplied_alpha, metadata.IsPMAlpha());
			set_or_clear_flag(info.flags, texture_flags::is_cube_map, metadata.IsCubemap());
			set_or_clear_flag(info.flags, texture_flags::is_volume_map, metadata.IsVolumemap());
			set_or_clear_flag(info.flags, texture_flags::is_srgb, IsSRGB(format));
		}

		/// @brief テクスチャ情報を設定
		/// @param scratch スクラッチイメージ
		/// @param data テクスチャデータ
		void copy_subresources(const ScratchImage& scratch, texture_data* const data)
		{
			const Image* const images{ scratch.GetImages() };
			const u32 image_count{ (u32)scratch.GetImageCount() };
			assert(images && scratch.GetMetadata().mipLevels && scratch.GetMetadata().mipLevels <= texture_data::max_mips);

			u64 subresource_size{ 0 };

			for (u32 i{ 0 }; i < image_count; ++i)
			{
				// 4 x u32 （幅、高さ、行ピッチ、スライスピッチ用
				subresource_size += sizeof(u32) * 4 + images[i].slicePitch;
			}

			if (subresource_size > ~(u32)0)
			{
				// リソースあたり最大4GBをサポート。
				data->info.import_error = import_error::max_size_exceeded;
				return;
			}

			data->subresource_size = (u32)subresource_size;
			data->subresource_data = (u8* const)CoTaskMemRealloc(data->subresource_data, subresource_size);
			assert(data->subresource_data);

			utl::blob_stream_writer blob{ data->subresource_data, data->subresource_size };

			for (u32 i{ 0 }; i < image_count; ++i)
			{
				const Image& image{ images[i] };
				blob.write((u32)image.width);
				blob.write((u32)image.height);
				blob.write((u32)image.rowPitch);
				blob.write((u32)image.slicePitch);
				blob.write(image.pixels, image.slicePitch);
			}
		}

		/// @brief サブリソースデータをイメージに変換
		/// @param data テクスチャデータ
		/// @return イメージリスト
		[[nodiscard]] utl::vector<Image> subresource_data_to_images(texture_data* const data)
		{
			assert(data && data->subresource_data && data->subresource_size);
			assert(data->info.mip_levels && data->info.mip_levels <= texture_data::max_mips);
			assert(data->info.array_size);

			const texture_info& info{ data->info };
			u32 image_count{ info.array_size };

			if (info.flags & content::texture_flags::is_volume_map)
			{
				u32 depth_per_mip_level{ info.array_size };
				for (u32 i{ 1 }; i < info.mip_levels; ++i)
				{
					depth_per_mip_level = std::max(depth_per_mip_level >> 1, (u32)1);
					image_count += depth_per_mip_level;
				}
			}
			else
			{
				image_count *= info.mip_levels;
			}

			utl::blob_stream_reader blob{ data->subresource_data };
			utl::vector<Image> images(image_count);

			for (u32 i{ 0 }; i < image_count; ++i)
			{
				Image image{};
				image.width = blob.read<u32>();
				image.height = blob.read<u32>();
				image.format = (DXGI_FORMAT)info.format;
				image.rowPitch = blob.read<u32>();
				image.slicePitch = blob.read<u32>();
				image.pixels = (u8*)blob.position();

				blob.skip(image.slicePitch);

				images[i] = image;
			}

			return images;
		}

		/// @brief アイコンをコピー
		/// @param bc_image コピーするイメージ
		/// @param data テクスチャデータ
		void copy_icon(const Image& bc_image, texture_data* const data)
		{
			ScratchImage scratch;
			if (FAILED(Decompress(bc_image, DXGI_FORMAT_UNKNOWN, scratch)))
			{
				return;
			}

			assert(scratch.GetImages());
			const Image& image{ scratch.GetImages()[0] };

			// 4 x u32 （幅、高さ、行ピッチ、スライスピッチ用
			data->icon_size = (u32)(sizeof(u32) * 4 + image.slicePitch);
			data->icon = (u8* const)CoTaskMemRealloc(data->icon, data->icon_size);
			assert(data->icon);

			utl::blob_stream_writer blob{ data->icon, data->icon_size };
			blob.write((u32)image.width);
			blob.write((u32)image.height);
			blob.write((u32)image.rowPitch);
			blob.write((u32)image.slicePitch);
			blob.write(image.pixels, image.slicePitch);
		}

		/// @brief テクスチャをロード
		/// @param data テクスチャデータ
		/// @param file_name ファイル名
		/// @return スクラッチイメージ
		[[nodiscard]] ScratchImage load_from_file(texture_data* const data, const char* file_name)
		{
			using namespace dxforge::content;
			assert(file_exists(file_name));

			if (!file_exists(file_name))
			{
				data->info.import_error = import_error::file_not_found;
				return {};
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
			ScratchImage scratch;

			// まずWICフォーマットの一つを試してみてください（例：BMP、JPEG、PNGなど）。
			wic_flags |= WIC_FLAGS_FORCE_RGB;
			HRESULT hr{ LoadFromWICFile(file, wic_flags, nullptr, scratch) };

			// WICのフォーマットではなかった。 TGAを試してみてください。
			if (FAILED(hr))
			{
				hr = LoadFromTGAFile(file, tga_flags, nullptr, scratch);
			}

			// TGAでもなかった。 HDRを試してみてください。
			if (FAILED(hr))
			{
				hr = LoadFromHDRFile(file, nullptr, scratch);
				if (SUCCEEDED(hr)) data->info.flags |= texture_flags::is_hdr;
			}

			// HDRではなかった。 DDSを試してみてください。
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

		/// @brief ミップマップを生成
		/// @param source 元のスクラッチイメージ
		/// @param info テクスチャ情報(失敗した場合はimport_errorを設定する)
		/// @param mip_levels ミップレベル数(0の場合はすべてのミップレベルを生成する)
		/// @param is_3d 3Dテクスチャかどうか
		/// @return ミップマップを含むスクラッチイメージ
		[[nodiscard]] ScratchImage generate_mipmaps(const ScratchImage& source, texture_info& info, u32 mip_levels, bool is_3d)
		{
			const TexMetadata& metadata{ source.GetMetadata() };
			mip_levels = math::clamp(mip_levels, (u32)0, get_max_mip_count((u32)metadata.width, (u32)metadata.height, (u32)metadata.depth));
			HRESULT hr{ S_OK };

			ScratchImage mip_scratch{};

			if (!is_3d)
			{
				hr = GenerateMipMaps(source.GetImages(), source.GetImageCount(), source.GetMetadata(),
					TEX_FILTER_DEFAULT, mip_levels, mip_scratch);
			}
			else
			{
				hr = GenerateMipMaps3D(source.GetImages(), source.GetImageCount(), source.GetMetadata(),
					TEX_FILTER_DEFAULT, mip_levels, mip_scratch);
			}

			if (FAILED(hr))
			{
				info.import_error = import_error::mipmap_generation;
				return {};
			}

			return mip_scratch;
		}

		/// @brief テクスチャを初期化
		/// @param data テクスチャデータ
		/// @param images イメージリスト
		/// @return スクラッチイメージ
		[[nodiscard]] ScratchImage initialize_from_images(texture_data* const data, const utl::vector<Image>& images)
		{
			assert(data);
			const texture_import_settings& settings{ data->import_settings };

			ScratchImage scratch;
			HRESULT hr{ S_OK };
			const u32 array_size{ (u32)images.size() };

			{ // 画像をスクラッチイメージに変換
				ScratchImage working_scratch{};

				if (settings.dimension == texture_dimension::texture_1d ||
					settings.dimension == texture_dimension::texture_2d)
				{
					const bool allow_1d{ settings.dimension == texture_dimension::texture_1d };
					assert(array_size >= 1 && images.size() >= 1);
					hr = working_scratch.InitializeArrayFromImages(images.data(), images.size(), allow_1d);
				}
				else if (settings.dimension == texture_dimension::texture_cube)
				{
					const Image& image{ images[0] };

					if (math::is_equal((f32)image.width / (f32)image.height, 2.f))
					{
						// 正距円筒の環境マップ。画像1枚につき1つのキューブマップに変換する。
						// GPUが使えない場合や、GPUでの変換に失敗した場合(例: UAVに使えないフォーマット)はCPUで変換する。
						const bool converted_on_gpu{ run_on_gpu([&](ID3D11Device* device)
							{
								hr = equirectangular_to_cubemap(device, images.data(), array_size, settings.cubemap_size,
									settings.prefilter_cubemap, settings.mirror_cubemap, working_scratch);
							}) };

						if (!converted_on_gpu || FAILED(hr))
						{
							working_scratch.Release();
							hr = equirectangular_to_cubemap(images.data(), array_size, settings.cubemap_size,
								settings.prefilter_cubemap, settings.mirror_cubemap, working_scratch);
						}
					}
					else if (array_size % 6 || image.width != image.height)
					{
						data->info.import_error = import_error::need_six_images;
						return {};
					}
					else
					{
						hr = working_scratch.InitializeCubeFromImages(images.data(), images.size());
					}
				}
				else
				{
					assert(settings.dimension == texture_dimension::texture_3d);
					hr = working_scratch.Initialize3DFromImages(images.data(), images.size());
				}

				if (FAILED(hr))
				{
					data->info.import_error = import_error::unknown;
					return {};
				}

				scratch = std::move(working_scratch);
			}

			// NOTE: プリフィルタするキューブマップは常にすべてのミップレベルを生成する。
			const bool prefilter_cubemap{ settings.dimension == texture_dimension::texture_cube && settings.prefilter_cubemap };
			if (settings.mip_levels != 1 || prefilter_cubemap)
			{
				scratch = generate_mipmaps(scratch, data->info, prefilter_cubemap ? 0 : settings.mip_levels,
					settings.dimension == texture_dimension::texture_3d);
			}

			return scratch;
		}

		/// @brief テクスチャの出力フォーマットを決定
		/// @param data テクスチャデータ
		/// @param scratch スクラッチイメージ
		/// @param image イメージ
		/// @return DXGIフォーマット
		DXGI_FORMAT determine_output_format(texture_data* const data, ScratchImage& scratch, const Image* const image)
		{
			assert(data && data->import_settings.compress);
			using namespace dxforge::content;
			const DXGI_FORMAT image_format{ image->format };
			DXGI_FORMAT output_format{ (DXGI_FORMAT)data->import_settings.output_format };

			// インポート設定でフォーマットが明示的に指定されていない場合、最適なブロック圧縮フォーマットを決定する。
			if (output_format != DXGI_FORMAT_UNKNOWN)
			{
				goto _done;
			}

			if ((data->info.flags & texture_flags::is_hdr) ||
				image_format == DXGI_FORMAT_BC6H_UF16 || image_format == DXGI_FORMAT_BC6H_SF16)
			{
				output_format = DXGI_FORMAT_BC6H_UF16;
			}

			// ソース画像がグレースケールまたはシングルチャンネルのブロック圧縮フォーマット（BC4）の場合、出力フォーマットはBC4になります。
			else if (image_format == DXGI_FORMAT_R8_UNORM || image_format == DXGI_FORMAT_BC4_UNORM || image_format == DXGI_FORMAT_BC4_SNORM)
			{
				output_format = DXGI_FORMAT_BC4_UNORM;
			}

			// 元画像が法線マップかどうかをテストし、法線マップであればBC5形式で出力する。
			else if (is_normal_map(image) || image_format == DXGI_FORMAT_BC5_UNORM || image_format == DXGI_FORMAT_BC5_SNORM)
			{
				data->info.flags |= texture_flags::is_imported_as_normal_map;
				output_format = DXGI_FORMAT_BC5_UNORM;

				if (IsSRGB(image_format))
				{
					scratch.OverrideFormat(MakeTypelessUNORM(MakeTypeless(image_format)));
				}
			}

			// RGBAブロック圧縮フォーマットを使用する。
			else
			{
				output_format = data->import_settings.prefer_bc7 ? DXGI_FORMAT_BC7_UNORM :
					scratch.IsAlphaAllOpaque() ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_BC3_UNORM;
			}

		_done:
			assert(IsCompressed(output_format));
			if (HasAlpha(output_format)) data->info.flags |= texture_flags::has_alpha;

			return IsSRGB(image_format) ? MakeSRGB(output_format) : output_format;
		}

		/// @brief テクスチャが法線マップかどうかを判定
		/// @param format フォーマット
		/// @return 法線マップの場合はtrue
		bool can_use_gpu(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_BC6H_TYPELESS:
			case DXGI_FORMAT_BC6H_UF16:
			case DXGI_FORMAT_BC6H_SF16:
			case DXGI_FORMAT_BC7_TYPELESS:
			case DXGI_FORMAT_BC7_UNORM:
			case DXGI_FORMAT_BC7_UNORM_SRGB:
			{
				return true;
			}
			}

			return false;
		}

		/// @brief テクスチャが法線マップかどうかを判定
		/// @param data テクスチャデータ
		/// @param scratch スクラッチイメージ
		/// @return 法線マップの場合はtrue
		[[nodiscard]] ScratchImage compress_image(texture_data* const data, ScratchImage& scratch)
		{
			assert(data && data->import_settings.compress && scratch.GetImages());

			const Image* const image{ scratch.GetImage(0, 0, 0) };
			if (!image)
			{
				data->info.import_error = import_error::unknown;
				return {};
			}

			const DXGI_FORMAT output_format{ determine_output_format(data, scratch, image) };
			HRESULT hr{ S_OK };
			ScratchImage bc_scratch;
			if (!(can_use_gpu(output_format) &&
				  run_on_gpu([&](ID3D11Device* device)
					  {
						  hr = Compress(device, scratch.GetImages(), scratch.GetImageCount(),
							  scratch.GetMetadata(), output_format, TEX_COMPRESS_DEFAULT, 1.f, bc_scratch);
					  })))
			{
				hr = Compress(scratch.GetImages(), scratch.GetImageCount(), scratch.GetMetadata(),
					output_format, TEX_COMPRESS_PARALLEL, data->import_settings.alpha_threshold, bc_scratch);
			}

			if (FAILED(hr))
			{
				data->info.import_error = import_error::compress;
				return {};
			}

			return bc_scratch;
		}

		/// @brief ブロック圧縮されたテクスチャを解凍する
		/// @param data テクスチャデータ(失敗した場合はimport_errorを設定する)
		/// @return 解凍したスクラッチイメージ
		[[nodiscard]] ScratchImage decompress_image(texture_data* const data)
		{
			using namespace dxforge::content;
			assert(data->import_settings.compress);
			texture_info& info{ data->info };
			const DXGI_FORMAT format{ (DXGI_FORMAT)info.format };
			assert(IsCompressed(format));
			utl::vector<Image> images = subresource_data_to_images(data);
			const bool is_3d{ (info.flags & texture_flags::is_volume_map) != 0 };

			TexMetadata metadata{};
			metadata.width = info.width;
			metadata.height = info.height;
			metadata.depth = is_3d ? info.array_size : 1;
			metadata.arraySize = is_3d ? 1 : info.array_size;
			metadata.mipLevels = info.mip_levels;
			metadata.miscFlags = info.flags & texture_flags::is_cube_map ? TEX_MISC_TEXTURECUBE : 0;
			metadata.miscFlags2 = info.flags & texture_flags::is_premultiplied_alpha
				? TEX_ALPHA_MODE_PREMULTIPLIED
				: info.flags & texture_flags::has_alpha ? TEX_ALPHA_MODE_STRAIGHT : TEX_ALPHA_MODE_OPAQUE;
			metadata.format = format;
			// TODO: 1Dのテクスチャをサポートする
			metadata.dimension = is_3d ? TEX_DIMENSION_TEXTURE3D : TEX_DIMENSION_TEXTURE2D;

			ScratchImage scratch;
			HRESULT hr{ Decompress(images.data(), (size_t)images.size(), metadata, DXGI_FORMAT_UNKNOWN, scratch) };

			if (FAILED(hr))
			{
				data->info.import_error = import_error::decompress;
				return {};
			}

			return scratch;
		}

		/// @brief Import()が出力した圧縮前のキューブマップをIBL用に事前フィルタし、インポート設定に従って圧縮する
		/// @param data テクスチャデータ(結果で置き換える。失敗した場合はimport_errorを設定する)
		/// @param filter_type フィルタの種類
		void prefilter_ibl(texture_data* const data, ibl_filter::type filter_type)
		{
			using namespace dxforge::content;
			texture_info& info{ data->info };
			const DXGI_FORMAT format{ (DXGI_FORMAT)info.format };

			// NOTE: Primalはassertで確かめているが、エディタ以外(テストなど)からも呼べるようにエラーとして返す。
			if (!data->import_settings.prefilter_cubemap || IsCompressed(format) || !(info.flags & texture_flags::is_cube_map) ||
				info.width != info.height || !info.array_size || info.array_size % 6)
			{
				info.import_error = import_error::unknown;
				return;
			}

			utl::vector<Image> images = subresource_data_to_images(data);
			assert(!images.empty());
			const u32 cubemap_count{ info.array_size / 6 };

			HRESULT hr{ S_OK };

			ScratchImage cubemaps{};
			hr = cubemaps.InitializeCube(format, info.width, info.height, cubemap_count, info.mip_levels);
			if (FAILED(hr) || cubemaps.GetImageCount() != images.size())
			{
				info.import_error = import_error::unknown;
				return;
			}

			for (u32 img_idx{ 0 }; img_idx < cubemaps.GetImageCount(); ++img_idx)
			{
				const Image& image{ cubemaps.GetImages()[img_idx] };
				if (image.slicePitch != images[img_idx].slicePitch || image.rowPitch != images[img_idx].rowPitch)
				{
					info.import_error = import_error::unknown;
					return;
				}
				memcpy(image.pixels, images[img_idx].pixels, image.slicePitch);
			}

			{
				// GPUが使えない場合や、GPUでのフィルタに失敗した場合(例: UAVに使えないsRGBのフォーマット)はCPUでフィルタする。
				const bool diffuse{ filter_type == ibl_filter::diffuse };
				ScratchImage prefiltered{};
				const bool filtered_on_gpu{ run_on_gpu([&](ID3D11Device* device)
					{
						hr = diffuse ? prefilter_diffuse(device, cubemaps, prefiltered) : prefilter_specular(device, cubemaps, prefiltered);
					}) };

				if (!filtered_on_gpu || FAILED(hr))
				{
					prefiltered.Release();
					hr = diffuse ? prefilter_diffuse(cubemaps, prefiltered) : prefilter_specular(cubemaps, prefiltered);
				}

				if (FAILED(hr))
				{
					info.import_error = import_error::unknown;
					return;
				}

				cubemaps = std::move(prefiltered);
			}

			if (data->import_settings.compress)
			{
				ScratchImage bc_scratch{ compress_image(data, cubemaps) };
				if (data->info.import_error) return;

				// アイコンに使用する最初の画像を解凍する。
				assert(bc_scratch.GetImages());
				copy_icon(bc_scratch.GetImages()[0], data);

				cubemaps = std::move(bc_scratch);
			}

			copy_subresources(cubemaps, data);
			texture_info_from_metadata(cubemaps.GetMetadata(), data->info);
		}

	} // 匿名名前空間

	/// @brief テクスチャツールの終了処理
	void ShutDownTextureTools()
	{
		d3d11_devices.clear();

		if (dxgi_module)
		{
			FreeLibrary(dxgi_module);
			dxgi_module = nullptr;
		}

		if (d3d11_module)
		{
			FreeLibrary(d3d11_module);
			d3d11_module = nullptr;
		}
	}

	/// @brief Import()が出力したキューブマップから拡散光のIBL用のキューブマップ(irradiance map)を作る
	/// @param data テクスチャデータ(Import()の結果のコピー。結果で置き換える)
	EDITOR_INTERFACE void PrefilterDiffuseIBL(texture_data* const data)
	{
		prefilter_ibl(data, ibl_filter::diffuse);
	}

	/// @brief Import()が出力したキューブマップを鏡面反射のIBL用に、ラフネスごとのミップへ事前フィルタする(Primal Day 074)
	/// @details 結果は1面が256x256でミップ6つ(ミップmがラフネスm / 5)のキューブマップ。その後、Import()が後回しにした圧縮を行う。
	/// @param data テクスチャデータ(Import()の結果。結果で置き換える)
	EDITOR_INTERFACE void PrefilterSpecularIBL(texture_data* const data)
	{
		prefilter_ibl(data, ibl_filter::specular);
	}

	/// @brief テクスチャの解凍
	/// @param data テクスチャデータ
	EDITOR_INTERFACE void Decompress(texture_data* const data)
	{
		ScratchImage scratch{ decompress_image(data) };
		if (!data->info.import_error)
		{
			copy_subresources(scratch, data);
			texture_info_from_metadata(scratch.GetMetadata(), data->info);
		}
	}

	/// @brief テクスチャのインポート
	/// @param data テクスチャデータ
	/// @return 成功した場合はtrue
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

			// すべての画像ソースは同じサイズでなければなりません。
			if (width != metadata.width || height != metadata.height)
			{
				data->info.import_error = import_error::size_mismatch;
				return;
			}

			// すべての画像ソースは同じフォーマットでなければなりません。
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

		// NOTE: プリフィルタするキューブマップは圧縮しない。圧縮はPrefilterSpecularIBL()でフィルタした後に行う。
		if (settings.compress && !(scratch.GetMetadata().IsCubemap() && settings.prefilter_cubemap))
		{
			ScratchImage bc_scratch{ compress_image(data, scratch) };
			if (data->info.import_error) return;

			// アイコンに使用する最初の画像を解凍する。
			assert(bc_scratch.GetImages());
			copy_icon(bc_scratch.GetImages()[0], data);

			scratch = std::move(bc_scratch);
		}

		copy_subresources(scratch, data);
		texture_info_from_metadata(scratch.GetMetadata(), data->info);
	}

}	// namespace dxforge::tools

