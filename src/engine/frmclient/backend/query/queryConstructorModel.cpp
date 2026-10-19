#include "queryConstructorModel.h"

#include "frmclient/backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — the fields' [+]
#include "frmclient/docView/tabDocument.h"                                  // a tab's fetch — the fields of its text
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/dlgs/settings/settingsFieldTree.h"   // ibReadSettingsFields — a field as the server writes it
#include "protocol/protocol.h"

#include <map>

namespace {

// WHAT A TAB'S VIEW ALREADY SAID OF A TEXT — its query's fields, kept by the tab's id: the panel reads them as it is
// built and again as each tree is filled, and the same text asks the server once.
struct ibKnownQueryFields {
	wxString       m_text;
	ibProtocolNode m_answer;
};

std::map<long long, ibKnownQueryFields>& KnownQueryFields()
{
	static std::map<long long, ibKnownQueryFields> s_known;
	return s_known;
}

// The question's fields, as a settings window's field tree reads them: the name, how the source calls it, the type,
// whether the options leave it. The constructor's tree shows the name.
std::vector<ibQueryConstructorField> ReadQueryFields(const ibProtocolNode& described)
{
	std::vector<ibQueryConstructorField> fields;
	for (ibSettingsPlainField& plain : ibReadSettingsFields(described)) {
		ibQueryConstructorField field;
		field.m_name = plain.m_name;
		field.m_presentation = plain.m_name;
		field.m_caption = plain.m_presentation;
		field.m_type = std::move(plain.m_type);
		field.m_available = plain.m_available;
		fields.push_back(std::move(field));
	}
	return fields;
}

} // namespace

void ibQueryFieldsKnown(long long tabId, const wxString& text, const ibProtocolNode& answer)
{
	KnownQueryFields()[tabId] = ibKnownQueryFields{ text, answer };
}

void ibQueryFieldsForget(long long tabId)
{
	KnownQueryFields().erase(tabId);
}

std::vector<ibQueryConstructorField> ibQueryFieldsOfText(const wxString& text, const ibMetaData* metaData, wxString* error)
{
	if (error != nullptr)
		error->clear();

	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return {};

	// ⭐ A TAB'S CONFIGURATION ASKS ITS TAB — a composer's panel: the text is the one being edited there, so its fields
	// are the tab's answer for it (fetch {Tab, Request: {Kind: Fields, Text}}); the text the tab's view was drawn with
	// was said with the view, and asks nothing.
	if (metaData != nullptr && metaData->GetTabId() != 0) {
		const long long tabId = metaData->GetTabId();
		ibProtocolNode answer;
		const auto known = KnownQueryFields().find(tabId);
		if (known != KnownQueryFields().end() && known->second.m_text == text) {
			answer = known->second.m_answer;
		}
		else {
			const ibTabDocument* const document = frame->FindDocument(tabId);
			if (document == nullptr)
				return {};
			ibProtocolNode request;
			request.SetValue(ibProtocolName::Kind, static_cast<long long>(ibProtocolComposerRequest::Fields))
				.SetValue(ibProtocolName::Text, text);
			if (!document->Fetch(request, answer))
				return {};
			ibQueryFieldsKnown(tabId, text, answer);
		}
		ibValueReferenceDataObject::ReadReferences(answer.FindChild(ibProtocolName::References));
		if (error != nullptr)
			*error = answer.GetString(ibProtocolName::Error);
		return ReadQueryFields(answer.FindChild(ibProtocolName::Fields));
	}

	// …A WINDOW THE SERVER ASKED reads the question's: the fields it described with it.
	return ReadQueryFields(frame->GetRequest().FindChild(ibProtocolName::Fields));
}
