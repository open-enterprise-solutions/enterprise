#include "controltextEditor.h"

#include <wx/dcbuffer.h>
#include <wx/dcscreen.h>
#include <wx/renderer.h>
#include <wx/settings.h>

#include "backend/appData.h"

//text event
wxDEFINE_EVENT(wxEVT_CONTROL_TEXT_ENTER, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_CONTROL_TEXT_INPUT, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_CONTROL_TEXT_CLEAR, wxCommandEvent);

//button event
wxDEFINE_EVENT(wxEVT_CONTROL_BUTTON_OPEN, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_CONTROL_BUTTON_SELECT, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_CONTROL_BUTTON_CLEAR, wxCommandEvent);

wxBEGIN_EVENT_TABLE(ibControlTextEditor, wxWindow)
	EVT_SIZE(ibControlTextEditor::OnSize)
	EVT_DPI_CHANGED(ibControlTextEditor::OnDPIChanged)
	EVT_PAINT(ibControlTextEditor::OnPaint)
	EVT_ERASE_BACKGROUND(ibControlTextEditor::OnEraseBackground)
	EVT_MOTION(ibControlTextEditor::OnMouseMotion)
	EVT_LEAVE_WINDOW(ibControlTextEditor::OnMouseLeave)
	EVT_LEFT_DOWN(ibControlTextEditor::OnLeftDown)
	EVT_LEFT_UP(ibControlTextEditor::OnLeftUp)
	EVT_MOUSE_CAPTURE_LOST(ibControlTextEditor::OnCaptureLost)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(ibControlTextEditor::ibControlCustomTextEditor, wxTextCtrl)
	EVT_TEXT(wxID_ANY, ibControlTextEditor::ibControlCustomTextEditor::OnText)
	EVT_TEXT_ENTER(wxID_ANY, ibControlTextEditor::ibControlCustomTextEditor::OnTextEnter)
	EVT_TEXT_MAXLEN(wxID_ANY, ibControlTextEditor::ibControlCustomTextEditor::OnText)
	EVT_KEY_DOWN(ibControlTextEditor::ibControlCustomTextEditor::OnKeyDown)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(ibControlTextEditor, wxWindow);

// accessors
// ---------

wxString ibControlTextEditor::DoGetValue() const
{
	return m_text->GetValue();
}
wxString ibControlTextEditor::GetRange(long from, long to) const
{
	return m_text->GetRange(from, to);
}

int ibControlTextEditor::GetLineLength(long lineNo) const
{
	return m_text->GetLineLength(lineNo);
}
wxString ibControlTextEditor::GetLineText(long lineNo) const
{
	return m_text->GetLineText(lineNo);
}
int ibControlTextEditor::GetNumberOfLines() const
{
	return m_text->GetNumberOfLines();
}

bool ibControlTextEditor::IsModified() const
{
	return m_text->IsModified();
}
bool ibControlTextEditor::IsEditable() const
{
	return m_text->IsEditable();
}

// more readable flag testing methods
bool ibControlTextEditor::IsSingleLine() const
{
	return m_text->IsSingleLine();
}
bool ibControlTextEditor::IsMultiLine() const
{
	return m_text->IsMultiLine();
}

// If the return values from and to are the same, there is no selection.
void ibControlTextEditor::GetSelection(long* from, long* to) const
{
	m_text->GetSelection(from, to);
}

wxString ibControlTextEditor::GetStringSelection() const
{
	return m_text->GetStringSelection();
}

// operations
// ----------

// editing
void ibControlTextEditor::Clear()
{
	m_text->Clear();
}
void ibControlTextEditor::Replace(long from, long to, const wxString& value)
{
	m_text->Replace(from, to, value);
}
void ibControlTextEditor::Remove(long from, long to)
{
	m_text->Remove(from, to);
}

// load/save the controls contents from/to the file
bool ibControlTextEditor::LoadFile(const wxString& file)
{
	return m_text->LoadFile(file);
}
bool ibControlTextEditor::SaveFile(const wxString& file)
{
	return m_text->SaveFile(file);
}

// sets/clears the dirty flag
void ibControlTextEditor::MarkDirty()
{
	m_text->MarkDirty();
}
void ibControlTextEditor::DiscardEdits()
{
	m_text->DiscardEdits();
}

// set the max number of characters which may be entered in a single line
// text control
void ibControlTextEditor::SetMaxLength(unsigned long len)
{
	m_text->SetMaxLength(len);
}

// writing text inserts it at the current position, appending always
// inserts it at the end
void ibControlTextEditor::WriteText(const wxString& text)
{
	m_text->WriteText(text);
}
void ibControlTextEditor::AppendText(const wxString& text)
{
	m_text->AppendText(text);
}

// insert the character which would have resulted from this key event,
// return true if anything has been inserted
bool ibControlTextEditor::EmulateKeyPress(const wxKeyEvent& event)
{
	return m_text->EmulateKeyPress(event);
}

// text control under some platforms supports the text styles: these
// methods allow to apply the given text style to the given selection or to
// set/get the style which will be used for all appended text
bool ibControlTextEditor::SetStyle(long start, long end, const wxTextAttr& style)
{
	return m_text->SetStyle(start, end, style);
}
bool ibControlTextEditor::GetStyle(long position, wxTextAttr& style)
{
	return m_text->GetStyle(position, style);
}
bool ibControlTextEditor::SetDefaultStyle(const wxTextAttr& style)
{
	return m_text->SetDefaultStyle(style);
}
const wxTextAttr& ibControlTextEditor::GetDefaultStyle() const
{
	return m_text->GetDefaultStyle();
}

// translate between the position (which is just an index in the text ctrl
// considering all its contents as a single strings) and (x, y) coordinates
// which represent column and line.
long ibControlTextEditor::XYToPosition(long x, long y) const
{
	return m_text->XYToPosition(x, y);
}
bool ibControlTextEditor::PositionToXY(long pos, long* x, long* y) const
{
	return m_text->PositionToXY(pos, x, y);
}

void ibControlTextEditor::ShowPosition(long pos)
{
	m_text->ShowPosition(pos);
}

// find the character at position given in pixels
//
// NB: pt is in device coords (not adjusted for the client area origin nor
//     scrolling)
wxTextCtrlHitTestResult ibControlTextEditor::HitTest(const wxPoint& pt, long* pos) const
{
	return m_text->HitTest(pt, pos);
}
wxTextCtrlHitTestResult ibControlTextEditor::HitTest(const wxPoint& pt,
	wxTextCoord* col,
	wxTextCoord* row) const
{
	return m_text->HitTest(pt, col, row);
}

// Clipboard operations
void ibControlTextEditor::Copy()
{
	m_text->Copy();
}
void ibControlTextEditor::Cut()
{
	m_text->Cut();
}
void ibControlTextEditor::Paste()
{
	m_text->Paste();
}

bool ibControlTextEditor::CanCopy() const
{
	return m_text->CanCopy();
}
bool ibControlTextEditor::CanCut() const
{
	return m_text->CanCut();
}
bool ibControlTextEditor::CanPaste() const
{
	return m_text->CanPaste();
}

// Undo/redo
void ibControlTextEditor::Undo()
{
	m_text->Undo();
}
void ibControlTextEditor::Redo()
{
	m_text->Redo();
}

bool ibControlTextEditor::CanUndo() const
{
	return m_text->CanUndo();
}
bool ibControlTextEditor::CanRedo() const
{
	return m_text->CanRedo();
}

// Insertion point
void ibControlTextEditor::SetInsertionPoint(long pos)
{
	m_text->SetInsertionPoint(pos);
}
void ibControlTextEditor::SetInsertionPointEnd()
{
	m_text->SetInsertionPointEnd();
}
long ibControlTextEditor::GetInsertionPoint() const
{
	return m_text->GetInsertionPoint();
}
long ibControlTextEditor::GetLastPosition() const
{
	return m_text->GetLastPosition();
}

void ibControlTextEditor::SetSelection(long from, long to)
{
	m_text->SetSelection(from, to);
}
void ibControlTextEditor::SelectAll()
{
	m_text->SelectAll();
}

void ibControlTextEditor::SetEditable(bool editable)
{
	m_text->SetEditable(editable);
}

// Autocomplete
bool ibControlTextEditor::DoAutoCompleteStrings(const wxArrayString& choices)
{
	return m_text->AutoComplete(choices);
}

bool ibControlTextEditor::DoAutoCompleteFileNames(int flags)
{
	return flags == wxFILE ? m_text->AutoCompleteFileNames() : m_text->AutoCompleteDirectories();
}

bool ibControlTextEditor::DoAutoCompleteCustom(wxTextCompleter* completer)
{
	return m_text->AutoComplete(completer);
}

// Note that overriding DoSetValue() is currently insufficient because the base
// class ChangeValue() only updates m_hintData of this object (which is null
// anyhow), instead of updating m_text->m_hintData, see #16998.
void ibControlTextEditor::ChangeValue(const wxString& value)
{
	m_text->ChangeValue(value);
}

void ibControlTextEditor::DoSetValue(const wxString& value, int flags)
{
	m_text->DoSetValue(value, flags);
}

bool ibControlTextEditor::DoLoadFile(const wxString& file, int fileType)
{
	return m_text->DoLoadFile(file, fileType);
}

bool ibControlTextEditor::DoSaveFile(const wxString& file, int fileType)
{
	return m_text->DoSaveFile(file, fileType);
}

// ----------------------------------------------------------------------------
// geometry
// ----------------------------------------------------------------------------

wxSize ibControlTextEditor::ComputeLabelBestSize() const
{
	if (m_dvcMode || m_labelText.empty())
		return wxSize(0, 0);

	if (m_cachedLabelSize.x < 0) {
		// Measure line-by-line so labels with explicit newlines push the
		// control taller instead of being clipped on a single baseline.
		const wxArrayString lines = wxSplit(m_labelText, wxT('\n'));
		int totalH = 0, maxW = 0;
		for (const wxString& line : lines) {
			wxCoord w = 0, h = 0;
			GetTextExtent(line, &w, &h);
			if (w > maxW) maxW = w;
			totalH += h;
		}
		m_cachedLabelSize = wxSize(maxW, totalH);
	}
	return m_cachedLabelSize;
}

wxSize ibControlTextEditor::ComputeButtonAreaSize() const
{
	int n = VisibleButtonCount();
	if (n == 0) return wxSize(0, 0);
	return wxSize(n * BtnSlotWidth() + 1, BtnSlotHeight());
}

void ibControlTextEditor::EnsureSlotMetrics() const
{
	if (m_cachedSlotW >= 0 && m_cachedSlotH >= 0)
		return;

	const int charH = GetCharHeight();
	const int wFromFont = charH + FromDIP(2);
	const int wMin      = FromDIP(buttonSize - 7);
	m_cachedSlotW = wFromFont > wMin ? wFromFont : wMin;

	const int hFromFont = charH + FromDIP(4);
	const int hMin      = FromDIP(buttonSize - 2);
	m_cachedSlotH = hFromFont > hMin ? hFromFont : hMin;
}

wxSize ibControlTextEditor::DoGetBestClientSize() const
{
	wxSize size = m_text != nullptr ? m_text->GetBestSize() : wxSize(0, 0);
	wxSize labelSize = ComputeLabelBestSize();

	if (m_labelMinSize.x > 0)
		labelSize.x = m_labelMinSize.x;

	if (size.y < labelSize.y) size.y = labelSize.y;

	// Button presence must not change the default size — buttons fit inside
	// whatever height the text area already has. Same rule for the width.
	// Include the visual gap between the label and the framed text area.
	const int gap = labelSize.x > 0 ? FromDIP(4) : 0;
	size.x += labelSize.x + gap;
	return size;
}

wxWindowList ibControlTextEditor::GetCompositeWindowParts() const
{
	wxWindowList parts;
	if (!m_destroying && m_text != nullptr)
		parts.push_back(m_text);
	return parts;
}

bool ibControlTextEditor::ShouldInheritColours() const
{
	return true;
}

bool ibControlTextEditor::Enable(bool enable)
{
	m_enabledIntent = enable;
	const bool inDesigner = appData != nullptr && appData->DesignerMode();

	// Match the original "panel over the control" idiom: in designer mode the
	// outer window stays enabled so clicks bubble up to the designer's
	// selection machinery even when the form sets Enabled=false. The inner
	// text control follows the Enabled property so it greys out only when the
	// user actually wants it disabled.
	bool result;
	if (inDesigner) {
		result = wxWindow::Enable(true);
		if (m_text != nullptr) m_text->Enable(enable);
	}
	else {
		result = wxWindow::Enable(enable);
		if (m_text != nullptr) m_text->Enable(enable);
	}
	Refresh();
	return result;
}

void ibControlTextEditor::LayoutControls()
{
	if (m_text == nullptr)
		return;

	const wxSize sizeTotal = GetClientSize();
	const int width = sizeTotal.x;
	const int height = sizeTotal.y;

	const int slotW = BtnSlotWidth();

	// label occupies left part (in non-dvc mode)
	int labelW = 0;
	if (!m_dvcMode) {
		wxSize lblSz = ComputeLabelBestSize();
		if (m_labelMinSize.x > 0) lblSz.x = m_labelMinSize.x;
		labelW = lblSz.x;
		m_labelRect = wxRect(0, 0, labelW, height);
	}
	else {
		m_labelRect = wxRect();
	}

	// framed area: text + buttons share a single 1px border. Leave a small
	// gap after the label so it doesn't butt up against the frame edge.
	const int labelGap = labelW > 0 ? FromDIP(4) : 0;
	const int frameX = m_dvcMode ? 0 : (labelW + labelGap);
	const int frameW = width - frameX;
	m_frameRect = wxRect(frameX, 0, std::max(0, frameW), height);

	// inset for 1px border on all sides
	const int innerX = frameX + 1;
	const int innerY = 1;
	const int innerH = std::max(0, height - 2);

	// button area flush to right edge of frame
	const int btnCount = VisibleButtonCount();
	const int btnTotalW = btnCount * slotW;
	const int btnAreaX = frameX + frameW - 1 - btnTotalW;

	int cursor = btnAreaX;
	auto place = [&](ButtonSlot& b) {
		if (!b.visible) { b.rect = wxRect(); return; }
		b.rect = wxRect(cursor, innerY, slotW, innerH);
		cursor += slotW;
	};
	place(m_btnSelect);
	place(m_btnClear);
	place(m_btnOpen);

	// Native single-line EDIT top-aligns its text inside the HWND. Size
	// m_text tightly around the glyph height, then place it slightly below
	// the mathematical centre to compensate for the edit control's intrinsic
	// top padding so the caret lands on the visual middle of the frame.
	int textW = btnAreaX - innerX;
	if (textW < 0) textW = 0;
	int textH = GetCharHeight() + FromDIP(2);
	if (textH > innerH || textH <= 0) textH = innerH;
	int textY = innerY + (innerH - textH) / 2 + FromDIP(1);
	if (textY + textH > innerY + innerH) textY = innerY + innerH - textH;
	m_text->SetSize(innerX, textY, textW, textH);
	m_textRect = wxRect(innerX, textY, textW, textH);
}

// ----------------------------------------------------------------------------
// paint / mouse
// ----------------------------------------------------------------------------

void ibControlTextEditor::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxAutoBufferedPaintDC dc(this);

	const wxColour bg = GetBackgroundColour();
	dc.SetBackground(bg);
	dc.Clear();

	dc.SetFont(GetFont());

	// label on the left — vertically centred on the frame (which is also the
	// visual centre of the caret) so the label baseline aligns with typed
	// text. Long lines get an ellipsis at the end (matches the original
	// ibDynamicStaticText wxST_ELLIPSIZE_END behaviour); explicit newlines
	// are honoured so multi-line labels can span several rows.
	if (!m_dvcMode && !m_labelText.empty()) {
		dc.SetTextForeground(m_enabledIntent
			? GetForegroundColour()
			: wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

		wxString label;
		if (m_labelRect.width > 0 && m_labelText.Find(wxT('\n')) == wxNOT_FOUND) {
			label = wxControl::Ellipsize(m_labelText, dc, wxELLIPSIZE_END, m_labelRect.width);
		}
		else if (m_labelRect.width > 0) {
			const wxArrayString lines = wxSplit(m_labelText, wxT('\n'));
			for (size_t i = 0; i < lines.size(); ++i) {
				if (i > 0) label += wxT('\n');
				label += wxControl::Ellipsize(lines[i], dc, wxELLIPSIZE_END, m_labelRect.width);
			}
		}
		else {
			label = m_labelText;
		}

		wxCoord tw = 0, th = 0;
		dc.GetMultiLineTextExtent(label, &tw, &th);
		const int centerY = !m_frameRect.IsEmpty()
			? m_frameRect.y + m_frameRect.height / 2
			: m_labelRect.y + m_labelRect.height / 2;
		dc.DrawText(label, m_labelRect.x, centerY - th / 2);
	}

	// unified frame around text + buttons: interior mirrors m_text's
	// own bg colour (SetBackgroundColour delegates onto m_text). When
	// the form sets a custom property colour both ends up equal —
	// frame fills as one solid block. When no custom colour is set
	// m_text keeps wxTextCtrl's native white, so the frame stays white
	// too (the original behaviour). Querying m_text directly avoids
	// having to distinguish "explicit" vs "inherited" bg on this
	// composite control.
	if (!m_frameRect.IsEmpty()) {
		const wxColour interior = m_enabledIntent
			? (m_text != nullptr ? m_text->GetBackgroundColour()
			                     : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW))
			: wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
		dc.SetBrush(interior);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(m_frameRect);

		const wxColour borderCol = m_enabledIntent
			? wxSystemSettings::GetColour(wxSYS_COLOUR_ACTIVEBORDER)
			: wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
		dc.SetPen(wxPen(borderCol));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(m_frameRect);
	}

	DrawButton(dc, m_btnSelect);
	DrawButton(dc, m_btnClear);
	DrawButton(dc, m_btnOpen);
}

void ibControlTextEditor::DrawButton(wxDC& dc, const ButtonSlot& b)
{
	if (!b.visible || b.rect.IsEmpty())
		return;

	// A button reads enabled only when the CONTROL is enabled AND the slot itself is — a per-button disabled
	// slot (Select / Clear on a read-only binding) greys out just like the whole-control disabled state.
	const bool on = m_enabledIntent && b.enabled;

	// flat embedded style: no bevel, just a subtle fill on hover/pressed so the
	// buttons read as inline affordances inside the text field, not standalone
	// push buttons.
	const wxColour base = on
		? wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)
		: wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);

	wxColour fill = base;
	if (on) {
		if (b.pressed && b.hovered) fill = base.ChangeLightness(85);
		else if (b.hovered)         fill = base.ChangeLightness(93);
	}

	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(fill);
	dc.DrawRectangle(b.rect);

	// thin vertical separator on the left of the button to visually group them
	const wxColour sepCol = on
		? wxSystemSettings::GetColour(wxSYS_COLOUR_ACTIVEBORDER)
		: wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
	dc.SetPen(wxPen(sepCol));
	dc.DrawLine(b.rect.x, b.rect.y + 2, b.rect.x, b.rect.y + b.rect.height - 2);

	dc.SetTextForeground(on
		? GetForegroundColour()
		: wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

	wxCoord tw = 0, th = 0;
	dc.GetTextExtent(b.text, &tw, &th);

	int tx = b.rect.x + (b.rect.width - tw) / 2;
	int ty = b.rect.y + (b.rect.height - th) / 2;
	if (b.pressed && b.hovered) { tx += 1; ty += 1; }

	dc.DrawText(b.text, tx, ty);
}

ibControlTextEditor::ButtonSlot* ibControlTextEditor::HitTestButton(const wxPoint& p)
{
	// A disabled slot is inert — skip it so it takes no hover / press / click (greyed but dead).
	if (m_btnSelect.visible && m_btnSelect.enabled && m_btnSelect.rect.Contains(p)) return &m_btnSelect;
	if (m_btnClear.visible  && m_btnClear.enabled  && m_btnClear.rect.Contains(p))  return &m_btnClear;
	if (m_btnOpen.visible   && m_btnOpen.enabled   && m_btnOpen.rect.Contains(p))   return &m_btnOpen;
	return nullptr;
}

void ibControlTextEditor::OnMouseMotion(wxMouseEvent& event)
{
	const wxPoint p = event.GetPosition();
	bool refresh = false;

	auto update = [&](ButtonSlot& b) {
		const bool over = b.visible && b.rect.Contains(p);
		if (over != b.hovered) { b.hovered = over; refresh = true; }
	};
	update(m_btnSelect);
	update(m_btnClear);
	update(m_btnOpen);

	if (refresh) Refresh();
	event.Skip();
}

void ibControlTextEditor::OnMouseLeave(wxMouseEvent& event)
{
	bool refresh = false;
	auto clear = [&](ButtonSlot& b) {
		if (b.hovered && !b.pressed) { b.hovered = false; refresh = true; }
	};
	clear(m_btnSelect);
	clear(m_btnClear);
	clear(m_btnOpen);

	if (refresh) Refresh();
	event.Skip();
}

void ibControlTextEditor::OnLeftDown(wxMouseEvent& event)
{
	// When the user logically disabled the control (Enabled=false) we do not
	// capture the click; it continues to bubble so that the designer can
	// select/edit the control as before.
	if (!m_enabledIntent) { event.Skip(); return; }
	const wxPoint p = event.GetPosition();
	ButtonSlot* hit = HitTestButton(p);
	if (hit != nullptr) {
		hit->pressed = true;
		hit->hovered = true;
		if (!HasCapture()) CaptureMouse();
		Refresh();
	}
	event.Skip();
}

void ibControlTextEditor::OnLeftUp(wxMouseEvent& event)
{
	if (HasCapture()) ReleaseMouse();

	const wxPoint p = event.GetPosition();
	ButtonSlot* hit = HitTestButton(p);

	// Reset the pressed state synchronously (while `this` is alive), but DEFER
	// firing the button command via QueueEvent. The handler (Select/Open) can
	// open a modal dialog and rebuild the form, destroying this control;
	// dispatching synchronously here and then touching `this` again (the next
	// fire() calls, Refresh) is a use-after-free. Queueing runs the command
	// after OnLeftUp has fully unwound and `this` is off the stack.
	auto fire = [&](ButtonSlot& b) {
		if (!b.pressed) return;
		const bool clicked = (hit == &b);
		b.pressed = false;
		if (clicked) {
			wxCommandEvent ev(b.eventType, GetId());
			ev.SetEventObject(m_text);
			if (b.eventType != wxEVT_CONTROL_BUTTON_CLEAR && m_text != nullptr)
				ev.SetString(GetValue());
			if (m_text != nullptr) m_text->SetFocus();
			GetEventHandler()->QueueEvent(ev.Clone());
		}
	};
	fire(m_btnSelect);
	fire(m_btnClear);
	fire(m_btnOpen);

	Refresh();
	event.Skip();
}

void ibControlTextEditor::OnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
	auto reset = [&](ButtonSlot& b) { b.pressed = false; b.hovered = false; };
	reset(m_btnSelect);
	reset(m_btnClear);
	reset(m_btnOpen);
	Refresh();
}

// ----------------------------------------------------------------------------
// label (ibControlDynamicBorder)
// ----------------------------------------------------------------------------

void ibControlTextEditor::CalculateLabelSize(wxCoord* w, wxCoord* h) const
{
	m_labelMinSize = wxDefaultSize;
	const wxSize s = ComputeLabelBestSize();
	if (w) *w = s.x;
	if (h) *h = s.y;
}

void ibControlTextEditor::ApplyLabelSize(const wxSize& s)
{
	// Called once per control on every form-wide alignment pass. If the
	// computed width hasn't changed there's nothing to re-layout — skip
	// the InvalidateBestSize / LayoutControls / Refresh chain.
	if (m_labelMinSize == s)
		return;

	m_labelMinSize = s;
	InvalidateBestSize();
	LayoutControls();
	Refresh();
}
