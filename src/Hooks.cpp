#include "Hooks.h"
#include "Manager.h"

namespace Hooks
{
	struct LoadGame
	{
		static void thunk(RE::IngredientItem* a_this, RE::BGSLoadFormBuffer* a_buf)
		{
			func(a_this, a_buf);

			// PreloadGame fires too early
			Manager::GetSingleton()->UnlearnIngredientEffects(a_this);
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static inline std::size_t                      idx = 0x0F;
	};

	void Install()
	{
		logger::info("{:*^30}", "HOOKS");

		stl::write_vfunc<RE::IngredientItem, LoadGame>();

		logger::info("Installed Ingredient::LoadGame hooks");
	}
}
