// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [HeadlessTestScripts.cpp]
// 作成日 : 2026/10/27
// 作成者 : 田中ミノル
// 概要 :
// ヘッドレス実行(--game-bin)のテストで、game.binから参照するスクリプト。
// ゲームのexeと同じく、REGISTER_SCRIPTでこの実行ファイルに静的に登録する。
// 更新履歴
// 2026/10/27 新規作成 (#11)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "Components/Entity.h"
#include "Components/Script.h"

using namespace dxforge;

namespace
{
	// update()が呼ばれるたびに位置のxを1、yをdtだけ進めるスクリプト。
	// 最後のフレームの位置から、update()の呼び出し回数と渡されたdtの合計を外から確かめられる。
	class headless_test_mover : public script::entity_script
	{
	public:
		explicit headless_test_mover(game_entity::entity entity)
			: script::entity_script{ entity } {}

		void update(f32 dt) override
		{
			const math::v3 p{ position() };
			set_position(math::v3{ p.x + 1.f, p.y + dt, p.z });
		}
	};
}

REGISTER_SCRIPT(headless_test_mover);
