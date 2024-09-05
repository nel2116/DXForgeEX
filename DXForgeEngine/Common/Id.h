// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Id.h]
// �쐬�� : 2024/08/12
// �쐬�� : �c���~�m��
// �T�v
// �@ID���`�����t�@�C��
// �X�V����
// 2024/08/12 �V�K�쐬
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== �C���N���[�h�� ======
#include "CommonHeaders.h"

namespace dxforge::id
{
	using id_type = u32; // id_type �� 32 �r�b�g�����Ȃ����� (u32) �Ƃ��Ē�`

	namespace internal
	{
		constexpr u32 generation_bits{ 8 }; // ����ԍ���ێ����邽�߂Ɏg�p����r�b�g���� 8 �ɐݒ�
		// �C���f�b�N�X�Ɏg�p����r�b�g�����v�Z
		constexpr u32 index_bits{ sizeof(id_type) * 8 - generation_bits };
		// �C���f�b�N�X�}�X�N���`���A���ʃr�b�g���C���f�b�N�X�Ƃ��Ďg�p
		constexpr id_type index_mask{ (id_type{1} << index_bits) - 1 };
		// ����}�X�N���`���A��ʃr�b�g�𐢑�ԍ��Ƃ��Ďg�p
		constexpr id_type generation_mask{ (id_type{1} << generation_bits) - 1 };
	}	// namespace internal
	// ID �̍ő�l�i������ ID �������j���`
	constexpr id_type invalid_id{ (id_type)-1 };
	// �폜���ꂽ�v�f�̍ŏ������`
	constexpr u32 min_deleted_elements{ 1024 };

	// generation_type �������Ɋ�Â��Č��� (generation_bits �� 8 �ȉ��Ȃ� u8�A16 �ȉ��Ȃ� u16�A����ȊO�� u32)
	using generation_type = std::conditional_t<internal::generation_bits <= 8, u8, std::conditional_t<internal::generation_bits <= 16, u16, u32>>;

	// generation_type �̃T�C�Y���Ageneration_bits �ɂ���ĕK�v�ȃr�b�g�����m�ۂł��邱�Ƃ��m�F����
	static_assert(sizeof(generation_type) * 8 >= internal::generation_bits);

	// id_type �̃T�C�Y���Ageneration_type �̃T�C�Y�ȏ�ł��邱�Ƃ��m�F
	static_assert((sizeof(id_type) - sizeof(generation_type)) > 0);

	/// @brief ID ���L�����ǂ����𔻒肷��֐�
	/// @param id ���肷�� ID
	/// @return ID ���L���Ȃ� true�A�����Ȃ� false
	inline bool is_valid(id_type id)
	{
		return id != invalid_id; // ������ ID �� invalid_id �ƈ�v���邽�߁A����ȊO�͗L���Ƃ���
	}

	/// @brief ID �̃C���f�b�N�X�������擾����֐�
	/// @param id �C���f�b�N�X�������擾���� ID
	/// @return ID �̃C���f�b�N�X����
	inline id_type index(id_type id)
	{
		id_type index{ id & internal::index_mask }; // ID �̃C���f�b�N�X�������}�X�N���Ď擾
		assert(index != internal::index_mask); // �C���f�b�N�X���ő�l�łȂ����Ƃ��m�F
		return index;
	}

	/// @brief ID �̐��㕔�����擾����֐�
	/// @param id ���㕔�����擾���� ID
	/// @return ID �̐��㕔��
	inline id_type generation(id_type id)
	{
		return (id >> internal::index_bits) & internal::generation_mask; // ID �̐��㕔�����E�V�t�g���Ď擾���A����}�X�N��K�p
	}

	/// @brief ID �̐��㕔�����X�V����֐�
	/// @param id ���㕔�����X�V���� ID
	/// @return �V���� ID
	inline id_type new_generation(id_type id)
	{
		const id_type generation{ id::generation(id) + 1 }; // ���݂̐���� 1 ��������
		assert(generation < (((u64)1 << internal::generation_bits) - 1));	// ����ԍ����ő�l�ɒB���Ă��Ȃ����Ƃ��m�F
		return index(id) | (generation << internal::index_bits); // �V��������𐶐����A�C���f�b�N�X�����Ƒg�ݍ��킹�ĐV���� ID ��Ԃ�
	}

#if _DEBUG
	namespace internal
	{
		struct id_base
		{
			constexpr explicit id_base(id_type id) : _id{ id } {} // �R���X�g���N�^: id_type �^�� ID ���󂯎��A�v���C�x�[�g�����o _id �ɐݒ�
			constexpr operator id_type() const { return _id; }    // id_base �� id_type �ɈÖٓI�ɕϊ�����I�y���[�^

		private:
			id_type _id;  // �v���C�x�[�g�����o: ID ��ێ�����
		};
	}	// namespace internal

#define DEFINE_TYPED_ID(name)								\
    struct name final : id::internal::id_base				\
    {														\
        constexpr explicit name(id::id_type id)				\
            : id_base{ id } {}								\
        constexpr name() : id_base{ 0 } {}					\
    };

#else
	// �f�o�b�O�ȊO�̃r���h�ł́ADEFINE_TYPED_ID �}�N���͒P�� id::id_type �̕ʖ����쐬
#define DEFINE_TYPED_ID(name) using name = id::id_type;
#endif

}