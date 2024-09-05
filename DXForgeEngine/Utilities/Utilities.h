// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Utilities.h]
// �쐬�� : 2024/08/12
// �쐬�� : �c���~�m��
// �T�v
//
// �X�V����
// 2024/08/12 �V�K�쐬
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== �C���N���[�h�� ======

#define USE_STL_VECTOR 1
#define USE_STL_DEQUE 1

#if USE_STL_VECTOR
#include <vector>          // vector �w�b�_�[�t�@�C�����C���N���[�h
namespace dxforge::utl
{
	template<typename T>
	using vector = std::vector<T>;  // std::vector �� primal::utl ���O��ԓ��� vector �Ƃ��ăG�C���A�X
}
#endif

#if USE_STL_DEQUE
#include <deque>           // deque �w�b�_�[�t�@�C�����C���N���[�h
namespace dxforge::utl
{
	template<typename T>
	using deque = std::deque<T>;   // std::deque �� primal::utl ���O��ԓ��� deque �Ƃ��ăG�C���A�X
}
#endif


namespace dxforge::util
{
}