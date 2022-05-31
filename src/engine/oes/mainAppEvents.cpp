////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main events
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include "core/frontend/mainFrame.h"
#include "core/frontend/mainFrameChild.h"

void CMainApp::OnKeyEvent(wxKeyEvent &event)
{
	if (event.GetKeyCode() == WXK_ESCAPE) {
		if (!appData->DesignerMode()) {
			wxAuiNotebook *noteBook = mainFrame->GetNotebook();
			wxASSERT(noteBook);
			if (noteBook->GetPageCount() > 0) {
				CDocChildFrame *childFrame = dynamic_cast<CDocChildFrame *>(
					noteBook->GetPage(noteBook->GetSelection())
				);
				if (childFrame) {
					childFrame->Close();
				}
			}
		}
	}

	event.Skip();
}

#include "core/frontend/visualView/visualEditorBase.h"

void CMainApp::OnMouseEvent(wxMouseEvent &event)
{
	if (event.LeftIsDown() || event.RightIsDown())
	{
		wxWindow *currentWindow = dynamic_cast<wxWindow *>(event.GetEventObject()); wxWindow *windowFocus = currentWindow;
		while (windowFocus && !windowFocus->IsKindOf(CLASSINFO(IVisualHost))) { 
			windowFocus = windowFocus->GetParent(); 
		}
		if (windowFocus) {
			IVisualHost *visualEditor = dynamic_cast<IVisualHost *>(windowFocus);
			if (visualEditor) {
				if (event.GetEventType() == wxEVT_LEFT_DOWN
					|| event.GetEventType() == wxEVT_RIGHT_DOWN) {
					visualEditor->OnClickFromApp(currentWindow, event);
				}
			}
		}
	}

	event.Skip();
}

void CMainApp::OnSetFocus(wxFocusEvent &event)
{
	wxWindow *windowFocus = dynamic_cast<wxWindow *>(event.GetEventObject());

	while (windowFocus && !windowFocus->IsKindOf(CLASSINFO(CDocChildFrame))) {
		windowFocus = windowFocus->GetParent();
	}

	if (windowFocus) {
		wxAuiNotebook *noteBook = mainFrame->GetNotebook();
		CDocChildFrame *pageWindow = dynamic_cast<CDocChildFrame *>(windowFocus); size_t newSelPos;
		for (newSelPos = 0; newSelPos < noteBook->GetPageCount(); newSelPos++) {
			if (noteBook->GetPage(newSelPos) == pageWindow) {
				break;
			}
		}

		if (newSelPos != noteBook->GetSelection()) {
			pageWindow->Activate();
		}
	}

	event.Skip();
}

int CMainApp::FilterEvent(wxEvent& event)
{
	if (event.GetEventType() == wxEVT_KEY_DOWN) {
		OnKeyEvent(
			static_cast<wxKeyEvent&>(event)
		);
	}
	else if (event.GetEventType() == wxEVT_LEFT_DOWN ||
		event.GetEventType() == wxEVT_RIGHT_DOWN) {
		OnMouseEvent(
			static_cast<wxMouseEvent&>(event)
		);
	}
	else if (event.GetEventType() == wxEVT_SET_FOCUS)
	{
		OnSetFocus(
			static_cast<wxFocusEvent&> (event)
		);
	}

	return Event_Skip;
}