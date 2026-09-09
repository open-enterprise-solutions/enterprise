#include "webCommandBar.h"

#include "webWindow.h"

#include "backend/metaData.h"
#include "backend/backend_picture.h"
#include "frontend/visualView/layers/commandBar.h"

ibWebToolbar* ibWebBuildCommandBar(ibValueCommandBar* commandBar,
	const ibMetaData* metaData, int ownerControlId)
{
	if (commandBar == nullptr)
		return nullptr;

	const std::vector<ibCommandEntry>& commands = commandBar->BuildCommands();
	if (commands.empty())
		return nullptr;

	auto* bar = new ibWebToolbar();
	bool anyTool = false;

	for (const ibCommandEntry& command : commands) {
		// A separator between nothing and nothing is a line at the start of the
		// bar, so one is emitted only after a tool has been.
		if (command.id == wxNOT_FOUND) {
			if (anyTool) {
				auto* separator = new ibWebToolBarSeparator();
				separator->SetParent(bar);
			}
			continue;
		}

		// The command's own live bitmap wins; a picture description is
		// the fallback. Same order as the desktop fill.
		wxBitmap bitmap = command.bitmap.IsOk() ? command.bitmap
			: (command.picture.IsEmptyPicture() ? wxNullBitmap
				: ibBackendPicture::CreatePicture(command.picture, metaData));
		wxString caption = command.caption;
		if (command.representation == ibRepresentation_Picture) caption = wxEmptyString;
		else if (command.representation == ibRepresentation_Text) bitmap = wxNullBitmap;

		auto* tool = new ibWebCommandTool(command.id, ownerControlId);
		tool->SetLabel(caption);
		tool->SetToolTip(command.caption);
		tool->SetRepresentation(static_cast<int>(command.representation));
		tool->SetHasPicture(bitmap.IsOk());
		if (bitmap.IsOk()) {
			const wxString b64 = ibBackendPicture::CreateBase64Image(bitmap.ConvertToImage());
			if (!b64.IsEmpty())
				tool->SetPictureDataUri(wxT("data:image/png;base64,") + b64);
		}
		tool->Enable(command.enabled);
		tool->SetParent(bar);
		anyTool = true;
	}

	if (!anyTool) {
		delete bar;
		return nullptr;
	}

	return bar;
}
