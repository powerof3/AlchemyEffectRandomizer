#include "Manager.h"
#include "Hooks.h"

void Manager::LoadSettings()
{
	const auto path = std::format("Data/SKSE/Plugins/po3_{}.ini", folder);

	CSimpleIniA ini;
	ini.SetUnicode();

	ini.LoadFile(path.c_str());

	ini::get_value(ini, shuffleMethod, "Settings", "iRandomMethod", ";Method\n;0 - Swap (ingredient effect groups with each other)\n;1 - Shuffle (all effects across each ingredient)");
	ini::get_value(ini, shuffleOn, "Settings", "iRandomizeOn", ";When to apply the randomizer\n;0 - Game Load (randomized on game load)\n;1 - Playthrough (randomized across different playthroughs)\n;2 - Alchemy Menu (randomized on game load and every time you craft a potion!)");
	ini::get_value(ini, unlearnIngredients, "Settings", "bUnlearnIngredients", ";Unlearn all ingredients upon randomization (for Playthrough mode, this happens only once).");
	ini::get_value(ini, fixedSeed, "Settings", "iSeed", ";Fixed RNG seed (for OnGameLoad randomization). If 0, ingredients will have different effects on each game load.");

	(void)ini.SaveFile(path.c_str());
}

void Manager::LoadBlacklist()
{
	logger::info("{:*^30}", "INI");

	const auto folderPath = std::format(R"(Data\{})", folder);

	if (!std::filesystem::exists(folderPath)) {
		logger::info("{} folder not found...", folder);
		return;
	}

	const auto configs = dist::get_configs(folderPath);

	if (configs.empty()) {
		logger::warn("No .ini files were found in {} folder, aborting...", folderPath);
		return;
	}

	logger::info("{} matching inis found", configs.size());

	for (auto& path : configs) {
		logger::info("\tINI : {}", path);

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.SetAllowKeyOnly();

		if (const auto rc = ini.LoadFile(path.c_str()); rc < 0) {
			logger::error("	couldn't read INI");
			continue;
		}

		if (const auto values = ini.GetSection("Blacklist"); values && !values->empty()) {
			logger::info("\t\t{} blacklist entries", values->size());
			for (const auto& key : *values | std::views::keys) {
				blacklistIDs.emplace(key.pItem);
			}
		}
	}
}

void Manager::OnPostLoad()
{
	LoadSettings();
	LoadBlacklist();

	glz::read_file(ingredientKnownEffectsSaveMap, ingredientKnownEffectsPath, std::string());

	Hooks::Install();
}

void Manager::InitBlacklist()
{
	for (auto& id : blacklistIDs) {
		if (auto form = RE::TESForm::LookupByEditorID<RE::IngredientItem>(id)) {
			blacklist.emplace(form);
		} else {
			logger::error("Blacklist: skipped {} (couldn't find form)", id);
		}
	}
}

void Manager::LoadIngredientEffects()
{
	if (const auto dataHandler = RE::TESDataHandler::GetSingleton()) {
		const auto& ingredients = dataHandler->GetFormArray<RE::IngredientItem>();

		originalEffectGroups.reserve(ingredients.size());
		for (const auto& ingredient : ingredients) {
			if (ingredient && !blacklist.contains(ingredient)) {
				originalEffectGroups.emplace_back(ingredient->effects.begin(), ingredient->effects.end());
			}
		}
	}
}

void Manager::OnDataLoad()
{
	logger::info("{:*^30}", "DATA LOAD");

	InitBlacklist();
	LoadIngredientEffects();

	RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(GetSingleton()); 

	if ((shuffleOn == SHUFFLE_ON::kGameLoad && (fixedSeed == 0 || !unlearnIngredients)) || shuffleOn == SHUFFLE_ON::kAlchemyMenu) {
		ShuffleIngredientEffects(shuffledEffectGroups);
	}

	logger::info("{:*^30}", "LOAD/SAVE");
}

void Manager::ApplyEffectGroups(const IngredientEffectGroups& a_effectGroups) const
{
    if (const auto dataHandler = RE::TESDataHandler::GetSingleton()) {
		std::uint32_t outerIdx = 0;
		for (const auto& ingredient : dataHandler->GetFormArray<RE::IngredientItem>()) {
			if (ingredient && !blacklist.contains(ingredient)) {
				std::uint32_t innerIdx = 0;  // serves as effect idx
				for (auto& effect : ingredient->effects) {
					effect = a_effectGroups[outerIdx][innerIdx];
					innerIdx++;
				}
				if (shuffleOn != SHUFFLE_ON::kPlaythrough) {
					UnlearnIngredientEffects(ingredient);
				}
				outerIdx++;
			}
		}
	}
}

bool Manager::can_unlearn_effect(const std::optional<std::uint16_t>& a_effectKnownFlag, std::uint32_t a_effectIdx) const
{
	if (!a_effectKnownFlag) {
		return true;
	}

	return shuffleOn == SHUFFLE_ON::kPlaythrough || (shuffleOn == SHUFFLE_ON::kGameLoad && fixedSeed != 0) ? (*a_effectKnownFlag && a_effectIdx) == 0 : true;
}

void Manager::UnlearnIngredientEffects(RE::IngredientItem* a_ingredient) const
{
	if (!unlearnIngredients || blacklist.contains(a_ingredient)) {
		return;
	}

	std::optional<std::uint16_t> knowEffectFlags{};
	if (const auto it = currentIngredientKnownEffectsMap.find(edid::get_editorID(a_ingredient)); it != currentIngredientKnownEffectsMap.end()) {
		knowEffectFlags = it->second;
	}
	for (std::uint32_t index = 0; index < 4; ++index) {
		if (can_unlearn_effect(knowEffectFlags, index)) {
			a_ingredient->gamedata.knownEffectFlags &= ~(1 << index);
		}
	}
}

std::uint64_t Manager::GetRNGSeed(bool a_onDataLoad) const
{
	const auto get_fixed_seed = [this]() {
		return fixedSeed != 0 ? fixedSeed : std::chrono::steady_clock::now().time_since_epoch().count();
	};

	switch (shuffleOn) {
	case SHUFFLE_ON::kGameLoad:
		return get_fixed_seed();
	case SHUFFLE_ON::kAlchemyMenu:
		return a_onDataLoad ? get_fixed_seed() : std::chrono::steady_clock::now().time_since_epoch().count();
	case SHUFFLE_ON::kPlaythrough:
		return currentPlayerID;
	default:
		return std::chrono::steady_clock::now().time_since_epoch().count();
	}
}

void Manager::shuffle_effect_groups(const std::uint64_t a_seed, IngredientEffectGroups& a_effectGroups) const
{
	RNG local_rng(a_seed);

	switch (shuffleMethod) {
	case SHUFFLE_METHOD::kSwap:
		{
			// swap effect groups around
			std::ranges::shuffle(a_effectGroups, local_rng);
		}
		break;
	case SHUFFLE_METHOD::kShuffle:
		{
			constexpr auto shuffle_effects = [](IngredientEffectGroups& a_ingredients, RNG& a_rng) {
				// flatten
				auto effects = a_ingredients | std::views::join | std::ranges::to<IngredientEffects>();
				// shuffle
				std::ranges::shuffle(effects, a_rng);
				// restore
				a_ingredients = effects | std::views::chunk(4) | std::ranges::to<IngredientEffectGroups>();
			};

			constexpr auto is_distribution_unique = [](const IngredientEffectGroups& a_effectGroups) {
				for (const auto& effectGroup : a_effectGroups) {
					std::unordered_set<RE::EffectSetting*> set{};
					for (const auto& effect : effectGroup) {
						if (!set.emplace(effect->baseEffect).second) {
							return false;
						}
					}
				}
				return true;
			};

			// initial shuffle, distribution probably contains duplicates
			shuffle_effects(a_effectGroups, local_rng);

			// divide into chunks
			auto num_chunks = static_cast<std::size_t>(a_effectGroups.size() / std::thread::hardware_concurrency());
			auto ingredient_chunks = a_effectGroups | std::views::chunk(num_chunks) | std::ranges::to<std::vector<IngredientEffectGroups>>();

			// shuffle until unique
			std::vector<std::future<void>> futures;
			for (auto& chunk : ingredient_chunks) {
				futures.emplace_back(std::async(std::launch::async, [&] {
					RNG threadRNG(a_seed);
					while (!is_distribution_unique(chunk)) {
						shuffle_effects(chunk, threadRNG);
					}
				}));
			}

			for (auto& future : futures) {
				future.wait();
			}

			// Rejoin shuffled chunks
			a_effectGroups = std::views::join(ingredient_chunks) | std::ranges::to<IngredientEffectGroups>();
		}
		break;
	default:
		break;
	}
}

void Manager::ShuffleIngredientEffects(ShuffledIngredientEffectGroups& a_effectGroups, bool a_reshuffle) const
{
	auto& [ingredientEffectGroup, shuffled] = a_effectGroups;
	if (ingredientEffectGroup.empty()) {
		ingredientEffectGroup = originalEffectGroups;
	}
	const auto seed = GetRNGSeed();
	if (!shuffled || a_reshuffle) {
		shuffle_effect_groups(seed, ingredientEffectGroup);
	}
	if (!shuffled || a_reshuffle || shuffleOn == SHUFFLE_ON::kPlaythrough) {
		ApplyEffectGroups(ingredientEffectGroup);
		logger::info("\tShuffled {} ingredient effects ({} individual effects | RNG seed : {})", ingredientEffectGroup.size(), ingredientEffectGroup.size() * 4, seed);
	}
	shuffled = true;
}

std::uint64_t Manager::GetCurrentPlayerID()
{
	if (currentPlayerID == std::numeric_limits<std::uint64_t>::max()) {
		currentPlayerID = get_game_playerID();
	}

	return currentPlayerID;
}

void Manager::GetPlayerIDFromSave()
{
	oldPlayerID = currentPlayerID;
	currentPlayerID = save_to_playerID(currentSave);
}

bool Manager::ShouldShuffleOnLoadSaveOrNewGame(bool a_saveLoad)
{
	switch (shuffleOn) {
	case SHUFFLE_ON::kGameLoad:
		return fixedSeed != 0 && unlearnIngredients;
	case SHUFFLE_ON::kPlaythrough:
		return a_saveLoad && GetCurrentPlayerID() == oldPlayerID ? false : true;
	default:
		return false;
	}
}

std::uint64_t Manager::get_game_playerID()
{
	return RE::BGSSaveLoadManager::GetSingleton()->currentPlayerID & 0xFFFFFFFF;
}

std::uint64_t Manager::save_to_playerID(const std::string& a_savePath)
{
	if (const auto save = clib_util::string::split(a_savePath, "_"); save.size() == 9) {
		return clib_util::string::to_num<std::uint64_t>(save[1], true);
	} else {
		return std::numeric_limits<std::uint64_t>::max();  // non standard save name, use game playerID instead
	}
}

void Manager::OnLoad(const std::string& a_savePath)
{
	currentSave = a_savePath;
	GetPlayerIDFromSave();

	if (const auto it = ingredientKnownEffectsSaveMap.find(currentSave); it != ingredientKnownEffectsSaveMap.end()) {
		currentIngredientKnownEffectsMap = it->second;
	} else {
		currentIngredientKnownEffectsMap.clear();
	}

	logger::info("Loaded : {} | {} ingredients known", a_savePath, currentIngredientKnownEffectsMap.size());

	if (ShouldShuffleOnLoadSaveOrNewGame(true)) {
		ShuffleIngredientEffects(shuffleOn == SHUFFLE_ON::kPlaythrough ? playthroughEffectGroupMap[currentPlayerID] : shuffledEffectGroups);
	}
}

void Manager::OnSave(const std::string& a_savePath)
{
	currentSave = a_savePath;

	if (const auto dataHandler = RE::TESDataHandler::GetSingleton()) {
		for (const auto& ingredient : dataHandler->GetFormArray<RE::IngredientItem>()) {
			if (ingredient && !blacklist.contains(ingredient)) {
				if (ingredient->gamedata.knownEffectFlags != 0) {
					currentIngredientKnownEffectsMap[edid::get_editorID(ingredient)] = ingredient->gamedata.knownEffectFlags;
				}
			}
		}
	}

	logger::info("Save: {} | {} ingredients known", a_savePath, currentIngredientKnownEffectsMap.size());

	ingredientKnownEffectsSaveMap[currentSave] = currentIngredientKnownEffectsMap;

	[[maybe_unused]] auto ec = glz::write_file(ingredientKnownEffectsSaveMap, ingredientKnownEffectsPath, std::string());
}

void Manager::OnDeleteSave(const std::string& a_savePath)
{
	ingredientKnownEffectsSaveMap.erase(a_savePath);
}

void Manager::OnNewGame()
{
	newGameStarted = true;
}

RE::BSEventNotifyControl Manager::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (!a_event) {
		return RE::BSEventNotifyControl::kContinue;
	}

	if (a_event->menuName == RE::RaceSexMenu::MENU_NAME && newGameStarted && ShouldShuffleOnLoadSaveOrNewGame(false)) {
		if (a_event->opening) {
			oldPlayerID = get_game_playerID();
		} else {
			currentPlayerID = get_game_playerID();
			SKSE::GetTaskInterface()->AddTask([this]() {
				ShuffleIngredientEffects(shuffleOn == SHUFFLE_ON::kPlaythrough ? playthroughEffectGroupMap[currentPlayerID] : shuffledEffectGroups);
			});
			newGameStarted = false;
		}
	} else if (a_event->menuName == RE::CraftingMenu::MENU_NAME && shuffleOn == SHUFFLE_ON::kAlchemyMenu) {
		if (a_event->opening) {
			isAlchemyMenu = false;
			if (const auto craftingMenu = RE::UI::GetSingleton()->GetMenu<RE::CraftingMenu>(); craftingMenu && craftingMenu->subMenu) {
				RE::GFxValue text;
				craftingMenu->subMenu->craftingMenu.GetMember("_subtypeName", &text);
				isAlchemyMenu = clib_util::string::iequals(text.GetString(), "Alchemy");
			}
			if (isAlchemyMenu) {
				RE::ItemCrafted::GetEventSource()->AddEventSink(GetSingleton());
			}
		} else if (isAlchemyMenu && hasCraftedPotion) {
			hasCraftedPotion = false;
		    SKSE::GetTaskInterface()->AddTask([this]() {
				ShuffleIngredientEffects(shuffledEffectGroups, true);
			});
			RE::ItemCrafted::GetEventSource()->RemoveEventSink(GetSingleton());
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Manager::ProcessEvent(const RE::ItemCrafted::Event* a_event, RE::BSTEventSource<RE::ItemCrafted::Event>*)
{
	if (!a_event) {
		return RE::BSEventNotifyControl::kContinue;
	}

	if (isAlchemyMenu && a_event->item) {
		hasCraftedPotion = true;
	}

    return RE::BSEventNotifyControl::kContinue;
}
