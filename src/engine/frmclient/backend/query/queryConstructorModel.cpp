#include "queryConstructorModel.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/dlgs/settings/settingsFieldTree.h"   // ibReadSettingsFields — a field as the server writes it
#include "protocol/protocol.h"

std::vector<ibQueryConstructorField> ibQueryFieldsOfText(const wxString& WXUNUSED(text),
	const ibMetaData* WXUNUSED(metaData), wxString* error)
{
	if (error != nullptr)
		error->clear();

	std::vector<ibQueryConstructorField> fields;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return fields;

	// The question's fields, as a settings window's field tree reads them: the name, how the source calls it, the type,
	// whether the options leave it. The constructor's tree shows the name.
	for (ibSettingsPlainField& described : ibReadSettingsFields(frame->GetRequest().FindChild(ibProtocolName::Fields))) {
		ibQueryConstructorField field;
		field.m_name = described.m_name;
		field.m_presentation = described.m_name;
		field.m_caption = described.m_presentation;
		field.m_type = std::move(described.m_type);
		field.m_available = described.m_available;
		fields.push_back(std::move(field));
	}
	return fields;
}
