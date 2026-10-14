////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report window
////////////////////////////////////////////////////////////////////////////

#include "treeDataReport.h"

#include "frmclient/win/picture.h"

void ibDataReportTree::Draw(const ibProtocolNode& root)
{
	ibMetaTreeBase::Draw(root);

	// THE HEADER — the root's properties' rows, by their names: a text shows how its value reads, and one the person is
	// typing into is left as typed.
	for (const ibProtocolNode& property : root.FindChild(ibProtocolName::Properties).Children()) {
		const wxString name = property.GetString(ibProtocolName::Name);
		wxTextCtrl* const text = name == wxT("Name") ? m_nameValue
			: name == wxT("Synonym") ? m_synonymValue
			: name == wxT("Comment") ? m_commentValue
			: nullptr;
		if (text != nullptr) {
			// …the synonym as its object reads it, the root row's: made from the name while none is written (the desktop's).
			const wxString presentation = text == m_synonymValue ? root.GetString(ibProtocolName::Synonym)
				: property.GetString(ibProtocolName::Presentation);
			if (!text->IsModified() && text->GetValue() != presentation)
				text->ChangeValue(presentation);
			text->SetEditable(!property.GetBool(ibProtocolName::ReadOnly));
		}
		else if (name == wxT("DefaultFormObject")) {
			UpdateChoiceSelection(m_defaultFormValue, property);
		}
		else if (name == wxT("DefaultComposer")) {
			UpdateChoiceSelection(m_defaultComposerValue, property);
		}
	}

	// …and the module's button its picture — the object module's, its menu entry's (the desktop's).
	if (!m_buttonModule->GetBitmap().IsOk()) {
		for (const ibProtocolNode& entry : root.FindChild(ibProtocolName::Menu).Children()) {
			if (entry.GetString(ibProtocolName::Name) != wxT("ObjectModule"))
				continue;
			const wxBitmap bitmap = ibProtocolPicture(entry.GetString(ibProtocolName::Picture));
			if (bitmap.IsOk()) {
				m_buttonModule->SetBitmap(bitmap);
				Layout();   // …the button grows by its picture
			}
			break;
		}
	}
}
