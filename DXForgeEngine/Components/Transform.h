// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Transform.h]
// �쐬�� : 2024/08/12
// �쐬�� : �c���~�m��
// �T�v
// �@Transform�R���|�[�l���g���`�����t�@�C��
// �X�V����
// 2024/08/12 �V�K�쐬
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== �C���N���[�h�� ======
#include "ComponentsCommon.h"

namespace dxforge::transform
{
	DEFINE_TYPED_ID(transform_id);

	struct init_info
	{
		f32 positon[3]{};        // �ʒu�����i�[����3�����x�N�g���ix, y, z�j
		f32 rotation[4]{};       // ��]�����i�[����N�H�[�^�j�I���iw, x, y, z�j
		f32 scale[3]{ 1.f, 1.f, 1.f }; // �X�P�[�������i�[����3�����x�N�g���ix, y, z�j�A�f�t�H���g�͑S����1.0
	};

	// Transform�R���|�[�l���g���쐬����֐�
	transform_id create_transform(const init_info& info, game_entity::entity_id entity_id);
	// Transform�R���|�[�l���g���폜����֐�
	void remove_transform(transform_id id);
}