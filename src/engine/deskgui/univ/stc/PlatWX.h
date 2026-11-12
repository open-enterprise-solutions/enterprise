// FORKED from wxWidgets' wxStyledTextCtrl (src/stc of wx 3.3; wx -> ib prefixes applied mechanically to the
// names it defines) - see stc.h.in. Fitted to Scintilla 5.6 (src/3rdparty/scintilla): its platform interface is
// Platform.h of that release - fonts and surfaces are classes to derive, colours are ColourRGBA.

#ifndef _DESKGUI_UNIV_STC_PLATWX_H_
#define _DESKGUI_UNIV_STC_PLATWX_H_

#include "wx/defs.h"

#if wxUSE_STC

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ScintillaTypes.h"
#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

class ibStyledTextCtrl;
class ibSTCListBox;
class ibSTCListBoxVisualData;

using namespace Scintilla;
using namespace Scintilla::Internal;


wxRect wxRectFromPRectangle(PRectangle prc);
PRectangle PRectangleFromwxRect(wxRect rc);
wxColour wxColourFromColourRGBA(ColourRGBA colour);

class ListBoxImpl : public ListBox {
private:
    ibSTCListBox*           m_listBox;
    ibSTCListBoxVisualData* m_visualData;

public:
    ListBoxImpl();
    ~ListBoxImpl() noexcept override;

    virtual void SetFont(const Font *font) override;
    virtual void Create(Window &parent, int ctrlID, Point location_, int lineHeight_, bool unicodeMode_, Technology technology_) override;
    virtual void SetAverageCharWidth(int width) override;
    virtual void SetVisibleRows(int rows) override;
    virtual int GetVisibleRows() const override;
    virtual PRectangle GetDesiredRect() override;
    virtual int CaretFromEdge() override;
    virtual void Clear() noexcept override;
    virtual void Append(char *s, int type = -1) override;
    virtual int Length() override;
    virtual void Select(int n) override;
    virtual int GetSelection() override;
    virtual int Find(const char *prefix) override;
    virtual std::string GetValue(int n) override;
    virtual void RegisterImage(int type, const char *xpm_data) override;
    virtual void RegisterRGBAImage(int type, int width, int height, const unsigned char *pixelsImage) override;
    virtual void ClearRegisteredImages() override;
    virtual void SetDelegate(IListBoxDelegate *lbDelegate) override;
    virtual void SetList(const char* list, char separator, char typesep) override;
    virtual void SetOptions(ListOptions options) override;

    void RegisterImageHelper(int type, const wxBitmap& bmp);
};


//----------------------------------------------------------------------
// ibSTCPopupWindow

#if defined(__WXOSX_COCOA__) || defined(__WXMSW__) || defined(__WXGTK__)
    #define ibSTC_POPUP_IS_CUSTOM 1
#else
    #define ibSTC_POPUP_IS_CUSTOM 0
#endif

// Define the base class used for ibSTCPopupWindow.
#ifdef __WXOSX_COCOA__

    #include "wx/nonownedwnd.h"
    #define ibSTC_POPUP_IS_FRAME 0

    class ibSTCPopupBase:public wxNonOwnedWindow
    {
    public:
        ibSTCPopupBase(wxWindow*);
        virtual ~ibSTCPopupBase();
        virtual bool Show(bool show=true) override;

    protected:
        virtual void DoSetSize(int, int, int, int, int) override;
        void SetSTCCursor(int);
        void RestoreSTCCursor();
        void OnMouseEnter(wxMouseEvent&);
        void OnMouseLeave(wxMouseEvent&);
        void OnParentDestroy(wxWindowDestroyEvent& event);

    private:
        WX_NSWindow       m_nativeWin;
        ibStyledTextCtrl* m_stc;
        bool              m_cursorSetByPopup;
        int               m_prevCursor;
    };

#elif wxUSE_POPUPWIN

    #include "deskgui/univ/popupWindow.h"
    #define ibSTC_POPUP_IS_FRAME 0

    // SEAM vs wx: the theme's popup (ibPopupWindow) - wx's own show and activation copies taken away
    class ibSTCPopupBase:public ibPopupWindow<wxPopupWindow>
    {
    public:
        ibSTCPopupBase(wxWindow*);
        #ifdef __WXGTK__
            virtual ~ibSTCPopupBase();
        #endif
    };

#else

    #include "wx/frame.h"
    #define ibSTC_POPUP_IS_FRAME 1

    class ibSTCPopupBase:public wxFrame
    {
    public:
        ibSTCPopupBase(wxWindow*);
        #ifdef __WXMSW__
            virtual bool Show(bool show=true) override;
            virtual bool MSWHandleMessage(WXLRESULT *result, WXUINT message,
                                          WXWPARAM wParam, WXLPARAM lParam)
                                          override;
        #elif !ibSTC_POPUP_IS_CUSTOM
            virtual bool Show(bool show=true) override;
            void ActivateParent();
        #endif
    };

#endif // __WXOSX_COCOA__

class ibSTCPopupWindow:public ibSTCPopupBase
{
public:
    ibSTCPopupWindow(wxWindow*);
    virtual ~ibSTCPopupWindow();
    virtual bool Destroy() override;
    virtual bool AcceptsFocus() const override;

protected:
    virtual void DoSetSize(int x, int y, int width, int height,
                           int sizeFlags = wxSIZE_AUTO) override;
    void OnParentMove(wxMoveEvent& event);
    #if defined(__WXOSX_COCOA__) || (defined(__WXGTK__)&&!ibSTC_POPUP_IS_FRAME)
        void OnIconize(wxIconizeEvent& event);
    #elif !ibSTC_POPUP_IS_CUSTOM
        void OnFocus(wxFocusEvent& event);
    #endif

private:
    wxPoint   m_relPos;
    wxPoint   m_absPos;
    wxWindow* m_tlw;
};

#endif // wxUSE_STC

#endif // _DESKGUI_UNIV_STC_PLATWX_H_
