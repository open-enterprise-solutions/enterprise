////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, unknownworlds team
//	Description : watch window
////////////////////////////////////////////////////////////////////////////

#include "watchdroptarget.h"    
#include "watchwindow.h"

CWatchDropTarget::CWatchDropTarget(CWatchWindow* watch)
{
    m_watch = watch;
}

bool CWatchDropTarget::OnDropText(wxCoord x, wxCoord y, const wxString& data)
{
    m_watch->AddWatch(data);    
    // Return false since we want to effectively copy the text, not move it.
    return false;

}