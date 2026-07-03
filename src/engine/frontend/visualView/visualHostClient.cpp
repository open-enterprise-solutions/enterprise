////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual client host
////////////////////////////////////////////////////////////////////////////

#include "visualHostClient.h"

#ifdef OES_USE_WEB
#include <iostream>
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

// Mirror desktop: mutate the root BoxSizer's orientation so child
// layout rebuilds on the next response reflect the new axis. The
// host owns the root sizer via SetSizer; only wxBoxSizer-style
// sizers carry an orientation (grid sizers drop the call silently).
void ibVisualHostClient::SetOrientation(int orient)
{
	if (auto* box = dynamic_cast<ibWebBoxSizer*>(GetSizer()))
		box->SetOrientation(orient);
}

#else  // !OES_USE_WEB
// Desktop-only implementation: wxScrolledCanvas-hosted form, tab,
// wxDocView Doc/View machinery.

ibVisualHostClient::ibVisualHostClient(ibFormVisualDocument* document, ibValueForm* valueForm, ibFrontendWindow* parent) :
	// On desktop ibFrontendWindow == wxWindow, so this just forwards.
	ibVisualHost(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize),
	m_document(document),
	m_valueForm(valueForm),
	m_dataViewSize(wxDefaultSize),
	m_dataViewSizeChanged(false)
{
	// Default MAIN sizer (m_mainSizer) up front — CreateVisualHost hangs the
	// chrome layers + content sub-sizer onto it (it lives its own parallel life).
	InitMainSizer();
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

void ibVisualHostClient::SetOrientation(int orient)
{
	ApplyContentOrientation(orient);
}

/////////////////////////////////////////////////////////////////////////////////////////////

#endif // !OES_USE_WEB
