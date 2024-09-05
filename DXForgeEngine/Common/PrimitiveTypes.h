// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [PrimitiveTypes.h]
// �쐬�� : 2024/08/12
// �쐬�� : �c���~�m��
// �T�v
// �@��{�I�Ȍ^���`�����t�@�C��
// �X�V����
// 2024/08/12 �V�K�쐬
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== �C���N���[�h�� ======
#include <stdint.h>

// �����Ȃ������^
using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;

// �����t�������^
using s64 = int64_t;
using s32 = int32_t;
using s16 = int16_t;
using s8 = int8_t;

// ������ID
constexpr u64 u64_invalid_id(static_cast<u64>(0xffff'ffff'ffff'ffff));
constexpr u32 u32_invalid_id(static_cast<u32>(0xffff'ffff));
constexpr u16 u16_invalid_id(static_cast<u16>(0xffff));
constexpr u8 u8_invalid_id(static_cast<u8>(0xff));

// ���������_�^
using f32 = float;