#pragma once

using IngredientEffects = std::vector<RE::Effect*>;
using IngredientEffectGroups = std::vector<IngredientEffects>;

using IngredientKnownEffectsMap = std::unordered_map<std::string, std::uint16_t>;  // IngredientEDID -> KnownEffectFlags

struct ShuffledIngredientEffectGroups
{
	IngredientEffectGroups groups{};
	bool                   shuffled{ false };
};

class Manager :
	public REX::TSingleton<Manager>,
	public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
	public RE::BSTEventSink<RE::ItemCrafted::Event>
{
public:
	enum class SHUFFLE_METHOD
	{
		kSwap,
		kShuffle
	};

	enum class SHUFFLE_ON
	{
		kGameLoad,
		kPlaythrough,
		kAlchemyMenu
	};

	void OnPostLoad();
	void OnDataLoad();

	void OnLoad(const std::string& a_savePath);
	void OnSave(const std::string& a_savePath);
	void OnDeleteSave(const std::string& a_savePath);
	void OnNewGame();

	void ShuffleIngredientEffects(ShuffledIngredientEffectGroups& a_effectGroups, bool a_reshuffle = false) const;
	void UnlearnIngredientEffects(RE::IngredientItem* a_ingredient) const;

private:
	void LoadSettings();
	void LoadBlacklist();
	void InitBlacklist();
	void LoadIngredientEffects();

	[[nodiscard]] SHUFFLE_METHOD GetShuffleMethod() const { return static_cast<SHUFFLE_METHOD>(shuffleMethod.GetValue()); }
	[[nodiscard]] SHUFFLE_ON     GetShuffleOn() const { return static_cast<SHUFFLE_ON>(shuffleOn.GetValue()); }

	std::uint64_t GetCurrentPlayerID();
	void          GetPlayerIDFromSave();
	bool          ShouldShuffleOnLoadSaveOrNewGame(bool a_saveLoad);

	std::uint64_t GetRNGSeed(bool a_onDataLoad = false) const;
	void          ApplyEffectGroups(const IngredientEffectGroups& a_effectGroups) const;

	static std::uint64_t get_game_playerID();
	static std::uint64_t save_to_playerID(const std::string& a_savePath);
	void                 shuffle_effect_groups(std::uint64_t a_seed, IngredientEffectGroups& a_effectGroups) const;
	[[nodiscard]] bool   can_unlearn_effect(const std::optional<std::uint16_t>& a_effectKnownFlag, std::uint32_t a_effectIdx) const;

	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::ItemCrafted::Event* a_event, RE::BSTEventSource<RE::ItemCrafted::Event>*) override;

	// members
	std::string folder{ "AlchemyEffectRandomizer" };
	std::string ingredientKnownEffectsPath{ R"(Data\AlchemyEffectRandomizer\IngredientKnownEffects.json)" };

	static constexpr std::string_view path = R"(Data\SKSE\Plugins\po3_AlchemyEffectRandomizer.ini)";

	std::unordered_set<std::string>         blacklistIDs;  // EDID
	std::unordered_set<RE::IngredientItem*> blacklist;     // IngredientItem

	REX::TIniSetting<std::uint32_t> shuffleMethod{ "Settings", "iRandomMethod", std::to_underlying(SHUFFLE_METHOD::kShuffle) };
	REX::TIniSetting<std::uint32_t> shuffleOn{ "Settings", "iRandomizeOn", std::to_underlying(SHUFFLE_ON::kPlaythrough) };
	REX::TIniSetting<bool>          unlearnIngredients{ "Settings", "bUnlearnIngredients", false };
	
	REX::TIniSetting<std::string>   fixedSeedStr{ "Settings", "iSeed", "0" };
	std::uint64_t                   fixedSeed{ 0 };

	IngredientEffectGroups         originalEffectGroups;
	ShuffledIngredientEffectGroups shuffledEffectGroups;  // gameload/static

	bool          newGameStarted{ false };
	std::string   currentSave{};
	std::uint64_t currentPlayerID{ std::numeric_limits<std::uint64_t>::max() };
	std::uint64_t oldPlayerID{ std::numeric_limits<std::uint64_t>::max() };

	std::unordered_map<std::uint64_t, ShuffledIngredientEffectGroups> playthroughEffectGroupMap;  // playerID -> IngredientEffectGroups

	bool isAlchemyMenu{ false };
	bool hasCraftedPotion{ false };

	std::unordered_map<std::string, IngredientKnownEffectsMap> ingredientKnownEffectsSaveMap;
	IngredientKnownEffectsMap                                  currentIngredientKnownEffectsMap;
};
