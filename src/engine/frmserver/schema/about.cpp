#include "about.h"

#include "backend/appData.h"
#include "backend/backend_core.h"   // GetBuildId, OES_VERSION_* (core/build.h)
#include "backend/metaCollection/metaObject.h"   // g_metaApplicationMetadataCLSID — the application's picture
#include "backend/plugin/pluginManager.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"
#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — the picture, by its id

void ibSchemaAbout::Build(const ibSession& session, const ibDataNode& /*args*/, ibDataNode& result) const
{
	const ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();

	result.SetValue(ibProtocol::ibSchema::ibAbout::Build, static_cast<std::int32_t>(GetBuildId()));
	// The platform's version, on the root's Version as a plugin's row says its own (core/build.h).
	result.SetValue(ibProtocol::ibSchema::ibAbout::Version,
		ibString::Format(ibT("%d.%d.%d"), OES_VERSION_MAJOR, OES_VERSION_MINOR, OES_VERSION_RELEASE));
	result.SetValue(ibProtocol::ibSchema::ibAbout::Picture, ibBackendPicture::SendPicture(g_metaApplicationMetadataCLSID));
	result.SetValue(ibProtocol::ibSchema::ibAbout::User, session.GetUserInfo().m_strUserName);
	if (applicationInstance == nullptr)
		return;

	result.SetValue(ibProtocol::ibSchema::ibAbout::Database, applicationInstance->GetDatabaseDescription());
	result.SetValue(ibProtocol::ibSchema::ibAbout::Program, applicationInstance->GetRunModeDescr());
	result.SetValue(ibProtocol::ibSchema::ibAbout::Locale, applicationInstance->GetLocale());

	// The plugins the process loaded — the dialog showed its block only when there were any.
	ibDataNode& plugins = result.Child(ibProtocol::ibSchema::ibAbout::Plugins);
	if (const ibPluginManager* const pluginManager = applicationInstance->GetPluginManager()) {
		for (const ibPluginManager::LoadedPlugin& plugin : pluginManager->Loaded()) {
			ibDataNode& row = plugins.AddChild(0, 0);
			row.SetValue(ibProtocol::ibSchema::ibAbout::Name, ibString::FromUTF8(plugin.m_info->name != nullptr ? plugin.m_info->name : "?"));
			row.SetValue(ibProtocol::ibSchema::ibAbout::Version, ibString::FromUTF8(plugin.m_info->version != nullptr ? plugin.m_info->version : ""));
		}
	}
}
