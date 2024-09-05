// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [GameEntity.h]
// �쐬�� : 2024/08/12
// �쐬�� : �c���~�m��
// �T�v
// �@�O���Ɍ��J����entity�N���X���`�����t�@�C��
// �X�V����
// 2024/08/12 �V�K�쐬
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== �C���N���[�h�� ======
#include "..\Components\ComponentsCommon.h"

namespace dxforge::game_entity
{
	DEFINE_TYPED_ID(entity_id);

	class entity
	{
	public:
		constexpr explicit entity(entity_id id) : _id(id) {}
		constexpr explicit entity() : _id(id::invalid_id) {}

	private:
		entity_id _id;
	};
}