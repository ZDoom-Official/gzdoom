/*
** autosegs.h
** Arrays built at link-time
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#ifndef AUTOSEGS_H
#define AUTOSEGS_H

#include <type_traits>
#include <cstdint>

#if defined(__clang__)
#if defined(__has_feature) && __has_feature(address_sanitizer)
#define NO_SANITIZE __attribute__((no_sanitize("address")))
#else
#define NO_SANITIZE
#endif
#else
#define NO_SANITIZE
#endif

#if defined _MSC_VER
#define NO_SANITIZE_M __declspec(no_sanitize_address)
#else
#define NO_SANITIZE_M
#endif

#include <tarray.h>

template<typename T>
class FAutoSeg
{ // register things automatically without segment hackery

	template <typename T2>
	struct ArgumentType;

	template <typename Ret, typename Func, typename Arg>
	struct ArgumentType<Ret(Func:: *)(Arg) const>
	{
		using Type = Arg;
	};

	template <typename Func>
	using ArgumentTypeT = typename ArgumentType<Func>::Type;

	template <typename Func>
	struct ReturnType
	{
		using Type = std::invoke_result_t<Func, ArgumentTypeT<decltype(&Func::operator())>>;
	};

	template <typename Func>
	using ReturnTypeT = typename ReturnType<Func>::Type;

	template <typename Func, typename Ret>
	struct HasReturnType
	{
		static constexpr bool Value = std::is_same_v<ReturnTypeT<Func>, Ret>;
	};

	template <typename Func, typename Ret>
	static constexpr bool HasReturnTypeV = HasReturnType<Func, Ret>::Value;

public:
	TArray<T*> fields {TArray<T*>::NoInit}; // skip constructor for fields, globals are zero-initialized, so this is fine

	template <typename Func>
	void ForEach(Func func, std::enable_if_t<HasReturnTypeV<Func, void>> * = nullptr)
	{
		for (T * elem : fields)
		{
			func(elem);
		}
	}

	template <typename Func>
	void ForEach(Func func, std::enable_if_t<HasReturnTypeV<Func, bool>> * = nullptr)
	{
		for (T * elem : fields)
		{
			if (!func(elem))
			{
				return;
			}
		}
	}
};

template<typename T>
struct FAutoSegEntry
{
	FAutoSegEntry(FAutoSeg<T> &seg, T* value)
	{
		seg.fields.push_back(value);
	}
};

struct AFuncDesc;
struct FieldDesc;
struct ClassReg;
struct FPropertyInfo;
struct FMapOptInfo;
struct FCVarDecl;

namespace AutoSegs
{
	extern FAutoSeg<AFuncDesc> ActionFunctons;
	extern FAutoSeg<FieldDesc> ClassFields;
	extern FAutoSeg<ClassReg> TypeInfos;
	extern FAutoSeg<FPropertyInfo> Properties;
	extern FAutoSeg<FMapOptInfo> MapInfoOptions;
	extern FAutoSeg<FCVarDecl> CVarDecl;
}


#endif
