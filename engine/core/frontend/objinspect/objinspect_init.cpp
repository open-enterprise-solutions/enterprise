#include "objinspect.h"
#include "frontend/mainFrame.h"

///////////////////////////////////////////////////////////////////////////////
// CObjectInspector
///////////////////////////////////////////////////////////////////////////////

CObjectInspector* CObjectInspector::s_instance = NULL;

CObjectInspector* CObjectInspector::Get()
{
	wxASSERT(CMainFrame::Get());
	if (!s_instance) {
		s_instance = new CObjectInspector(CMainFrame::Get(), wxID_ANY);
	}
	return s_instance;
}

void CObjectInspector::Destroy()
{
	wxDELETE(s_instance);
}