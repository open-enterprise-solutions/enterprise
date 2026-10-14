#ifndef WATCH_DROP_TARGET_H
#define WATCH_DROP_TARGET_H

#include <wx/wx.h>
#include <wx/dnd.h>

//
// Forward declarations.
//

class ibWatchWindow;

/**
 * This class handles dragging and dropping items onto the watch window.
 */
class ibWatchDropTarget : public wxTextDropTarget
{

public:

    /**
     * Constructor. The watch window that will be added to as a result of drag
     * and drop is specified.
     */
    explicit ibWatchDropTarget(ibWatchWindow* watch);

    /**
     * From wxTextDropTarget.
     */
    virtual bool OnDropText(wxCoord x, wxCoord y, const wxString& data);

private:

	ibWatchWindow* m_watch;

};

#endif