// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Entity.cpp]
// �쐬�� : 2024/08/12
// �쐬�� : �c���~�m��
// �T�v
// 	�G���e�B�e�B�����������t�@�C��
// �X�V����
// 2024/08/12 �V�K�쐬
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== �C���N���[�h�� ======
#include "Entity.h"

namespace dxforge::game_entity
{
	namespace
	{
		utl::vector<id::generation_type> generations;
		utl::deque<entity_id> free_ids;
	}

	entity create_game_entity(const entity_info& info)
	{
		assert(info.transform);									// transform ��� nullptr �łȂ����Ƃ��m�F
		if (!info.transform) return entity();					// transform ��� nullptr �̏ꍇ�͖����ȃG���e�B�e�B��Ԃ�

		entity_id id;											// �G���e�B�e�BID���i�[����ϐ�

		// �폜���ꂽ�G���e�B�e�BID�����邩�ǂ����𔻒�
		if (free_ids.size() > id::min_deleted_elements)
		{	// �폜���ꂽ�G���e�B�e�BID������ꍇ
			id = free_ids.front();								// �폜���ꂽ�G���e�B�e�BID���ė��p
			assert(!is_alive(entity{ id }));					// �ė��p����ID���g���Ă��Ȃ����Ƃ��m�F
			free_ids.pop_front();								// �ė��p����ID���폜
			id = entity_id{ id::new_generation(id) };			// ����ԍ����X�V
			++generations[id::index(id)];						// ����ԍ����X�V
		}
		else
		{	// �폜���ꂽ�G���e�B�e�BID���Ȃ��ꍇ
			id = entity_id{ (id::id_type)generations.size() };	// �V�����G���e�B�e�BID���쐬
			generations.push_back(0);							// ����ԍ���������
		}

		const entity new_entity{ id };							// �V�����G���e�B�e�B���쐬
		const id::id_type index{ id::index(id) };				// �C���f�b�N�X���擾

		return new_entity;										// �V�����G���e�B�e�B��Ԃ�
	}

	void remove_game_entity(entity id)
	{
	}

	bool is_alive(entity id)
	{
		return false;
	}

}