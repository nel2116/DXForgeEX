// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Entity.h]
// �쐬�� : 2024/08/12
// �쐬�� : �c���~�m��
// �T�v
// 	�G���e�B�e�B���`�����t�@�C��
// �X�V����
// 2024/08/12 �V�K�쐬
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== �C���N���[�h�� ======
#include "ComponentsCommon.h"

namespace dxforge
{
	// INIT_INFO �}�N�����`�F�w�肳�ꂽ component �̖��O��Ԃ� init_info �\���̂��`����
#define INIT_INFO(component) namespace component { struct init_info; }
	// transform ���O��Ԃ� init_info �\���̂��`
	INIT_INFO(transform);
#undef INIT_INFO

	namespace game_entity {
		// entity_info �\���̂��`�F�Q�[���G���e�B�e�B�̏����������i�[
		struct entity_info
		{
			// transform ���O��Ԃ� init_info �^�̃|�C���^��錾�A�������� nullptr
			transform::init_info* transform{ nullptr };
		};

		// �Q�[���G���e�B�e�B���쐬����֐�
		entity create_game_entity(const entity_info& info);

		// �Q�[���G���e�B�e�B���폜����֐�
		void remove_game_entity(entity id);

		// �w�肵���G���e�B�e�B���L�����ǂ������m�F����֐�
		bool is_alive(entity id);
	}
};
