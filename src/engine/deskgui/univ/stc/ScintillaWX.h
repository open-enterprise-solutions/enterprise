// FORKED from wxWidgets' wxStyledTextCtrl (src/stc of wx 3.3; wx -> ib prefixes applied mechanically to the
// names it defines) - see stc.h.in. Fitted to Scintilla 5.6 (src/3rdparty/scintilla): its internals live in
// Scintilla::Internal, its messages and flags are enum classes - the SEAM comments mark where that changed the code.

////////////////////////////////////////////////////////////////////////////
// Name:        src/stc/ScintillaWX.h
// Purpose:     A wxWidgets implementation of Scintilla.  A class derived
//              from ScintillaBase that uses the "wx platform" defined in
//              PlatWX.cpp.  This class is one end of a bridge between
//              the wx world and the Scintilla world.  It needs a peer
//              object of type ibStyledTextCtrl to function.
//
// Author:      Robin Dunn
//
// Created:     13-Jan-2000
// Copyright:   (c) 2000 by Total Control Software
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_STC_SCINTILLAWX_H_
#define _DESKGUI_UNIV_STC_SCINTILLAWX_H_
#include "wx/defs.h"

//----------------------------------------------------------------------

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <new>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <algorithm>
#include <memory>

// These are all Scintilla headers - SEAM: in the order Scintilla's own Qt host includes them (5.6)
wxGCC_WARNING_SUPPRESS(double-promotion)
#include "ScintillaTypes.h"
#include "ScintillaMessages.h"
#include "ScintillaStructures.h"
#include "Scintilla.h"
#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "ILoader.h"
#include "ILexer.h"

#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "CallTip.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "AutoComplete.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "ScintillaBase.h"
#include "CaseConvert.h"
wxGCC_WARNING_RESTORE(double-promotion)

#include "wx/dnd.h"
#include "wx/event.h"
#include "wx/private/textinput.h"
#ifdef __WXMSW__
#include "wx/msw/wrapwin.h"                     // HBITMAP
#endif

using namespace Scintilla;
using namespace Scintilla::Internal;

// Define this if there is a standard clipboard format for rectangular
// text selection under the current platform.
#if defined(__WXMSW__) || defined(__WXGTK__)
    #define wxHAVE_STC_RECT_FORMAT
#endif

//----------------------------------------------------------------------


class WXDLLIMPEXP_FWD_CORE wxDC;
class ibStyledTextCtrl;           // forward
class ScintillaWX;
class ibSTCTimer;

//----------------------------------------------------------------------
// Helper classes

#if wxUSE_DRAG_AND_DROP
class ibSTCDropTarget : public wxTextDropTarget {
public:
    void SetScintilla(ScintillaWX* swx) {
        m_swx = swx;
    }

    bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override;
    wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult def) override;
    wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def) override;
    void OnLeave() override;

private:
    ScintillaWX* m_swx;
};
#endif

//----------------------------------------------------------------------

class ScintillaWX : public ScintillaBase
#ifdef wxHAS_TEXT_INPUT_CLIENT
                  , public wxTextInputClient
#endif
{
public:

    ScintillaWX(ibStyledTextCtrl* win);
    ~ScintillaWX();

    // base class virtuals
    virtual void Initialise() override;
    virtual void Finalise() override;
    virtual void StartDrag() override;
    virtual bool SetIdle(bool on) override;
    virtual void SetMouseCapture(bool on) override;
    virtual bool HaveMouseCapture() override;
    virtual void ScrollText(Sci::Line linesToMove) override;
    virtual void SetVerticalScrollPos() override;
    virtual void SetHorizontalScrollPos() override;
    virtual bool ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) override;
    virtual void Copy() override;
    virtual void Paste() override;
    virtual void CopyToClipboard(const SelectionText &selectedText) override;

    virtual void CreateCallTipWindow(PRectangle rc) override;
    virtual void AddToPopUp(const char *label, int cmd = 0, bool enabled = true) override;
    virtual void ClaimSelection() override;

    // SEAM vs wx: Scintilla 5.6 asks the host for both conversions (pure there); this control's documents are UTF-8
    // (wx 3.3 builds Unicode only), so a text in another code page is converted through wx's conversions
    virtual std::string UTF8FromEncoded(std::string_view encoded) const override;
    virtual std::string EncodedFromUTF8(std::string_view utf8) const override;

    virtual sptr_t DefWndProc(Message iMessage,
                              uptr_t wParam,
                              sptr_t lParam) override;
    virtual sptr_t WndProc(Message iMessage,
                           uptr_t wParam,
                           sptr_t lParam) override;

    virtual void NotifyChange() override;
    virtual void NotifyParent(NotificationData scn) override;

    virtual void CancelModes() override;

    virtual void UpdateSystemCaret() override;
    virtual bool FineTickerRunning(TickReason reason) override;
    virtual void FineTickerStart(TickReason reason, int millis, int tolerance) override;
    virtual void FineTickerCancel(TickReason reason) override;

    // Event delegates
    void DoPaint(wxDC* dc, wxRect rect);
    void DoHScroll(int type, int pos);
    void DoVScroll(int type, int pos);
    void DoSize(int width, int height);
    void DoLoseFocus();
    void DoGainFocus();
    void DoInvalidateStyleData();
    void DoLeftButtonDown(Point pt, unsigned int curTime, bool shift, bool ctrl, bool alt, bool meta);
    void DoRightButtonDown(Point pt, unsigned int curTime, bool shift, bool ctrl, bool alt, bool meta);
    void DoLeftButtonUp(Point pt, unsigned int curTime, bool shift, bool ctrl, bool alt, bool meta);
    void DoLeftButtonMove(Point pt, unsigned int curTime, bool shift, bool ctrl, bool alt, bool meta);
    void DoMiddleButtonUp(Point pt);
    void DoMouseWheel(wxMouseWheelAxis axis, int rotation, int delta,
                      int linesPerAction, int columnsPerAction,
                      bool ctrlDown, bool isPageScroll);
    void DoAddChar(wxUniChar key);
    int  DoKeyDown(const wxKeyEvent& event, bool* consumed);
    void DoOnIdle(wxIdleEvent& evt);

#if wxUSE_DRAG_AND_DROP
    bool DoDropText(long x, long y, const wxString& data);
    wxDragResult DoDragEnter(wxCoord x, wxCoord y, wxDragResult def);
    wxDragResult DoDragOver(wxCoord x, wxCoord y, wxDragResult def);
    void DoDragLeave();
#endif

    void DoCommand(int ID);
    bool DoContextMenu(Point pt);
    void DoOnListBox();
    void DoMouseCaptureLost();

    // Native text input composition.
#ifdef wxHAS_TEXT_INPUT_CLIENT
    // Rolling the pre-edit text back relies on tentative undo actions being
    // recorded, so inline composition also requires undo collection.
    bool IsTextInputEnabled() const override
        { return imeInteraction == IMEInteraction::Inline && pdoc->IsCollectingUndo(); }
    bool HasActiveComposition() const override
        { return m_compositionActive; }
    void CancelComposition() override;
#endif

#ifdef __WXGTK__
    bool UpdateComposition(const wxString& text,
                           int cursorCharacters) override;
    bool CommitComposition(const wxString& text) override;
    wxRect GetIMEContextRect() override;
#endif // __WXGTK__

#ifdef __WXOSX_COCOA__
    bool InsertText(const wxString& text,
                    long replacementStart,
                    long replacementLength) override;
    bool SetMarkedText(const wxString& text,
                       long selectedStart,
                       long selectedLength,
                       long replacementStart,
                       long replacementLength) override;
    void UnmarkText() override;
    bool HasMarkedText() const override
        { return HasActiveComposition() && m_compositionLength != 0; }
    bool GetMarkedTextRange(long* start, long* length) const override;
    bool GetSelectedTextRange(long* start, long* length) const override;
    bool GetTextInRange(long start, long length, wxString* text,
                        long* actualStart,
                        long* actualLength) const override;
    bool GetTextRect(long start, long length, wxRect* rect,
                     long* actualStart,
                     long* actualLength) override;
    bool GetTextPosition(const wxPoint& point, long* position) override;
#endif // __WXOSX_COCOA__


    // helpers
    void FullPaint();
    void FullPaintDC(wxDC* dc);
    bool CanPaste() override;
    bool GetHideSelection() { return !vs.selection.visible; }
    void DoScrollToLine(int line);
    void DoScrollToColumn(int column);
    void ClipChildren(wxDC& dc, PRectangle rect);
    void SetPaintAbandoned(){paintState = PaintState::abandoned;}
    void DoMarkerDefineBitmap(int markerNumber, const wxBitmap& bmp);
    void DoRegisterImage(int type, const wxBitmap& bmp);

private:
    bool                capturedMouse;
    bool                focusEvent;

    // True while the dtor runs: notifications are suppressed then, as the
    // control may already be partially destroyed.
    bool                m_destroying = false;

    ibStyledTextCtrl*   stc;

    // SEAM vs wx: TickReason is an enum class since Scintilla 5.1, which wxIntegerHash does not hash
    using TimersHash = std::map<TickReason, ibSTCTimer*>;
    TimersHash          timers;

#if wxUSE_DRAG_AND_DROP
    ibSTCDropTarget*    dropTarget;
    wxDragResult        dragResult;
#endif

    int                 wheelVRotation;
    int                 wheelHRotation;

#ifdef wxHAS_TEXT_INPUT_CLIENT
    bool                m_compositionActive = false;
    Sci::Position       m_compositionStart = 0;
    Sci::Position       m_compositionLength = 0;

    // Whether the first modification of the current composition was already
    // reported as starting an action, and whether its result is being
    // inserted: see NotifyParent().
    bool                m_compositionActionStarted = false;
    bool                m_committingComposition = false;

    bool StartComposition(long replacementStart, long replacementLength);
    void UndoCompositionText();
    void ClearCompositionIndicator();
    Sci::Position InsertCompositionText(const wxString& text,
                                        CharacterSource source);
    void SendCompositionResult(const wxString& text);
    Sci::Position PositionFromUTF16(long position) const;
    long PositionToUTF16(Sci::Position position) const;
    Sci::Position RelativePositionUTF16Clamped(Sci::Position position,
                                               long lengthUTF16,
                                               long* unitsShort = nullptr)
                                               const;
#endif

    // For use in creating a system caret
    bool HasCaretSizeChanged();
    bool CreateSystemCaret();
    bool DestroySystemCaret();

    static sptr_t DirectFunction(ScintillaWX* swx, unsigned int iMessage,
                                 uptr_t wParam, sptr_t lParam);
#ifdef __WXMSW__
    HBITMAP sysCaretBitmap;
    int sysCaretWidth;
    int sysCaretHeight;
#endif

#ifdef wxHAVE_STC_RECT_FORMAT
    // The presence of this format on the clipboard indicates that the text is
    // a rectangular (and not the default linear) selection.
    wxDataFormat m_clipRectTextFormat;
#endif

#ifdef __WXMSW__
    // ScintillaWin
    HWND MainHWND() const noexcept;

    // DBCS
    void ImeStartComposition();
    void ImeEndComposition();
#endif

    friend class ibSTCCallTip;
    friend class ibSTCTimer; // To get access to TickReason declaration
};

//----------------------------------------------------------------------
#endif
