#include "Manager.h"

void OnInit(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kPostLoad:
		Manager::GetSingleton()->OnPostLoad();
		break;
	case SKSE::MessagingInterface::kDataLoaded:
		Manager::GetSingleton()->OnDataLoad();
		break;
	case SKSE::MessagingInterface::kSaveGame:
		{
			const std::string savePath = { static_cast<char*>(a_msg->data), a_msg->dataLen };
			Manager::GetSingleton()->OnSave(savePath);
		}
		break;
	case SKSE::MessagingInterface::kPreLoadGame:
		{
			std::string savePath({ static_cast<char*>(a_msg->data), a_msg->dataLen });
			clib_util::string::replace_last_instance(savePath, ".ess", "");

			Manager::GetSingleton()->OnLoad(savePath);
		}
		break;
	case SKSE::MessagingInterface::kDeleteGame:
		{
			const std::string savePath({ static_cast<char*>(a_msg->data), a_msg->dataLen });
			Manager::GetSingleton()->OnDeleteSave(savePath);
		}
		break;
	case SKSE::MessagingInterface::kNewGame:
		Manager::GetSingleton()->OnNewGame();
		break;
	default:
		break;
	}
}

#ifdef SKYRIM_AE
extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion(Version::MAJOR);
	v.PluginName("AlchemyEffectsRandomizer");
	v.AuthorName("powerofthree");
	v.UsesAddressLibrary();
	v.UsesNoStructs();
	v.CompatibleVersions({ SKSE::RUNTIME_LATEST });

	return v;
}();
#else
extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "AlchemyEffectsRandomizer";
	a_info->version = Version::MAJOR;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (ver < SKSE::RUNTIME_1_5_39) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}

	return true;
}
#endif

void InitializeLog()
{
	auto path = logger::log_directory();
	if (!path) {
		stl::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%v"s);

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();

	logger::info("Game version : {}", a_skse->RuntimeVersion().string());

	SKSE::Init(a_skse);

	auto messaging = SKSE::GetMessagingInterface();
	messaging->RegisterListener("SKSE", OnInit);

	return true;
}
