#pragma once

#define NOMINMAX

#include <future>
#include <unordered_set>

#include "RE/Skyrim.h"
#include "REX/REX.h"
#include "SKSE/SKSE.h"

#include "ClibUtil/distribution.hpp"
#include <glaze/glaze.hpp>
#include <spdlog/sinks/basic_file_sink.h>

#include "ClibUtil/editorID.hpp"

namespace dist = clib_util::distribution;
namespace edid = clib_util::editorID;

using namespace std::literals;

namespace stl
{
	template <class F, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[0] };
		T::func = vtbl.write_vfunc(T::idx, T::thunk);
	}

	template <class T>
	void write_thunk_call(std::uintptr_t a_src)
	{
		auto& trampoline = REL::GetTrampoline();
		T::func = trampoline.write_call<5>(a_src, T::thunk);
	}

	template <class T>
	void write_thunk_jmp(std::uintptr_t a_src)
	{
		auto& trampoline = REL::GetTrampoline();
		T::func = trampoline.write_jmp<5>(a_src, T::thunk);
	}

	template <class T>
	T& get_setting_ref(REX::TSetting<T>& a_setting)
	{
		return static_cast<T&>(a_setting);
	}

	template <class T>
	const T& get_setting_ref(const REX::TSetting<T>& a_setting)
	{
		return static_cast<const T&>(a_setting);
	}
}

#ifdef SKYRIM_AE
#	define OFFSET(se, ae) ae
#else
#	define OFFSET(se, ae) se
#endif

#include "Version.h"
