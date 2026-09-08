////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual client host
////////////////////////////////////////////////////////////////////////////

#include "visualHostClient.h"

#ifdef OES_USE_WEB
#include <iostream>
#include "frontend/visualView/ctrl/tableBox.h"   // a picker is a list in choice mode
#include "frontend/web/webChildFrame.h"
#include "frontend/web/webSizer.h"

// Web-side dtor: tear down the control tree through ClearVisualHost
// before ~ibWebWindow takes over. ClearVisualHost fires each control's
// Cleanup hook (script-side OnClose / resource release) in post-order
// and wipes m_baseObjects, so by the time the sizer / children get
// cascade-destroyed there are no stale back-pointers anywhere. Without
// this the default dtor would run ~ibWebWindow directly — fine for a
// plain window, but the host owns runtime state (form refcount, event
// bindings, per-control cleanup hooks) that the desktop mirror releases
// via ClearControl(m_valueForm, true). Matches that semantics.
ibVisualHostClient::~ibVisualHostClient()
{
	std::cerr << "[life] ~ibVisualHostClient " << this
		<< " form=" << (void*)GetValueForm() << std::endl;
	ClearVisualHost();
}

// Push the form's caption to the owning tab (ibWebDocChildFrame) so
// /session reports the new title. The host is parented under the tab
// via SetParent; cast-and-SetTitle is the web analogue of desktop's
// ibDocument::SetTitle chain.
void ibVisualHostClient::SetCaption(const wxString& strCaption)
{
	if (auto* tab = dynamic_cast<ibWebDocChildFrame*>(GetParent()))
		tab->SetTitle(strCaption);
}

// A PICKER is a form whose list is in CHOICE MODE, and that is the whole test.
// Choice mode reaches a form from one place only — the source a select form is
// built on (ibDynamicListView_Choice in GetSelectForm / GetFolderSelectForm),
// carried onto the main table at form build — so a list in choice mode was
// opened to hand a value back and nothing else opens one.
//
// NOT "has an owner control", which was the first test here and was wrong twice
// over. An object form opened from a list carries an owner too, so it can tell
// the list what was created — and a new document came back marked as a dialog.
// And GetOwnerControl() casts to ibValueFrame, which the quick filter's owner is
// not, so that test would have quietly missed the picker it opens.
bool ibVisualHostClient::IsPickerHost() const
{
	ibValueForm* const form = GetValueForm();
	if (form == nullptr)
		return false;
	const auto* table = dynamic_cast<const ibValueModelTableBox*>(form->GetCommandProvider());
	return table != nullptr && table->IsChoiceMode();
}

nlohmann::json ibVisualHostClient::ToJSON() const
{
	auto node = ibWebWindow::ToJSON();

	if (IsPickerHost())
		node["modal"] = true;

	// The form's own resolved title — its Title property, or the synonym of what
	// it shows. Not the tab's copy: that one is taken when the tab is made and
	// never moves again on this front, and the host is not parented to its tab
	// yet the first time this runs.
	if (const ibValueForm* const form = GetValueForm())
		node["caption"] = form->GetControlTitle();

	return node;
}

#else  // !OES_USE_WEB
// Desktop-only implementation: the facade panel that carries the form's chrome, with the
// scrolling window holding its controls inside; tab, wxDocView Doc/View machinery.

ibVisualHostClient::ibVisualHostClient(ibFormVisualDocument* document, ibValueForm* valueForm, ibFrontendWindow* parent) :
	// On desktop ibFrontendWindow == wxWindow, so this just forwards.
	ibVisualHost(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize),
	m_dataViewSizeChanged(false),
	m_dataViewSize(wxDefaultSize),
	m_valueForm(valueForm),
	m_document(document)
{
	ibVisualHostClient::Bind(wxEVT_SIZE, &ibVisualHostClient::OnSize, this);
	ibVisualHostClient::Bind(wxEVT_IDLE, &ibVisualHostClient::OnIdle, this);
}

ibVisualHostClient::~ibVisualHostClient()
{
	ibVisualHostClient::Unbind(wxEVT_SIZE, &ibVisualHostClient::OnSize, this);
	ibVisualHostClient::Unbind(wxEVT_IDLE, &ibVisualHostClient::OnIdle, this);

	ClearControl(m_valueForm, true);
}

/////////////////////////////////////////////////////////////////////////////////

void ibVisualHostClient::OnSize(wxSizeEvent& event)
{
	m_dataViewSizeChanged = (m_dataViewSize != GetSize()) && (m_dataViewSize != wxDefaultSize);
	event.Skip();
}

void ibVisualHostClient::OnIdle(wxIdleEvent& event)
{
	if (m_dataViewSizeChanged)
		m_valueForm->RefreshForm();

	m_dataViewSize = GetSize();
	m_dataViewSizeChanged = false;

	event.Skip();
}

/////////////////////////////////////////////////////////////////////////////////

void ibVisualHostClient::OnClickFromApp(wxWindow* currentWindow, wxMouseEvent& event)
{
	if (event.GetEventType() == wxEVT_LEFT_DOWN) {
		wxWindow* wnd = currentWindow;
		while (wnd != nullptr) {
			ibValueFrame* founded = GetObjectBase(wnd);
			if (founded != nullptr) {
				OnSelected(founded, wnd);
				break;
			}
			wnd = wnd->GetParent();
		}
	}
}

#include "frontend/visualView/ctrl/tableBox.h"   // a picker is a list in choice mode
#include "backend/metaCollection/partial/commonObject.h"

void ibVisualHostClient::SetCaption(const wxString& strCaption)
{
	const ibValueForm* handler = m_document->GetValueForm();

	if (m_document->IsVisualDemonstrationDoc()) {
		if (strCaption.IsEmpty()) {
			const ibSourceDataObject* srcObject = handler->GetSourceObject();
			if (srcObject != nullptr) {
				const ibValueMetaObjectFormBase* creator = handler->GetFormMetaObject();
				const ibValueMetaObjectGenericData* genericObject = srcObject->GetSourceMetaObject();
				if (genericObject != nullptr) {
					m_document->SetTitle(genericObject->GetSynonym() + wxT(": ") + creator->GetSynonym());
					m_document->SetFilename(genericObject->GetFileName(), true);
				}
				else if (creator != nullptr) {
					m_document->SetTitle(creator->GetSynonym());
					m_document->SetFilename(creator->GetFileName(), true);
				}
			}
			else {
				const ibValueMetaObjectFormBase* creator = handler->GetFormMetaObject();
				if (creator != nullptr) {
					m_document->SetTitle(creator->GetSynonym());
					m_document->SetFilename(creator->GetFileName(), true);
				}
			}
		}
		else {
			m_document->SetTitle(strCaption);
			m_document->SetFilename(wxEmptyString, true);
		}
	}
	else if (strCaption.IsEmpty()) {
		const ibSourceDataObject* srcObject = handler->GetSourceObject();
		if (srcObject != nullptr && !m_document->IsVisualDemonstrationDoc()) {
			m_document->SetTitle(srcObject->GetSourceCaption());
			const ibValueMetaObjectGenericData* genericObject = srcObject->GetSourceMetaObject();
			if (genericObject != nullptr) {
				m_document->SetFilename(genericObject->GetFileName(), true);
			}
			else {
				m_document->SetFilename(srcObject->GetSourceCaption(), true);
			}
		}
		else {
			const ibValueMetaObjectFormBase* creator = handler->GetFormMetaObject();
			if (creator != nullptr && !m_document->IsVisualDemonstrationDoc()) {
				m_document->SetTitle(creator->GetSynonym());
				m_document->SetFilename(creator->GetFileName(), true);
			}
		}
	}
	else if (m_document != nullptr && !m_document->IsVisualDemonstrationDoc()) {
		m_document->SetTitle(strCaption);
		m_document->SetFilename(wxEmptyString, true);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////

#endif // !OES_USE_WEB
