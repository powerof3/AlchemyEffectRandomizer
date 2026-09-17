#include "Manager.h"
#include "Hooks.h"

#include <SimpleIni.h>
#undef ERROR

void Manager::LoadSettings()
{
	const auto store = REX::FIniSettingStore::GetSingleton();
	store->Init(path.data(), "");

	store->Load();
	store->Save();

	fixedSeed = REX::STR::TO_NUM<std::uint64_t>(stl::get_setting_ref(fixedSeedStr));
}

void Manager::LoadBlacklist()
{
	REX::INFO("{:*^30}", "INI");

	const auto folderPath = std::format(R"(Data\{})", folder);

	std::error_code ec;
	if (!std::filesystem::exists(folderPath, ec)) {
		REX::INFO("{} folder not found...", folder);
		return;
	}

	const auto configs = dist::get_configs(folderPath);

	if (configs.empty()) {
		REX::WARN("No .ini files were found in {} folder, aborting...", folderPath);
		return;
	}

	REX::INFO("{} matching inis found", configs.size());

	for (auto& config : configs) {
		REX::INFO("\tINI : {}", config);

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.SetAllowKeyOnly();

		if (const auto rc = ini.LoadFile(config.c_str()); rc < 0) {
			REX::ERROR("\tcouldn't read INI");
			continue;
		}

		if (const auto values = ini.GetSection("Blacklist"); values && !values->empty()) {
			REX::INFO("\t\t{} blacklist entries", values->size());
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

	std::string           buffer;
	[[maybe_unused]] auto ec = glz::read_file_json(ingredientKnownEffectsSaveMap, ingredientKnownEffectsPath, buffer);

	Hooks::Install();
}

void Manager::InitBlacklist()
{
	REX::INFO("{:*^30}", "LOADING BLACKLIST");

	for (auto& id : blacklistIDs) {
		if (auto form = RE::TESForm::LookupByEditorID<RE::IngredientItem>(id)) {
			blacklist.emplace(form);
		} else {
			REX::ERROR("Blacklist: skipped {} (couldn't find form)", id);
		}
	}

	REX::INFO("Blacklist: {} ingredients", blacklist.size());
}

void Manager::LoadIngredientEffects()
{
	REX::INFO("{:*^30}", "LOADING INGREDIENTS");

	if (const auto dataHandler = RE::TESDataHandler::GetSingleton()) {
		const auto& ingredients = dataHandler->GetFormArray<RE::IngredientItem>();

		originalEffectGroups.reserve(ingredients.size());
		for (const auto& ingredient : ingredients) {
			if (ingredient && !blacklist.contains(ingredient)) {
				if (ingredient->effects.size() == 4) {
					if (std::ranges::all_of(ingredient->effects, [](const auto* effect) { return effect && effect->baseEffect; })) {
						originalEffectGroups.emplace_back(ingredient->effects.begin(), ingredient->effects.end());
					} else {
						REX::INFO("{} has null effect groups, skipping", edid::get_editorID(ingredient));
						blacklist.emplace(ingredient);
					}
				} else {
					REX::INFO("{} has nonstandard effect groups, skipping (size : {})", edid::get_editorID(ingredient), ingredient->effects.size());
					blacklist.emplace(ingredient);
				}
			}
		}
	}

	REX::INFO("EffectGroups: {} ({} effects)", originalEffectGroups.size(), originalEffectGroups.size() * 4);
	REX::INFO("Blacklist: {} ingredients", blacklist.size());
}

void Manager::OnDataLoad()
{
	InitBlacklist();
	LoadIngredientEffects();

	RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(GetSingleton());

	if ((GetShuffleOn() == SHUFFLE_ON::kGameLoad && (fixedSeed == 0 || !unlearnIngredients)) || GetShuffleOn() == SHUFFLE_ON::kAlchemyMenu) {
		ShuffleIngredientEffects(shuffledEffectGroups);
	}

	REX::INFO("{:*^30}", "LOAD/SAVE");
}

void Manager::ApplyEffectGroups(const IngredientEffectGroups& a_effectGroups) const
{
	if (const auto dataHandler = RE::TESDataHandler::GetSingleton()) {
		std::size_t outerIdx = 0;
		for (const auto& ingredient : dataHandler->GetFormArray<RE::IngredientItem>()) {
			if (ingredient && !blacklist.contains(ingredient)) {
				std::size_t innerIdx = 0;  // serves as effect idx
				for (auto& effect : ingredient->effects) {
					effect = a_effectGroups[outerIdx][innerIdx];
					innerIdx++;
				}
				if (GetShuffleOn() != SHUFFLE_ON::kPlaythrough) {
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

	return GetShuffleOn() == SHUFFLE_ON::kPlaythrough || (GetShuffleOn() == SHUFFLE_ON::kGameLoad && fixedSeed != 0) ? (*a_effectKnownFlag & (1u << a_effectIdx)) == 0 : true;
}

void Manager::UnlearnIngredientEffects(RE::IngredientItem* a_ingredient) const
{
	if (!unlearnIngredients || blacklist.contains(a_ingredient)) {
		return;
	}

	if (IsSessionScoped() && currentSaveMatchesShuffle) {
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

	switch (GetShuffleOn()) {
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
	REX::TRandom<std::uint64_t> local_rng(a_seed);

	switch (GetShuffleMethod()) {
	case SHUFFLE_METHOD::kSwap:
		{
			// swap effect groups around
			std::ranges::shuffle(a_effectGroups, local_rng);
		}
		break;
	case SHUFFLE_METHOD::kShuffle:
		{
			constexpr auto shuffle_effects = [](IngredientEffectGroups& a_ingredients, REX::TRandom<std::uint64_t>& a_rng) {
				// flatten
				auto effects = a_ingredients | std::views::join | std::ranges::to<IngredientEffects>();
				// shuffle
				std::ranges::shuffle(effects, a_rng);
				// restore
				a_ingredients = effects | std::views::chunk(4) | std::ranges::to<IngredientEffectGroups>();
			};

			constexpr auto is_distribution_unique = [](const IngredientEffectGroups& a_effectGroups) {
				for (const auto& effectGroup : a_effectGroups) {
					Set<RE::EffectSetting*> set{};
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
			const auto threads = std::max<std::size_t>(1, std::thread::hardware_concurrency());
			const auto num_chunks = std::max<std::size_t>(1, a_effectGroups.size() / threads);
			auto       ingredient_chunks = a_effectGroups | std::views::chunk(num_chunks) | std::ranges::to<std::vector<IngredientEffectGroups>>();

			// shuffle until unique
			std::vector<std::future<void>> futures;
			futures.reserve(ingredient_chunks.size());
			for (auto& chunk : ingredient_chunks) {
				futures.emplace_back(std::async(std::launch::async, [&] {
					REX::TRandom<std::uint64_t> threadRNG(a_seed);
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

void Manager::ShuffleIngredientEffects(ShuffledIngredientEffectGroups& a_effectGroups, bool a_reshuffle)
{
	auto& [ingredientEffectGroup, shuffled] = a_effectGroups;
	if (ingredientEffectGroup.empty()) {
		ingredientEffectGroup.assign(originalEffectGroups.begin(), originalEffectGroups.end());
	}
	if (ingredientEffectGroup.empty()) {
		return;
	}
	const auto seed = GetRNGSeed();
	if (!shuffled || a_reshuffle) {
		shuffle_effect_groups(seed, ingredientEffectGroup);
		shuffleGeneration++;
		currentSaveMatchesShuffle = false;
	}
	if (!shuffled || a_reshuffle || GetShuffleOn() == SHUFFLE_ON::kPlaythrough) {
		ApplyEffectGroups(ingredientEffectGroup);
		REX::INFO("\tShuffled {} ingredient effects ({} individual effects | RNG seed : {})", ingredientEffectGroup.size(), ingredientEffectGroup.size() * 4, seed);
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
	switch (GetShuffleOn()) {
	case SHUFFLE_ON::kGameLoad:
		return fixedSeed != 0 && unlearnIngredients;
	case SHUFFLE_ON::kPlaythrough:
		return !a_saveLoad || GetCurrentPlayerID() != oldPlayerID;
	default:
		return false;
	}
}

std::uint64_t Manager::get_game_playerID()
{
	return RE::BGSSaveLoadManager::GetSingleton()->currentCharacterID & 0xFFFFFFFF;
}

std::uint64_t Manager::save_to_playerID(const std::string& a_savePath)
{
	if (const auto save = clib_util::string::split(a_savePath, "_"); save.size() == 9) {
		return REX::STR::TO_NUM<std::uint64_t>(save[1], true);
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

	const auto sessionIt = sessionSaves.find(currentSave);
	currentSaveMatchesShuffle = sessionIt != sessionSaves.end() && sessionIt->second == shuffleGeneration;

	REX::INFO("Loaded : {} | {} ingredients known{}", a_savePath, currentIngredientKnownEffectsMap.size(),
		IsSessionScoped() ? (currentSaveMatchesShuffle ? " | saved this session, keeping known effects" : " | saved under a different shuffle, unlearning") : "");

	if (ShouldShuffleOnLoadSaveOrNewGame(true)) {
		ShuffleIngredientEffects(GetShuffleOn() == SHUFFLE_ON::kPlaythrough ? playthroughEffectGroupMap[currentPlayerID] : shuffledEffectGroups);
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

	REX::INFO("Save: {} | {} ingredients known", a_savePath, currentIngredientKnownEffectsMap.size());

	ingredientKnownEffectsSaveMap[currentSave] = currentIngredientKnownEffectsMap;
	sessionSaves[currentSave] = shuffleGeneration;

	std::string           buffer;
	[[maybe_unused]] auto ec = glz::write_file_json(ingredientKnownEffectsSaveMap, ingredientKnownEffectsPath, buffer);
}

void Manager::OnDeleteSave(const std::string& a_savePath)
{
	ingredientKnownEffectsSaveMap.erase(a_savePath);
	sessionSaves.erase(a_savePath);
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
				ShuffleIngredientEffects(GetShuffleOn() == SHUFFLE_ON::kPlaythrough ? playthroughEffectGroupMap[currentPlayerID] : shuffledEffectGroups);
			});
			newGameStarted = false;
		}
	} else if (a_event->menuName == RE::CraftingMenu::MENU_NAME && GetShuffleOn() == SHUFFLE_ON::kAlchemyMenu) {
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
