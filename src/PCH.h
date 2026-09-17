#pragma once

#define NOMINMAX

#include <future>
#include <unordered_set>

#include "RE/Skyrim.h"
#include "REX/REX.h"
#include "SKSE/SKSE.h"

#include "ClibUtil/distribution.hpp"
#include <glaze/glaze.hpp>
#include <boost/unordered/unordered_node_map.hpp>
#include <boost/unordered/unordered_node_set.hpp>
#include <spdlog/sinks/basic_file_sink.h>

#include "ClibUtil/editorID.hpp"

namespace dist = clib_util::distribution;
namespace edid = clib_util::editorID;

using namespace std::literals;

template <class K, class D, class H = boost::hash<K>, class KEqual = std::equal_to<K>>
using Map = boost::unordered_node_map<K, D, H, KEqual>;

template <class K, class H = boost::hash<K>, class KEqual = std::equal_to<K>>
using Set = boost::unordered_node_set<K, H, KEqual>;

struct string_hash
{
	using is_transparent = void;

	std::size_t operator()(const char* str) const
	{
		return boost::hash<std::string_view>{}(str);
	}

	std::size_t operator()(std::string_view str) const
	{
		return boost::hash<std::string_view>{}(str);
	}

	std::size_t operator()(const std::string& str) const
	{
		return boost::hash<std::string>{}(str);
	}
};

template <class D>
using StringMap = Map<std::string, D, string_hash, std::equal_to<>>;

using StringSet = Set<std::string, string_hash, std::equal_to<>>;

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
