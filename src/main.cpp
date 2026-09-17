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

#ifdef SKYRIM_SUPPORT_AE
constexpr REL::Version MIN_ADDRESS_LIBRARY_V5_RUNTIME{ 1, 7, 99, 0 };

SKSE_PLUGIN_VERSION = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion(REL::Version{ Version::MAJOR, Version::MINOR, Version::PATCH });
	v.PluginName("Alchemy Effects Randomizer");
	v.AuthorName("powerofthree");
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });

	if constexpr (SKSE::RUNTIME_SSE_LATEST < MIN_ADDRESS_LIBRARY_V5_RUNTIME) {
		v.MinimumRequiredXSEVersion(REL::Version{ 2, 2, 5 });
	} else {
		v.MinimumRequiredXSEVersion(REL::Version{ 2, 3, 0 });
	}

	return v;
}();
#else
SKSE_PLUGIN_QUERY(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "Alchemy Effects Randomizer";
	a_info->version = Version::MAJOR;

	if (a_skse->IsEditor()) {
		REX::CRITICAL("Loaded in editor, marking as incompatible");
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (ver
#	ifndef SKYRIMVR
		< SKSE::RUNTIME_SSE_1_5_39
#	else
		> SKSE::RUNTIME_VR_1_4_15_1
#	endif
	) {
		REX::CRITICAL("Unsupported runtime version {}", ver);
		return false;
	}

	return true;
}
#endif

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .log = true,
						   .logName = Version::PROJECT.data(),
						   .trampoline = true,
						   .trampolineSize = 14 * 5 });

	const auto runtimeVersion = a_skse->RuntimeVersion();

	REX::INFO("Game version : {} (built for {})", runtimeVersion, SKSE::RUNTIME_SSE_LATEST);

#ifdef SKYRIM_SUPPORT_AE
	if constexpr (SKSE::RUNTIME_SSE_LATEST < MIN_ADDRESS_LIBRARY_V5_RUNTIME) {
		if (runtimeVersion >= MIN_ADDRESS_LIBRARY_V5_RUNTIME) {
			REX::FAIL(
				"You are using a newer version of Skyrim than this version of {0} supports.\n"
				"Install the correct version of {0} for your game version.\n"
				"Runtime: {1}\n"
				"Supported: 1.6.1170 (Steam) / 1.6.1179 (GOG)",
				Version::PROJECT, runtimeVersion);
		}
	}
#endif

	auto messaging = SKSE::GetMessagingInterface();
	messaging->RegisterListener("SKSE", OnInit);

	return true;
}
