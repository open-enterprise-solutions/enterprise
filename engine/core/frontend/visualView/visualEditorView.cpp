////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual view  
////////////////////////////////////////////////////////////////////////////

#include "visualEditorView.h"
#include <wx/collpane.h>

void CVisualView::CreateFrame()
{
#if !defined(__WXGTK__ )
	Freeze();   // Prevent flickering on wx 2.8,
				// Causes problems on wx 2.9 in wxGTK (e.g. wxNoteBook objects)
#endif

	Enable(m_valueForm ? m_valueForm->m_enabled : false);

	if (m_valueForm && m_valueForm->IsShown())
	{
		// --- [1] Set the color of the form -------------------------------
		if (m_valueForm->m_bg.IsOk()) {
			SetBackgroundColour(m_valueForm->m_bg);
		}
		else {
			SetOwnBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_APPWORKSPACE));
		}

		// --- [2] Title bar Setup
		if (m_document && !IsDemonstration()) {
			m_document->SetFilename(m_valueForm->m_caption, true);
			m_document->SetTitle(m_valueForm->m_caption);
		}

		// --- [3] Default sizer Setup 
		m_mainBoxSizer = new wxBoxSizer(m_valueForm->m_orient);
		SetSizer(m_mainBoxSizer);

		// --- [4] Create the components of the form -------------------------
		// Used to save valueForm objects for later display
		IValueFrame *menubar = NULL;
		wxWindow* statusbar = NULL;
		wxWindow* toolbar = NULL;

		for (unsigned int i = 0; i < m_valueForm->GetChildCount(); i++) {
			IValueFrame *child = m_valueForm->GetChild(i);

			if (!menubar && (m_valueForm->GetObjectTypeName() == wxT("menubar_form"))) {
				// main form acts as a menubar
				menubar = m_valueForm;
			}
			else if (child->GetObjectTypeName() == wxT("menubar")) {
				// Create the menubar later
				menubar = child;
			}
			else {
				// Recursively generate the ObjectTree
				try
				{
					// we have to put the content valueForm panel as parentObject in order
					// to SetSizeHints be called.
					GenerateControl(child, this, GetFrameSizer());
				}
				catch (std::exception& ex)
				{
					wxLogError(ex.what());
				}
			}
		}

		Layout();

		//m_mainBoxSizer->Fit(this);
		//SetClientSize(GetBestSize());

		Refresh();
	}
	else {
		// There is no form to display
		Show(false);
		Refresh();
	}

#if !defined(__WXGTK__)
	Thaw();
#endif

	UpdateVirtualSize();
}

void CVisualView::UpdateFrame()
{
#if !defined(__WXGTK__ )
	Freeze();   // Prevent flickering on wx 2.8,
				// Causes problems on wx 2.9 in wxGTK (e.g. wxNoteBook objects)
#endif

	Enable(m_valueForm->m_enabled);

	if (m_valueForm &&
		m_valueForm->IsShown()) {

		wxSize defSize = GetSize(); 

		// --- [1] Set the color of the form -------------------------------
		if (m_valueForm->m_bg.IsOk()) {
			SetBackgroundColour(m_valueForm->m_bg);
		}
		else {
			SetOwnBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_APPWORKSPACE));
		}

		// --- [2] Title bar Setup
		if (m_document && !IsDemonstration()) {
			m_document->SetFilename(m_valueForm->m_caption, true);
			m_document->SetTitle(m_valueForm->m_caption);
		}

		// --- [3] Default sizer Setup 
		m_mainBoxSizer->SetOrientation(m_valueForm->m_orient);

		// --- [4] Create the components of the form -------------------------
		// Used to save valueForm objects for later display
		IValueFrame *menubar = NULL;
		wxWindow* statusbar = NULL;
		wxWindow* toolbar = NULL;

		for (unsigned int i = 0; i < m_valueForm->GetChildCount(); i++) {
			
			IValueFrame *child = m_valueForm->GetChild(i);

			if (!menubar && (m_valueForm->GetObjectTypeName() == wxT("menubar_form"))) {
				// main form acts as a menubar
				menubar = m_valueForm;
			}
			else if (child->GetObjectTypeName() == wxT("menubar")) {
				// Create the menubar later
				menubar = child;
			}
			else
			{
				// Recursively generate the ObjectTree
				try
				{
					// we have to put the content valueForm panel as parentObject in order
					// to SetSizeHints be called.
					RefreshControl(child, this, GetFrameSizer());
				}
				catch (std::exception& ex)
				{
					wxLogError(ex.what());
				}
			}
		}

		Layout();

		//m_mainBoxSizer->Fit(this);
		//SetClientSize(GetBestSize());

		Refresh();
	}
	else {
		// There is no form to display
		Show(false);
		Refresh();
	}

#if !defined(__WXGTK__)
	Thaw();
#endif

	UpdateVirtualSize();
}

#include "common/reportManager.h"

CVisualView::~CVisualView()
{
	for (unsigned int i = 0; i < m_valueForm->GetChildCount(); i++) {
		IValueFrame *objChild = m_valueForm->GetChild(i);
		DeleteRecursive(objChild, true);
	}
}