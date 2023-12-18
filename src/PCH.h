#pragma once

#define NOMINMAX

#include <future>
#include <unordered_set>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include "ClibUtil/distribution.hpp"
#include "ClibUtil/rng.hpp"
#include "ClibUtil/simpleINI.hpp"
#include "ClibUtil/singleton.hpp"
#include "ClibUtil/timer.hpp"
#include <glaze/glaze.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <ankerl/unordered_dense.h>

#include "ClibUtil/editorID.hpp"

#define DLLEXPORT __declspec(dllexport)

namespace logger = SKSE::log;
namespace ini = clib_util::ini;
namespace dist = clib_util::distribution;
namespace edid = clib_util::editorID;

using namespace std::literals;
using namespace clib_util::singleton;

using RNG = clib_util::RNG<XoshiroCpp::Xoshiro256StarStar>;
using Timer = clib_util::Timer;

namespace stl
{
	using namespace SKSE::stl;

	template <class F, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[0] };
		T::func = vtbl.write_vfunc(T::idx, T::thunk);
	}

	template <class T>
	void write_thunk_call(std::uintptr_t a_src)
	{
		auto& trampoline = SKSE::GetTrampoline();
		SKSE::AllocTrampoline(14);

		T::func = trampoline.write_call<5>(a_src, T::thunk);
	}

	template <class T>
	void write_thunk_jmp(std::uintptr_t a_src)
	{
		SKSE::AllocTrampoline(14);
		auto& trampoline = SKSE::GetTrampoline();

		T::func = trampoline.write_branch<5>(a_src, T::thunk);
	}
}

#ifdef SKYRIM_AE
#	define OFFSET(se, ae) ae
#else
#	define OFFSET(se, ae) se
#endif

#include "Version.h"
