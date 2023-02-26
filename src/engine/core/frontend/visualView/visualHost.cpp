////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual host  
////////////////////////////////////////////////////////////////////////////

#include "visualHost.h"
#include <wx/collpane.h>

void CVisualHost::CreateFrame()
{
#if !defined(__WXGTK__ )
	wxScrolledWindow::Freeze();   // Prevent flickering on wx 2.8,
				// Causes problems on wx 2.9 in wxGTK (e.g. wxNoteBook objects)
#endif

	wxScrolledWindow::Enable(true);

	if (m_valueForm != NULL &&
		m_valueForm->IsShown())
	{
		// --- [1] Set the color of the form -------------------------------
		SetForegroundColour(m_valueForm->GetForegroundColour());
		SetBackgroundColour(m_valueForm->GetBackgroundColour());

		// --- [2] Title bar Setup
		if (m_document && !IsDemonstration()) {
			m_document->SetFilename(m_valueForm->GetCaption(), true);
			m_document->SetTitle(m_valueForm->GetCaption());
		}

		// --- [3] Default sizer Setup 
		m_mainBoxSizer = new wxBoxSizer(m_valueForm->GetOrient());
		wxScrolledWindow::SetSizer(m_mainBoxSizer);

		// --- [4] Create the components of the form -------------------------
		// Used to save valueForm objects for later display

		for (unsigned int i = 0; i < m_valueForm->GetChildCount(); i++) {
			IValueFrame* child = m_valueForm->GetChild(i);
			// Recursively generate the ObjectTree
			try {
				// we have to put the content valueForm panel as parentObject in order
				// to SetSizeHints be called.
				GenerateControl(child, this, GetFrameSizer());
			}
			catch (std::exception& ex) {
				wxLogError(ex.what());
			}
		}

		wxScrolledWindow::Layout();
		wxScrolledWindow::Refresh();
	}
	else {
		// There is no form to display
		wxScrolledWindow::Show(false);
		wxScrolledWindow::Refresh();
	}

#if !defined(__WXGTK__)
	wxScrolledWindow::Thaw();
#endif

	UpdateVirtualSize();
}

void CVisualHost::UpdateFrame()
{
#if !defined(__WXGTK__ )
	wxScrolledWindow::Freeze();   // Prevent flickering on wx 2.8,
				// Causes problems on wx 2.9 in wxGTK (e.g. wxNoteBook objects)
#endif

	wxScrolledWindow::Enable(true);

	if (m_valueForm != NULL &&
		m_valueForm->IsShown()) {

		wxSize defSize = GetSize();

		// --- [1] Set the color of the form -------------------------------
		SetForegroundColour(m_valueForm->GetForegroundColour());
		SetBackgroundColour(m_valueForm->GetBackgroundColour());

		// --- [2] Title bar Setup
		if (m_document != NULL && !IsDemonstration()) {
			m_document->SetFilename(m_valueForm->GetCaption(), true);
			m_document->SetTitle(m_valueForm->GetCaption());
		}

		// --- [3] Default sizer Setup 
		m_mainBoxSizer->SetOrientation(m_valueForm->GetOrient());

		// --- [4] Create the components of the form -------------------------
		// Used to save valueForm objects for later display
		for (unsigned int i = 0; i < m_valueForm->GetChildCount(); i++) {
			IValueFrame* child = m_valueForm->GetChild(i);
			// Recursively generate the ObjectTree
			try {
				// we have to put the content valueForm panel as parentObject in order
				// to SetSizeHints be called.
				RefreshControl(child, this, GetFrameSizer());
			}
			catch (std::exception& ex) {
				wxLogError(ex.what());
			}
		}

		wxScrolledWindow::Layout();
		wxScrolledWindow::Refresh();
	}
	else {
		// There is no form to display
		wxScrolledWindow::Show(false);
		wxScrolledWindow::Refresh();
	}

#if !defined(__WXGTK__)
	wxScrolledWindow::Thaw();
#endif

	UpdateVirtualSize();
}

#include "core/frontend/docView/docManager.h"

CVisualHost::~CVisualHost()
{
	for (unsigned int i = 0; i < m_valueForm->GetChildCount(); i++) {
		IValueFrame* objChild = m_valueForm->GetChild(i);
		DeleteRecursive(objChild, true);
	}
}