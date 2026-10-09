#include "about.h"

#include "backend/appData.h"
#include "backend/backend_core.h"   // GetBuildId
#include "backend/backend_picture.h"
#include "backend/metaCollection/metaObject.h"   // g_metaCommonMetadataCLSID — the configuration's picture
#include "backend/plugin/pluginManager.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"

void ibSchemaAbout::Build(const ibSession& session, ibDataNode& result) const
{
	const ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();

	result.SetValue(wxT("Build"), static_cast<s32>(GetBuildId()));
	result.SetValue(wxT("Picture"), wxString(ibBackendPicture::GetServerPicture(g_metaCommonMetadataCLSID).GetData()));
	result.SetValue(wxT("User"), session.GetUserInfo().m_strUserName);
	if (applicationInstance == nullptr)
		return;

	result.SetValue(wxT("Database"), applicationInstance->GetDatabaseDescription());
	result.SetValue(wxT("Application"), applicationInstance->GetRunModeDescr());
	result.SetValue(wxT("Locale"), applicationInstance->GetLocale());

	// The plugins the process loaded — the dialog showed its block only when there were any.
	ibDataNode& plugins = result.Child(wxT("Plugins"));
	if (const ibPluginManager* const pluginManager = applicationInstance->GetPluginManager()) {
		for (const ibPluginManager::LoadedPlugin& plugin : pluginManager->Loaded()) {
			ibDataNode& row = plugins.AddChild(0, 0);
			row.SetValue(wxT("Name"), wxString::FromUTF8(plugin.m_info->name != nullptr ? plugin.m_info->name : "?"));
			row.SetValue(wxT("Version"), wxString::FromUTF8(plugin.m_info->version != nullptr ? plugin.m_info->version : ""));
		}
	}
}
