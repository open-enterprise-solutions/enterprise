#include "about.h"

#include "backend/appData.h"
#include "backend/backend_core.h"   // GetBuildId
#include "backend/metaCollection/metaObject.h"   // g_metaCommonMetadataCLSID — the configuration's picture
#include "backend/plugin/pluginManager.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"
#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — the picture, by its id

void ibSchemaAbout::Build(const ibSession& session, const ibDataNode& /*args*/, ibDataNode& result) const
{
	const ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();

	result.SetValue(ibProtocol::ibSchema::ibAbout::Build, static_cast<std::int32_t>(GetBuildId()));
	result.SetValue(ibProtocol::ibSchema::ibAbout::Picture, ibBackendPicture::SendPicture(g_metaCommonMetadataCLSID));
	result.SetValue(ibProtocol::ibSchema::ibAbout::User, session.GetUserInfo().m_strUserName);
	if (applicationInstance == nullptr)
		return;

	result.SetValue(ibProtocol::ibSchema::ibAbout::Database, applicationInstance->GetDatabaseDescription());
	result.SetValue(ibProtocol::ibSchema::ibAbout::Application, applicationInstance->GetRunModeDescr());
	result.SetValue(ibProtocol::ibSchema::ibAbout::Locale, applicationInstance->GetLocale());

	// The plugins the process loaded — the dialog showed its block only when there were any.
	ibDataNode& plugins = result.Child(ibProtocol::ibSchema::ibAbout::Plugins);
	if (const ibPluginManager* const pluginManager = applicationInstance->GetPluginManager()) {
		for (const ibPluginManager::LoadedPlugin& plugin : pluginManager->Loaded()) {
			ibDataNode& row = plugins.AddChild(0, 0);
			row.SetValue(ibProtocol::ibSchema::ibAbout::Name, wxString::FromUTF8(plugin.m_info->name != nullptr ? plugin.m_info->name : "?"));
			row.SetValue(ibProtocol::ibSchema::ibAbout::Version, wxString::FromUTF8(plugin.m_info->version != nullptr ? plugin.m_info->version : ""));
		}
	}
}
