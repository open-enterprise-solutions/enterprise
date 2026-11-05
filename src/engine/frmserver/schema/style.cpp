#include "style.h"

#include "backend/appData.h"                       // ibApplicationInstance::GetSettingsStorage
#include "backend/session/session.h"
#include "backend/settings/settingsComposer.h"     // ibUserSettingsKey - the person's row
#include "backend/settings/settingsStorage.h"
#include "core/localization/dictionary.h"          // _() - its words
#include "core/serialize/dataBuilder.h"

#include <initializer_list>

namespace {

// THE PLATFORM'S LOOK - the one object the row of the style is kept under; what is configured here is the platform's,
// not something of the configuration's tree.
const ibGuid& StyleObject()
{
	static const ibGuid s_object(ibT("7374796c-6500-0000-0000-000000000001"));
	return s_object;
}

// A theme by its value, or false when the value is no theme: a number, whole, and one of the three.
bool ThemeOf(const ibDataValue& value, ibProtocol::ibSchema::ibStyle::ibTheme& theme)
{
	if (value.Kind() != ibDataKind::Number)
		return false;
	const ibNumber number = value.AsNumber();
	for (const ibProtocol::ibSchema::ibStyle::ibTheme known : { ibProtocol::ibSchema::ibStyle::ibTheme::System,
		ibProtocol::ibSchema::ibStyle::ibTheme::Light, ibProtocol::ibSchema::ibStyle::ibTheme::Dark }) {
		if (number == ibNumber(static_cast<std::int32_t>(known))) {
			theme = known;
			return true;
		}
	}
	return false;
}

} // namespace

ibProtocol::ibSchema::ibStyle::ibTheme ibSchemaStyle::ReadTheme(const ibSession& session)
{
	ibProtocol::ibSchema::ibStyle::ibTheme theme = ibProtocol::ibSchema::ibStyle::ibTheme::System;
	const ibSettingsStorage* const storage = ibApplicationInstance::GetSettingsStorage(session.GetApplicationInstance());
	ibDataNode row;
	if (storage == nullptr || !storage->Restore(ibUserSettingsKey(ibSettingsCategory::Style, StyleObject(), wxNullGuid,
		&session.GetUserInfo()), row))
		return theme;
	// A row whose value is no theme - a number a later build wrote, or no number at all - is read as the system's, not
	// refused: a look is no reason to stop a window.
	const auto stored = row.FindField(ibProtocol::ibSchema::ibStyle::Theme);
	if (!stored || !ThemeOf(*stored, theme))
		theme = ibProtocol::ibSchema::ibStyle::ibTheme::System;
	return theme;
}

void ibSchemaStyle::Build(const ibSession& session, const ibDataNode& /*args*/, ibDataNode& result) const
{
	result.SetValue(ibProtocol::ibSchema::ibStyle::Theme, static_cast<std::int32_t>(ReadTheme(session)));
}

std::function<void()> ibSchemaStyle::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	if (static_cast<ibProtocol::ibSchema::ibCommand>(command) != ibProtocol::ibSchema::ibCommand::Save) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibT("the style takes Save alone");
		return nullptr;
	}
	const auto given = args.FindField(ibProtocol::ibSchema::ibStyle::Theme);
	ibProtocol::ibSchema::ibStyle::ibTheme theme = ibProtocol::ibSchema::ibStyle::ibTheme::System;
	if (!given || !ThemeOf(*given, theme)) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibT("which theme: Theme - 0 the system's, 1 light, 2 dark");
		return nullptr;
	}
	ibSettingsStorage* const storage = ibApplicationInstance::GetSettingsStorage(session.GetApplicationInstance());
	if (storage == nullptr) {
		refusal = ibProtocol::ibRefusal::Exception;
		error = ibT("the base keeps no settings: the theme cannot be kept");
		return nullptr;
	}
	// Kept here, on the session, before the answer: a storage that would not keep it is a refusal the person is shown,
	// not a frame answered as if it had. Nothing is left for the work to do but to answer the frame.
	ibDataNode row;
	row.SetValue(ibProtocol::ibSchema::ibStyle::Theme, static_cast<std::int32_t>(theme));
	if (!storage->Save(ibUserSettingsKey(ibSettingsCategory::Style, StyleObject(), wxNullGuid, &session.GetUserInfo()), row)) {
		refusal = ibProtocol::ibRefusal::Exception;
		error = _("The theme could not be kept");
		return nullptr;
	}
	return []() {};
}
