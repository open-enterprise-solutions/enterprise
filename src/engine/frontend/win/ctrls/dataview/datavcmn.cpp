/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/datavcmn.cpp
// Purpose:     ibDataViewCtrl base classes and common parts
// Author:      Robert Roebling
// Created:     2006/02/20
// Copyright:   (c) 2006, Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#if wxUSE_DATAVIEWCTRL

#include "dataview.h"

#ifndef WX_PRECOMP
#include <wx/dc.h>
#include <wx/settings.h>
#include <wx/log.h>
#include <wx/crt.h>
#endif

#include <wx/datectrl.h>
#include <wx/except.h>
#include <wx/spinctrl.h>
#include <wx/choice.h>
#include <wx/imaglist.h>
#include <wx/renderer.h>
#include <wx/uilocale.h>

#if wxUSE_ACCESSIBILITY
#include <wx/access.h>
#endif // wxUSE_ACCESSIBILITY

// Uncomment this line to, for custom renderers, visually show the extent
// of both a cell and its item.
//#define DEBUG_RENDER_EXTENTS

const char ibDataViewCtrlNameStr[] = "dataviewCtrl";

namespace
{

	// Custom handler pushed on top of the edit control used by ibDataViewCtrl to
	// forward some events to the main control itself.
	class ibDataViewEditorCtrlEvtHandler : public wxEvtHandler
	{
	public:
		ibDataViewEditorCtrlEvtHandler(wxWindow* editor, ibDataViewRenderer* owner)
		{
			m_editorCtrl = editor;
			m_owner = owner;

			m_finished = false;
		}

		void SetFocusOnIdle(bool focus = true) { m_focusOnIdle = focus; }

	protected:
		void OnChar(wxKeyEvent& event);
		void OnTextEnter(wxCommandEvent& event);
		void OnKillFocus(wxFocusEvent& event);
		void OnIdle(wxIdleEvent& event);

	private:
		bool IsEditorSubControl(wxWindow* win) const;

		ibDataViewRenderer* m_owner;
		wxWindow* m_editorCtrl;
		bool                    m_finished;
		bool                    m_focusOnIdle;

	private:
		wxDECLARE_EVENT_TABLE();
	};

} // anonymous namespace

// ---------------------------------------------------------
// ibDataViewIndexListModel
// ---------------------------------------------------------

static int my_sort(int* v1, int* v2)
{
	return *v2 - *v1;
}

ibDataViewIndexListModel::ibDataViewIndexListModel(unsigned int initial_size)
{
	// IDs are ordered until an item gets deleted or inserted
	m_ordered = true;

	// build initial index
	unsigned int i;
	for (i = 1; i < initial_size + 1; i++)
		m_hash.Add(ibDataViewItem(wxUIntToPtr(i)));
	m_nextFreeID = initial_size + 1;
}

void ibDataViewIndexListModel::Reset(unsigned int new_size)
{
	/* ibDataViewModel:: */ BeforeReset();

	m_hash.Clear();

	// IDs are ordered until an item gets deleted or inserted
	m_ordered = true;

	// build initial index
	unsigned int i;
	for (i = 1; i < new_size + 1; i++)
		m_hash.Add(ibDataViewItem(wxUIntToPtr(i)));

	m_nextFreeID = new_size + 1;

	/* ibDataViewModel:: */ AfterReset();
}

void ibDataViewIndexListModel::RowPrepended()
{
	m_ordered = false;

	unsigned int id = m_nextFreeID;
	m_nextFreeID++;

	ibDataViewItem item(wxUIntToPtr(id));
	m_hash.Insert(item, 0);
	ItemAdded(ibDataViewItem(0), item);

}

void ibDataViewIndexListModel::RowInserted(unsigned int before)
{
	m_ordered = false;

	unsigned int id = m_nextFreeID;
	m_nextFreeID++;

	ibDataViewItem item(wxUIntToPtr(id));
	m_hash.Insert(item, before);
	ItemAdded(ibDataViewItem(0), item);
}

void ibDataViewIndexListModel::RowAppended()
{
	unsigned int id = m_nextFreeID;
	m_nextFreeID++;

	ibDataViewItem item(wxUIntToPtr(id));
	m_hash.Add(item);
	ItemAdded(ibDataViewItem(0), item);
}

void ibDataViewIndexListModel::RowDeleted(unsigned int row)
{
	m_ordered = false;

	ibDataViewItem item(m_hash[row]);
	m_hash.RemoveAt(row);
	/* ibDataViewModel:: */ ItemDeleted(ibDataViewItem(0), item);
}

void ibDataViewIndexListModel::RowsDeleted(const wxArrayInt& rows)
{
	m_ordered = false;

	ibDataViewItemArray array;
	unsigned int i;
	for (i = 0; i < rows.GetCount(); i++)
	{
		ibDataViewItem item(m_hash[rows[i]]);
		array.Add(item);
	}

	wxArrayInt sorted = rows;
	sorted.Sort(my_sort);
	for (i = 0; i < sorted.GetCount(); i++)
		m_hash.RemoveAt(sorted[i]);

	/* ibDataViewModel:: */ ItemsDeleted(ibDataViewItem(0), array);
}

void ibDataViewIndexListModel::RowChanged(unsigned int row)
{
	/* ibDataViewModel:: */ ItemChanged(GetItem(row));
}

void ibDataViewIndexListModel::RowValueChanged(unsigned int row, unsigned int col)
{
	/* ibDataViewModel:: */ ValueChanged(GetItem(row), col);
}

unsigned int ibDataViewIndexListModel::GetRow(const ibDataViewItem& item) const
{
	if (m_ordered)
		return wxPtrToUInt(item.GetID()) - 1;

	// assert for not found
	return (unsigned int)m_hash.Index(item);
}

ibDataViewItem ibDataViewIndexListModel::GetItem(unsigned int row) const
{
	wxCHECK_MSG(row < m_hash.GetCount(), ibDataViewItem(), wxS("invalid index"));
	return ibDataViewItem(m_hash[row]);
}

unsigned int ibDataViewIndexListModel::GetChildren(const ibDataViewItem& item, ibDataViewItemArray& children) const
{
	if (item.IsOk())
		return 0;

	children = m_hash;

	return m_hash.GetCount();
}

// ---------------------------------------------------------
// ibDataViewVirtualListModel
// ---------------------------------------------------------

#ifndef __WXOSX__

ibDataViewVirtualListModel::ibDataViewVirtualListModel(unsigned int initial_size)
{
	m_size = initial_size;
}

void ibDataViewVirtualListModel::Reset(unsigned int new_size)
{
	/* ibDataViewModel:: */ BeforeReset();

	m_size = new_size;

	/* ibDataViewModel:: */ AfterReset();
}

void ibDataViewVirtualListModel::RowPrepended()
{
	m_size++;
	ibDataViewItem item(wxUIntToPtr(1));
	ItemAdded(ibDataViewItem(0), item);
}

void ibDataViewVirtualListModel::RowInserted(unsigned int before)
{
	m_size++;
	ibDataViewItem item(wxUIntToPtr(before + 1));
	ItemAdded(ibDataViewItem(0), item);
}

void ibDataViewVirtualListModel::RowAppended()
{
	m_size++;
	ibDataViewItem item(wxUIntToPtr(m_size));
	ItemAdded(ibDataViewItem(0), item);
}

void ibDataViewVirtualListModel::RowDeleted(unsigned int row)
{
	m_size--;
	ibDataViewItem item(wxUIntToPtr(row + 1));
	/* ibDataViewModel:: */ ItemDeleted(ibDataViewItem(0), item);
}

void ibDataViewVirtualListModel::RowsDeleted(const wxArrayInt& rows)
{
	m_size -= rows.GetCount();

	wxArrayInt sorted = rows;
	sorted.Sort(my_sort);

	ibDataViewItemArray array;
	unsigned int i;
	for (i = 0; i < sorted.GetCount(); i++)
	{
		ibDataViewItem item(wxUIntToPtr(sorted[i] + 1));
		array.Add(item);
	}
	/* ibDataViewModel:: */ ItemsDeleted(ibDataViewItem(0), array);
}

void ibDataViewVirtualListModel::RowChanged(unsigned int row)
{
	/* ibDataViewModel:: */ ItemChanged(GetItem(row));
}

void ibDataViewVirtualListModel::RowValueChanged(unsigned int row, unsigned int col)
{
	/* ibDataViewModel:: */ ValueChanged(GetItem(row), col);
}

unsigned int ibDataViewVirtualListModel::GetRow(const ibDataViewItem& item) const
{
	return wxPtrToUInt(item.GetID()) - 1;
}

ibDataViewItem ibDataViewVirtualListModel::GetItem(unsigned int row) const
{
	return ibDataViewItem(wxUIntToPtr(row + 1));
}

bool ibDataViewVirtualListModel::HasDefaultCompare() const
{
	return true;
}

int ibDataViewVirtualListModel::Compare(const ibDataViewItem& item1,
	const ibDataViewItem& item2,
	unsigned int WXUNUSED(column),
	bool ascending) const
{
	unsigned int pos1 = wxPtrToUInt(item1.GetID());  // -1 not needed here
	unsigned int pos2 = wxPtrToUInt(item2.GetID());  // -1 not needed here

	if (ascending)
		return pos1 - pos2;
	else
		return pos2 - pos1;
}

unsigned int ibDataViewVirtualListModel::GetChildren(const ibDataViewItem& WXUNUSED(item), ibDataViewItemArray& WXUNUSED(children)) const
{
	return 0;  // should we report an error ?
}

#endif  // __WXOSX__

//-----------------------------------------------------------------------------
// ibDataViewIconText
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibDataViewIconText, wxObject);

IMPLEMENT_VARIANT_OBJECT(ibDataViewIconText)

// ---------------------------------------------------------
// ibDataViewRendererBase
// ---------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibDataViewRendererBase, wxObject);

ibDataViewRendererBase::ibDataViewRendererBase(const wxString& varianttype,
	ibDataViewCellMode WXUNUSED(mode),
	int WXUNUSED(align))
	: m_variantType(varianttype)
{
	m_owner = NULL;
	m_valueAdjuster = NULL;
}

ibDataViewRendererBase::~ibDataViewRendererBase()
{
	if (m_editorCtrl)
		DestroyEditControl();
	delete m_valueAdjuster;
}

ibDataViewCtrl* ibDataViewRendererBase::GetView() const
{
	return const_cast<ibDataViewRendererBase*>(this)->GetOwner()->GetOwner();
}

bool ibDataViewRendererBase::StartEditing(const ibDataViewItem& item, wxRect labelRect)
{
	ibDataViewColumn* const column = GetOwner();
	ibDataViewCtrl* const dv_ctrl = column->GetOwner();

	// Before doing anything we send an event asking if editing of this item is really wanted.
	ibDataViewEvent event(wxEVT_DATAVIEW_ITEM_START_EDITING, dv_ctrl, column, item);
	dv_ctrl->GetEventHandler()->ProcessEvent(event);
	if (!event.IsAllowed())
		return false;

	// Remember the item being edited for use in FinishEditing() later.
	m_item = item;

	unsigned int col = GetOwner()->GetModelColumn();
	const wxVariant& value = CheckedGetValue(dv_ctrl->GetModel(), item, col);

	m_editorCtrl = CreateEditorCtrl(
		(wxWindow*)dv_ctrl->CellToDataViewWindow(item, column), labelRect, value);

	// there might be no editor control for the given item
	if (!m_editorCtrl)
	{
		m_item = ibDataViewItem();
		return false;
	}

	ibDataViewEditorCtrlEvtHandler* handler =
		new ibDataViewEditorCtrlEvtHandler(m_editorCtrl, (ibDataViewRenderer*)this);

	m_editorCtrl->PushEventHandler(handler);

#if defined(__WXGTK20__)
	handler->SetFocusOnIdle();
#else
	m_editorCtrl->SetFocus();
#endif

	return true;
}

void ibDataViewRendererBase::NotifyEditingStarted(const ibDataViewItem& item)
{
	ibDataViewColumn* const column = GetOwner();
	ibDataViewCtrl* const dv_ctrl = column->GetOwner();

	ibDataViewEvent event(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, dv_ctrl, column, item);
	dv_ctrl->GetEventHandler()->ProcessEvent(event);
}

void ibDataViewRendererBase::DestroyEditControl()
{
	// Remove our event handler first to prevent it from (recursively) calling
	// us again as it would do via a call to FinishEditing() when the editor
	// loses focus when we hide it below.
	wxEvtHandler* const handler = m_editorCtrl->PopEventHandler();

	// Hide the control immediately but don't delete it yet as there could be
	// some pending messages for it.
	m_editorCtrl->Hide();

	wxPendingDelete.Append(handler);
	wxPendingDelete.Append(m_editorCtrl);

	// Ensure that DestroyEditControl() is not called again for this control.
	m_editorCtrl.Release();
}

void ibDataViewRendererBase::CancelEditing()
{
	if (m_editorCtrl)
		DestroyEditControl();

	DoHandleEditingDone(NULL);
}

bool ibDataViewRendererBase::FinishEditing()
{
	if (!m_editorCtrl)
		return true;

	bool gotValue = false;

	wxVariant value;
	if (GetValueFromEditorCtrl(m_editorCtrl, value))
	{
		// This is the normal case and we will use this value below (if it
		// passes validation).
		gotValue = true;
	}
	//else: Not really supposed to happen, but still proceed with
	//      destroying the edit control if it does.

	DestroyEditControl();

	GetView()->GetMainWindow()->SetFocus();

	return DoHandleEditingDone(gotValue ? &value : NULL);
}

bool
ibDataViewRendererBase::DoHandleEditingDone(wxVariant* value)
{
	if (value)
	{
		if (!Validate(*value))
		{
			// Invalid value can't be used, so if it's the same as if we hadn't
			// got it in the first place.
			value = NULL;
		}
	}

	ibDataViewColumn* const column = GetOwner();
	ibDataViewCtrl* const dv_ctrl = column->GetOwner();
	unsigned int col = column->GetModelColumn();

	// Now we should send Editing Done event
	ibDataViewEvent event(wxEVT_DATAVIEW_ITEM_EDITING_DONE, dv_ctrl, column, m_item);
	if (value)
		event.SetValue(*value);
	else
		event.SetEditCancelled();

	dv_ctrl->GetEventHandler()->ProcessEvent(event);

	bool accepted = false;
	if (value && event.IsAllowed())
	{
		dv_ctrl->GetModel()->ChangeValue(*value, m_item, col);
		accepted = true;
	}

	m_item = ibDataViewItem();

	return accepted;
}

wxVariant
ibDataViewRendererBase::CheckedGetValue(const ibDataViewModel* model,
	const ibDataViewItem& item,
	unsigned column) const
{
	wxVariant value;
	// Avoid calling GetValue() if the model isn't supposed to have any values
	// in this cell (e.g. a non-first column of a container item), this could
	// be unexpected.
	if (model->HasValue(item, column))
		model->GetValue(value, item, column);

	// We always allow the cell to be null, regardless of the renderer type.
	if (!value.IsNull())
	{
		if (!IsCompatibleVariantType(value.GetType()))
		{
			// If you're seeing this message, this indicates that either your
			// renderer is using the wrong type, or your model returns values
			// of the wrong type.
			wxLogDebug("Wrong type returned from the model for column %u: "
				"%s required but actual type is %s",
				column,
				GetVariantType(),
				value.GetType());

			// Don't return data of mismatching type, this could be unexpected.
			value.MakeNull();
		}
	}

	return value;
}

bool
ibDataViewRendererBase::PrepareForItem(const ibDataViewModel* model,
	const ibDataViewItem& item,
	unsigned column)
{
	// This method is called by the native control, so we shouldn't allow
	// exceptions to escape from it.
	wxTRY
	{

		// Now check if we have a value and remember it if we do.
		wxVariant value = CheckedGetValue(model, item, column);

		if (!value.IsNull())
		{
			if (m_valueAdjuster)
			{
				if (IsHighlighted())
					value = m_valueAdjuster->MakeHighlighted(value);
			}

			SetValue(value);
		}

		// Also set up the attributes: note that we need to do this even for the
		// empty cells because background colour is still relevant for them.
		ibDataViewItemAttr attr;
		model->GetAttr(item, column, attr);
		SetAttr(attr);

		// Finally determine the enabled/disabled state and apply it, even to the
		// empty cells.
		SetEnabled(model->IsEnabled(item, column));

		return !value.IsNull();
	}
		wxCATCH_ALL
		(
			// There is not much we can do about it here, just log it and don't
			// show anything in this cell.
			wxLogDebug("Retrieving the value from the model threw an exception");
	return false;
		)
}


int ibDataViewRendererBase::GetEffectiveAlignment() const
{
	int alignment = GetEffectiveAlignmentIfKnown();
	wxASSERT(alignment != wxDVR_DEFAULT_ALIGNMENT);
	return alignment;
}


int ibDataViewRendererBase::GetEffectiveAlignmentIfKnown() const
{
	int alignment = GetAlignment();

	if (alignment == wxDVR_DEFAULT_ALIGNMENT)
	{
		if (GetOwner() != NULL)
		{
			// if we don't have an explicit alignment ourselves, use that of the
			// column in horizontal direction and default vertical alignment
			alignment = GetOwner()->GetAlignment() | wxALIGN_CENTRE_VERTICAL;
		}
	}

	return alignment;
}

// ----------------------------------------------------------------------------
// ibDataViewCustomRendererBase
// ----------------------------------------------------------------------------

bool ibDataViewCustomRendererBase::ActivateCell(const wxRect& cell,
	ibDataViewModel* model,
	const ibDataViewItem& item,
	unsigned int col,
	const wxMouseEvent* mouseEvent)
{
	return ActivateCell(cell, model, item, col, mouseEvent);
}

void ibDataViewCustomRendererBase::RenderBackground(wxDC* dc, const wxRect& rect)
{
	if (!m_attr.HasBackgroundColour())
		return;

	const wxColour& colour = m_attr.GetBackgroundColour();
	wxDCPenChanger changePen(*dc, colour);
	wxDCBrushChanger changeBrush(*dc, colour);

	dc->DrawRectangle(rect);
}

void
ibDataViewCustomRendererBase::WXCallRender(wxRect rectCell, wxDC* dc, int state)
{
	wxCHECK_RET(dc, "no DC to draw on in custom renderer?");

	// adjust the rectangle ourselves to account for the alignment
	wxRect rectItem = rectCell;
	const int align = GetEffectiveAlignment();

	const wxSize size = GetSize();

	// take alignment into account only if there is enough space, otherwise
	// show as much contents as possible
	//
	// notice that many existing renderers (e.g. ibDataViewSpinRenderer)
	// return hard-coded size which can be more than they need and if we
	// trusted their GetSize() we'd draw the text out of cell bounds
	// entirely

	if (size.x >= 0 && size.x < rectCell.width)
	{
		if (align & wxALIGN_CENTER_HORIZONTAL)
			rectItem.x += (rectCell.width - size.x) / 2;
		else if (align & wxALIGN_RIGHT)
			rectItem.x += rectCell.width - size.x;
		// else: wxALIGN_LEFT is the default

		rectItem.width = size.x;
	}

	if (size.y >= 0 && size.y < rectCell.height)
	{
		if (align & wxALIGN_CENTER_VERTICAL)
			rectItem.y += (rectCell.height - size.y) / 2;
		else if (align & wxALIGN_BOTTOM)
			rectItem.y += rectCell.height - size.y;
		// else: wxALIGN_TOP is the default

		rectItem.height = size.y;
	}


	// set up the DC attributes

	// override custom foreground with the standard one for the selected items
	// because we currently don't allow changing the selection background and
	// custom colours may be unreadable on it
	wxColour col;
	if (state & wxDATAVIEW_CELL_SELECTED)
		col = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT);
	else if (m_attr.HasColour())
		col = m_attr.GetColour();
	else // use default foreground
		col = GetOwner()->GetOwner()->GetForegroundColour();

	wxDCTextColourChanger changeFg(*dc, col);

	wxDCFontChanger changeFont(*dc);
	if (m_attr.HasFont())
		changeFont.Set(m_attr.GetEffectiveFont(dc->GetFont()));

#ifdef DEBUG_RENDER_EXTENTS
	{

		wxDCBrushChanger changeBrush(*dc, *wxTRANSPARENT_BRUSH);
		wxDCPenChanger changePen(*dc, *wxRED);

		dc->DrawRectangle(rectCell);

		dc->SetPen(*wxGREEN);
		dc->DrawRectangle(rectItem);

	}
#endif

	Render(rectItem, dc, state);
}

wxSize ibDataViewCustomRendererBase::GetTextExtent(const wxString& str) const
{
	const ibDataViewCtrl* view = GetView();

	if (m_attr.HasFont())
	{
		wxFont font(m_attr.GetEffectiveFont(view->GetFont()));
		wxSize size;
		view->GetTextExtent(str, &size.x, &size.y, NULL, NULL, &font);
		return size;
	}
	else
	{
		return view->GetTextExtent(str);
	}
}

void
ibDataViewCustomRendererBase::RenderText(const wxString& text,
	int xoffset,
	wxRect rect,
	wxDC* dc,
	int state)
{
	wxRect rectText = rect;
	rectText.x += xoffset;
	rectText.width -= xoffset;

	int flags = 0;
	if (state & wxDATAVIEW_CELL_SELECTED)
		flags |= wxCONTROL_SELECTED;
	if (!(GetOwner()->GetOwner()->IsEnabled() && GetEnabled()))
		flags |= wxCONTROL_DISABLED;

	wxRendererNative::Get().DrawItemText(
		GetOwner()->GetOwner(),
		*dc,
		text,
		rectText,
		GetEffectiveAlignment(),
		flags,
		GetEllipsizeMode());
}

void ibDataViewCustomRendererBase::SetEnabled(bool enabled)
{
	m_enabled = enabled;
}

//-----------------------------------------------------------------------------
// ibDataViewEditorCtrlEvtHandler
//-----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibDataViewEditorCtrlEvtHandler, wxEvtHandler)
EVT_CHAR(ibDataViewEditorCtrlEvtHandler::OnChar)
EVT_KILL_FOCUS(ibDataViewEditorCtrlEvtHandler::OnKillFocus)
EVT_IDLE(ibDataViewEditorCtrlEvtHandler::OnIdle)
EVT_TEXT_ENTER(-1, ibDataViewEditorCtrlEvtHandler::OnTextEnter)
wxEND_EVENT_TABLE()

void ibDataViewEditorCtrlEvtHandler::OnIdle(wxIdleEvent& event)
{
	if (m_focusOnIdle)
	{
		m_focusOnIdle = false;

		// Ignore focused items within the compound editor control
		wxWindow* win = wxWindow::FindFocus();
		if (!IsEditorSubControl(win))
		{
			m_editorCtrl->SetFocus();
		}
	}

	event.Skip();
}

void ibDataViewEditorCtrlEvtHandler::OnTextEnter(wxCommandEvent& WXUNUSED(event))
{
	m_finished = true;
	m_owner->FinishEditing();
}

void ibDataViewEditorCtrlEvtHandler::OnChar(wxKeyEvent& event)
{
	switch (event.m_keyCode)
	{
	case WXK_ESCAPE:
		m_finished = true;
		m_owner->CancelEditing();
		break;

	case WXK_RETURN:
		if (!event.HasAnyModifiers())
		{
			m_finished = true;
			m_owner->FinishEditing();
			break;
		}
		wxFALLTHROUGH; // Ctrl/Alt/Shift-Enter is not handled specially

	default:
		event.Skip();
	}
}

void ibDataViewEditorCtrlEvtHandler::OnKillFocus(wxFocusEvent& event)
{
	// Ignore focus changes within the compound editor control
	wxWindow* win = event.GetWindow();
	if (IsEditorSubControl(win))
	{
		event.Skip();
		return;
	}

	if (!m_finished)
	{
		m_finished = true;
		m_owner->FinishEditing();
	}

	event.Skip();
}

bool ibDataViewEditorCtrlEvtHandler::IsEditorSubControl(wxWindow* win) const
{
	// Checks whether the given window belongs to the editor control
	// (is either the editor itself or a child of the compound editor).
	while (win)
	{
		if (win == m_editorCtrl)
		{
			return true;
		}

		win = win->GetParent();
	}

	return false;
}

// ---------------------------------------------------------
// ibDataViewColumnBase
// ---------------------------------------------------------

void ibDataViewColumnBase::Init(ibDataViewRenderer* renderer,
	unsigned int model_column)
{
	m_renderer = renderer;
	m_model_column = model_column;
	m_owner = NULL;
	m_renderer->SetOwner((ibDataViewColumn*)this);
}

ibDataViewColumnBase::~ibDataViewColumnBase()
{
	delete m_renderer;
}

// ---------------------------------------------------------
// ibDataViewCtrlBase
// ---------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibDataViewCtrlBase, wxControl);

ibDataViewCtrlBase::ibDataViewCtrlBase()
{
	m_model = NULL;
	m_expander_column = 0;
	m_indent = 8;
}

ibDataViewCtrlBase::~ibDataViewCtrlBase()
{
	if (m_model)
	{
		m_model->DecRef();
		m_model = NULL;
	}
}

bool ibDataViewCtrlBase::AssociateModel(ibDataViewModel* model)
{
	if (m_model)
	{
		m_model->DecRef();   // discard old model, if any
	}

	// add our own reference to the new model:
	m_model = model;
	if (m_model)
	{
		m_model->IncRef();
	}

	return true;
}

ibDataViewModel* ibDataViewCtrlBase::GetModel()
{
	return m_model;
}

const ibDataViewModel* ibDataViewCtrlBase::GetModel() const
{
	return m_model;
}

void ibDataViewCtrlBase::Expand(const ibDataViewItem& item)
{
	ExpandAncestors(item);

	DoExpand(item, false);
}

void ibDataViewCtrlBase::ExpandChildren(const ibDataViewItem& item)
{
	ExpandAncestors(item);

	DoExpand(item, true);
}

void ibDataViewCtrlBase::ExpandAncestors(const ibDataViewItem& item)
{
	if (!m_model) return;

	if (!item.IsOk()) return;

	wxVector<ibDataViewItem> parentChain;

	// at first we get all the parents of the selected item
	ibDataViewItem parent = m_model->GetParent(item);
	while (parent.IsOk())
	{
		parentChain.push_back(parent);
		parent = m_model->GetParent(parent);
	}

	// then we expand the parents, starting at the root
	while (!parentChain.empty())
	{
		DoExpand(parentChain.back(), false);
		parentChain.pop_back();
	}
}

ibDataViewItem ibDataViewCtrlBase::GetCurrentItem() const
{
	return HasFlag(wxDV_MULTIPLE) ? DoGetCurrentItem()
		: GetSelection();
}

void ibDataViewCtrlBase::SetCurrentItem(const ibDataViewItem& item)
{
	wxCHECK_RET(item.IsOk(), "Can't make current an invalid item.");

	if (HasFlag(wxDV_MULTIPLE))
		DoSetCurrentItem(item);
	else
		Select(item);
}

ibDataViewItem ibDataViewCtrlBase::GetSelection() const
{
	if (GetSelectedItemsCount() != 1)
		return ibDataViewItem();

	ibDataViewItemArray selections;
	GetSelections(selections);
	return selections[0];
}

namespace
{

	// Helper to account for inconsistent signature of ibDataViewProgressRenderer
	// ctor: it takes an extra "label" argument as first parameter, unlike all the
	// other renderers.
	template <typename Renderer>
	struct RendererFactory
	{
		static Renderer*
			New(ibDataViewCellMode mode, int align)
		{
			return new Renderer(Renderer::GetDefaultType(), mode, align);
		}
	};

	template <>
	struct RendererFactory<ibDataViewProgressRenderer>
	{
		static ibDataViewProgressRenderer*
			New(ibDataViewCellMode mode, int align)
		{
			return new ibDataViewProgressRenderer(
				wxString(),
				ibDataViewProgressRenderer::GetDefaultType(),
				mode,
				align
			);
		}
	};

	template <typename Renderer, typename LabelType>
	ibDataViewColumn*
		CreateColumnWithRenderer(const LabelType& label,
			unsigned model_column,
			ibDataViewCellMode mode,
			int width,
			wxAlignment align,
			int flags)
	{
		// For compatibility reason, handle wxALIGN_NOT as wxDVR_DEFAULT_ALIGNMENT
		// when creating the renderer here because a lot of existing code,
		// including our own dataview sample, uses wxALIGN_NOT just because it's
		// the default value of the alignment argument in AppendXXXColumn()
		// methods, but this doesn't mean that it actually wants to top-align the
		// column text.
		//
		// This does make it impossible to create top-aligned text using these
		// functions, but it can always be done by creating the renderer with the
		// desired alignment explicitly and should be so rarely needed in practice
		// (without speaking that vertical alignment is completely unsupported in
		// native OS X version), that it's preferable to do the right thing by
		// default here rather than account for it.
		return new ibDataViewColumn(
			label,
			RendererFactory<Renderer>::New(
				mode,
				align & wxALIGN_BOTTOM
				? align
				: align | wxALIGN_CENTRE_VERTICAL
			),
			model_column,
			width,
			align,
			flags
		);
	}

	// Common implementation of all {Append,Prepend}XXXColumn() below.
	template <typename Renderer, typename LabelType>
	ibDataViewColumn*
		AppendColumnWithRenderer(ibDataViewCtrlBase* dvc,
			const LabelType& label,
			unsigned model_column,
			ibDataViewCellMode mode,
			int width,
			wxAlignment align,
			int flags)
	{
		ibDataViewColumn* const
			col = CreateColumnWithRenderer<Renderer>(
				label, model_column, mode, width, align, flags
			);

		dvc->AppendColumn(col);
		return col;
	}

	template <typename Renderer, typename LabelType>
	ibDataViewColumn*
		PrependColumnWithRenderer(ibDataViewCtrlBase* dvc,
			const LabelType& label,
			unsigned model_column,
			ibDataViewCellMode mode,
			int width,
			wxAlignment align,
			int flags)
	{
		ibDataViewColumn* const
			col = CreateColumnWithRenderer<Renderer>(
				label, model_column, mode, width, align, flags
			);

		dvc->PrependColumn(col);
		return col;
	}

} // anonymous namespace

ibDataViewColumn*
ibDataViewCtrlBase::AppendTextColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewTextRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::AppendIconTextColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewIconTextRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::AppendToggleColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewToggleRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::AppendProgressColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewProgressRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::AppendDateColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewDateRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::AppendBitmapColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewBitmapRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::AppendTextColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewTextRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::AppendIconTextColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewIconTextRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::AppendToggleColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewToggleRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::AppendProgressColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewProgressRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::AppendDateColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewDateRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::AppendBitmapColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return AppendColumnWithRenderer<ibDataViewBitmapRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependTextColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewTextRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependIconTextColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewIconTextRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependToggleColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewToggleRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependProgressColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewProgressRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependDateColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewDateRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependBitmapColumn(const wxString& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewBitmapRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependTextColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewTextRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependIconTextColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewIconTextRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependToggleColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewToggleRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependProgressColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewProgressRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependDateColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewDateRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

ibDataViewColumn*
ibDataViewCtrlBase::PrependBitmapColumn(const wxBitmap& label, unsigned int model_column,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	return PrependColumnWithRenderer<ibDataViewBitmapRenderer>(
		this, label, model_column, mode, width, align, flags
	);
}

bool
ibDataViewCtrlBase::AppendColumn(ibDataViewColumn* col)
{
	col->SetOwner((ibDataViewCtrl*)this);
	return true;
}

bool
ibDataViewCtrlBase::PrependColumn(ibDataViewColumn* col)
{
	col->SetOwner((ibDataViewCtrl*)this);
	return true;
}

bool
ibDataViewCtrlBase::InsertColumn(unsigned int WXUNUSED(pos), ibDataViewColumn* col)
{
	col->SetOwner((ibDataViewCtrl*)this);
	return true;
}

void ibDataViewCtrlBase::StartEditor(const ibDataViewItem& item, unsigned int column)
{
	EditItem(item, GetColumn(column));
}

#if wxUSE_DRAG_AND_DROP

/* static */
wxDataObjectComposite*
ibDataViewCtrlBase::CreateDataObject(const wxVector<wxDataFormat>& formats)
{
	if (formats.empty())
	{
		return NULL;
	}

	wxDataObjectComposite* dataObject(new wxDataObjectComposite);
	for (size_t i = 0; i < formats.size(); ++i)
	{
		switch (formats[i].GetType())
		{
		case wxDF_TEXT:
		case wxDF_OEMTEXT:
		case wxDF_UNICODETEXT:
			dataObject->Add(new wxTextDataObject);
			break;

		case wxDF_BITMAP:
		case wxDF_PNG:
			dataObject->Add(new wxBitmapDataObject);
			break;

		case wxDF_FILENAME:
			dataObject->Add(new wxFileDataObject);
			break;

		case wxDF_HTML:
			dataObject->Add(new wxHTMLDataObject);
			break;

		case wxDF_METAFILE:
		case wxDF_SYLK:
		case wxDF_DIF:
		case wxDF_TIFF:
		case wxDF_DIB:
		case wxDF_PALETTE:
		case wxDF_PENDATA:
		case wxDF_RIFF:
		case wxDF_WAVE:
		case wxDF_ENHMETAFILE:
		case wxDF_LOCALE:
		case wxDF_PRIVATE:
		default: // any other custom format
			dataObject->Add(new wxCustomDataObject(formats[i]));
			break;

		case wxDF_INVALID:
		case wxDF_MAX:
			break;
		}
	}

	return dataObject;
}

#endif // wxUSE_DRAG_AND_DROP

// ---------------------------------------------------------
// ibDataViewEvent
// ---------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibDataViewEvent, wxNotifyEvent);

wxDEFINE_EVENT(wxEVT_DATAVIEW_SELECTION_CHANGED, ibDataViewEvent);

wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_ACTIVATED, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_COLLAPSING, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_COLLAPSED, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_EXPANDING, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_EXPANDED, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_START_EDITING, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_EDITING_DONE, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, ibDataViewEvent);

wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_START_INSERTING, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_START_DELETING, ibDataViewEvent);

wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, ibDataViewEvent);

wxDEFINE_EVENT(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_COLUMN_HEADER_RIGHT_CLICK, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_COLUMN_SORTED, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_COLUMN_REORDERED, ibDataViewEvent);

wxDEFINE_EVENT(wxEVT_DATAVIEW_CACHE_HINT, ibDataViewEvent);

wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, ibDataViewEvent);
wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_DROP, ibDataViewEvent);

wxDEFINE_EVENT(wxEVT_DATAVIEW_VIEW_SET, ibDataViewEvent);

// Common part of non-copy ctors.
void ibDataViewEvent::Init(ibDataViewCtrlBase* dvc,
	ibDataViewColumn* column,
	const ibDataViewItem& item)
{
	m_item = item;
	m_col = column ? column->GetModelColumn() : -1;
	m_model = dvc ? dvc->GetModel() : NULL;
	m_column = column;
	m_pos = wxDefaultPosition;
	m_cacheFrom = 0;
	m_cacheTo = 0;
	m_editCancelled = false;
#if wxUSE_DRAG_AND_DROP
	m_dataObject = NULL;
	m_dataBuffer = NULL;
	m_dataSize = 0;
	m_dragFlags = 0;
	m_dropEffect = wxDragNone;
	m_proposedDropIndex = -1;
#endif // wxUSE_DRAG_AND_DROP

	m_viewMode = ibDataViewViewMode::ibDataViewList;

	SetEventObject(dvc);
}

#if wxUSE_DRAG_AND_DROP

void ibDataViewEvent::InitData(wxDataObjectComposite* obj, wxDataFormat format)
{
	SetDataFormat(format);

	SetDataObject(obj->GetObject(format));

	const size_t size = obj->GetDataSize(format);
	SetDataSize(size);

	if (size)
	{
		obj->GetDataHere(format, m_dataBuf.GetWriteBuf(size));
		m_dataBuf.UngetWriteBuf(size);

		SetDataBuffer(m_dataBuf.GetData());
	}
}

#endif // wxUSE_DRAG_AND_DROP

#if wxUSE_SPINCTRL

// -------------------------------------
// ibDataViewSpinRenderer
// -------------------------------------

ibDataViewSpinRenderer::ibDataViewSpinRenderer(int min, int max, ibDataViewCellMode mode, int alignment) :
	ibDataViewCustomRenderer(wxT("long"), mode, alignment)
{
	m_min = min;
	m_max = max;
}

wxWindow* ibDataViewSpinRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
	long l = value;
	wxString str;
	str.Printf(wxT("%d"), (int)l);
	wxSpinCtrl* sc = new wxSpinCtrl(parent, wxID_ANY, str,
		labelRect.GetTopLeft(), labelRect.GetSize(), wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER, m_min, m_max, l);
#ifdef __WXOSX__
	const wxSize size = sc->GetSize();
	wxPoint pt = sc->GetPosition();
	sc->SetSize(pt.x - 4, pt.y - 4, size.x, size.y);
#endif

	return sc;
}

bool ibDataViewSpinRenderer::GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value)
{
	wxSpinCtrl* sc = (wxSpinCtrl*)editor;
	long l = sc->GetValue();
	value = l;
	return true;
}

bool ibDataViewSpinRenderer::Render(wxRect rect, wxDC* dc, int state)
{
	wxString str;
	str.Printf(wxT("%d"), (int)m_data);
	RenderText(str, 0, rect, dc, state);
	return true;
}

wxSize ibDataViewSpinRenderer::GetSize() const
{
	wxSize sz = GetTextExtent(wxString::Format("%d", (int)m_data));

	// Allow some space for the spin buttons, which is approximately the size
	// of a scrollbar (and getting pixel-exact value would be complicated).
	// Also add some whitespace between the text and the button:
	sz.x += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, m_editorCtrl);
	sz.x += GetTextExtent("M").x;

	return sz;
}

bool ibDataViewSpinRenderer::SetValue(const wxVariant& value)
{
	m_data = value.GetLong();
	return true;
}

bool ibDataViewSpinRenderer::GetValue(wxVariant& value) const
{
	value = m_data;
	return true;
}

#if wxUSE_ACCESSIBILITY
wxString ibDataViewSpinRenderer::GetAccessibleDescription() const
{
	return wxString::Format(wxS("%li"), m_data);
}
#endif // wxUSE_ACCESSIBILITY

#endif // wxUSE_SPINCTRL

// -------------------------------------
// ibDataViewChoiceRenderer
// -------------------------------------

ibDataViewChoiceRenderer::ibDataViewChoiceRenderer(const wxArrayString& choices, ibDataViewCellMode mode, int alignment) :
	ibDataViewCustomRenderer(wxT("string"), mode, alignment)
{
	m_choices = choices;
}

wxWindow* ibDataViewChoiceRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
	wxChoice* c = new wxChoice
	(
		parent,
		wxID_ANY,
		labelRect.GetTopLeft(),
		wxSize(labelRect.GetWidth(), -1),
		m_choices
	);
	c->Move(labelRect.GetRight() - c->GetRect().width, wxDefaultCoord);
	c->SetStringSelection(value.GetString());
	return c;
}

bool ibDataViewChoiceRenderer::GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value)
{
	wxChoice* c = (wxChoice*)editor;
	wxString s = c->GetStringSelection();
	value = s;
	return true;
}

bool ibDataViewChoiceRenderer::Render(wxRect rect, wxDC* dc, int state)
{
	RenderText(m_data, 0, rect, dc, state);
	return true;
}

wxSize ibDataViewChoiceRenderer::GetSize() const
{
	wxSize sz;

	for (wxArrayString::const_iterator i = m_choices.begin(); i != m_choices.end(); ++i)
		sz.IncTo(GetTextExtent(*i));

	// Allow some space for the right-side button, which is approximately the
	// size of a scrollbar (and getting pixel-exact value would be complicated).
	// Also add some whitespace between the text and the button:
	sz.x += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, m_editorCtrl);
	sz.x += GetTextExtent("M").x;

	return sz;
}

bool ibDataViewChoiceRenderer::SetValue(const wxVariant& value)
{
	m_data = value.GetString();
	return true;
}

bool ibDataViewChoiceRenderer::GetValue(wxVariant& value) const
{
	value = m_data;
	return true;
}

#if wxUSE_ACCESSIBILITY
wxString ibDataViewChoiceRenderer::GetAccessibleDescription() const
{
	return m_data;
}
#endif // wxUSE_ACCESSIBILITY

// ----------------------------------------------------------------------------
// ibDataViewChoiceByIndexRenderer
// ----------------------------------------------------------------------------

ibDataViewChoiceByIndexRenderer::ibDataViewChoiceByIndexRenderer(const wxArrayString& choices,
	ibDataViewCellMode mode, int alignment) :
	ibDataViewChoiceRenderer(choices, mode, alignment)
{
	m_variantType = wxS("long");
}

wxWindow* ibDataViewChoiceByIndexRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
	wxVariant string_value = value.GetLong() != wxNOT_FOUND ? GetChoice(value.GetLong())
		: wxString();

	return ibDataViewChoiceRenderer::CreateEditorCtrl(parent, labelRect, string_value);
}

bool ibDataViewChoiceByIndexRenderer::GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value)
{
	wxVariant string_value;
	if (!ibDataViewChoiceRenderer::GetValueFromEditorCtrl(editor, string_value))
		return false;

	value = (long)GetChoices().Index(string_value.GetString());
	return true;
}

bool ibDataViewChoiceByIndexRenderer::SetValue(const wxVariant& value)
{
	wxVariant string_value = value.GetLong() != wxNOT_FOUND ? GetChoice(value.GetLong())
		: wxString();
	return ibDataViewChoiceRenderer::SetValue(string_value);
}

bool ibDataViewChoiceByIndexRenderer::GetValue(wxVariant& value) const
{
	wxVariant string_value;
	if (!ibDataViewChoiceRenderer::GetValue(string_value))
		return false;

	value = (long)GetChoices().Index(string_value.GetString());
	return true;
}

#if wxUSE_ACCESSIBILITY
wxString ibDataViewChoiceByIndexRenderer::GetAccessibleDescription() const
{
	wxVariant strVal;
	if (ibDataViewChoiceRenderer::GetValue(strVal))
		return strVal;

	return wxString::Format(wxS("%li"), (long)GetChoices().Index(strVal.GetString()));
}
#endif // wxUSE_ACCESSIBILITY

// ---------------------------------------------------------
// ibDataViewDateRenderer
// ---------------------------------------------------------

#if wxUSE_DATEPICKCTRL

ibDataViewDateRenderer::ibDataViewDateRenderer(const wxString& varianttype,
	ibDataViewCellMode mode, int align)
	: ibDataViewCustomRenderer(varianttype, mode, align)
{
}

wxWindow*
ibDataViewDateRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
	return new wxDatePickerCtrl
	(
		parent,
		wxID_ANY,
		value.GetDateTime(),
		labelRect.GetTopLeft(),
		labelRect.GetSize()
	);
}

bool ibDataViewDateRenderer::GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value)
{
	wxDatePickerCtrl* ctrl = static_cast<wxDatePickerCtrl*>(editor);
	value = ctrl->GetValue();
	return true;
}

bool ibDataViewDateRenderer::SetValue(const wxVariant& value)
{
	m_date = value.GetDateTime();
	return true;
}

bool ibDataViewDateRenderer::GetValue(wxVariant& value) const
{
	value = m_date;
	return true;
}

wxString ibDataViewDateRenderer::FormatDate() const
{
	return m_date.Format(wxGetUIDateFormat());
}

#if wxUSE_ACCESSIBILITY
wxString ibDataViewDateRenderer::GetAccessibleDescription() const
{
	return FormatDate();
}
#endif // wxUSE_ACCESSIBILITY

bool ibDataViewDateRenderer::Render(wxRect cell, wxDC* dc, int state)
{
	wxString tmp = FormatDate();
	RenderText(tmp, 0, cell, dc, state);
	return true;
}

wxSize ibDataViewDateRenderer::GetSize() const
{
	return GetTextExtent(FormatDate());
}

#endif // (defined(wxHAS_GENERIC_DATAVIEWCTRL) 

// ----------------------------------------------------------------------------
// ibDataViewCheckIconTextRenderer implementation
// ----------------------------------------------------------------------------

IMPLEMENT_VARIANT_OBJECT(ibDataViewCheckIconText)

wxIMPLEMENT_CLASS(ibDataViewCheckIconText, ibDataViewIconText);

wxIMPLEMENT_CLASS(ibDataViewCheckIconTextRenderer, ibDataViewRenderer);

ibDataViewCheckIconTextRenderer::ibDataViewCheckIconTextRenderer
(
	ibDataViewCellMode mode,
	int align
)
	: ibDataViewCustomRenderer(GetDefaultType(), mode, align)
{
	m_allow3rdStateForUser = false;
}

void ibDataViewCheckIconTextRenderer::Allow3rdStateForUser(bool allow)
{
	m_allow3rdStateForUser = allow;
}

bool ibDataViewCheckIconTextRenderer::SetValue(const wxVariant& value)
{
	m_value << value;
	return true;
}

bool ibDataViewCheckIconTextRenderer::GetValue(wxVariant& value) const
{
	value << m_value;
	return true;
}

#if wxUSE_ACCESSIBILITY
wxString ibDataViewCheckIconTextRenderer::GetAccessibleDescription() const
{
	wxString text = m_value.GetText();
	if (!text.empty())
	{
		text += wxS(" ");
	}

	switch (m_value.GetCheckedState())
	{
	case wxCHK_CHECKED:
		/* TRANSLATORS: Checkbox state name */
		text += _("checked");
		break;
	case wxCHK_UNCHECKED:
		/* TRANSLATORS: Checkbox state name */
		text += _("unchecked");
		break;
	case wxCHK_UNDETERMINED:
		/* TRANSLATORS: Checkbox state name */
		text += _("undetermined");
		break;
	}

	return text;
}
#endif // wxUSE_ACCESSIBILITY

wxSize ibDataViewCheckIconTextRenderer::GetSize() const
{
	wxSize size = GetCheckSize();
	size.x += MARGIN_CHECK_ICON;

	const wxBitmapBundle& bb = m_value.GetBitmapBundle();
	if (bb.IsOk())
	{
		const wxSize sizeIcon = bb.GetPreferredLogicalSizeFor(GetView());
		if (sizeIcon.y > size.y)
			size.y = sizeIcon.y;

		size.x += sizeIcon.x + MARGIN_ICON_TEXT;
	}

	wxString text = m_value.GetText();
	if (text.empty())
		text = "Dummy";

	const wxSize sizeText = GetTextExtent(text);
	if (sizeText.y > size.y)
		size.y = sizeText.y;

	size.x += sizeText.x;

	return size;
}

bool ibDataViewCheckIconTextRenderer::Render(wxRect cell, wxDC* dc, int state)
{
	/*
	Draw the text first because if the item has a background colour set
	then with wxGTK the entire cell is painted over during RenderText()
	when attributes are applied.
	*/

	const wxSize sizeCheck = GetCheckSize();

	int xoffset = sizeCheck.x + MARGIN_CHECK_ICON;

	wxRect rectIcon;
	const wxBitmapBundle& bb = m_value.GetBitmapBundle();
	const bool drawIcon = bb.IsOk();
	if (drawIcon)
	{
		const wxSize sizeIcon = bb.GetPreferredLogicalSizeFor(GetView());
		rectIcon = wxRect(cell.GetPosition(), sizeIcon);
		rectIcon.x += xoffset;
		rectIcon = rectIcon.CentreIn(cell, wxVERTICAL);

		xoffset += sizeIcon.x + MARGIN_ICON_TEXT;
	}

	RenderText(m_value.GetText(), xoffset, cell, dc, state);

	// Then draw the checkbox.
	int renderFlags = 0;
	switch (m_value.GetCheckedState())
	{
	case wxCHK_UNCHECKED:
		break;

	case wxCHK_CHECKED:
		renderFlags |= wxCONTROL_CHECKED;
		break;

	case wxCHK_UNDETERMINED:
		renderFlags |= wxCONTROL_UNDETERMINED;
		break;
	}

	if (state & wxDATAVIEW_CELL_PRELIT)
		renderFlags |= wxCONTROL_CURRENT;

	wxRect rectCheck(cell.GetPosition(), sizeCheck);
	rectCheck = rectCheck.CentreIn(cell, wxVERTICAL);

	wxRendererNative::Get().DrawCheckBox
	(
		GetView(), *dc, rectCheck, renderFlags
	);

	// Finally draw the icon, if any.
	if (drawIcon)
		dc->DrawIcon(bb.GetIconFor(GetView()), rectIcon.GetPosition());

	return true;
}

bool
ibDataViewCheckIconTextRenderer::ActivateCell(const wxRect& WXUNUSED(cell),
	ibDataViewModel* model,
	const ibDataViewItem& item,
	unsigned int col,
	const wxMouseEvent* mouseEvent)
{
	if (mouseEvent)
	{
		if (!wxRect(GetCheckSize()).Contains(mouseEvent->GetPosition()))
			return false;
	}

	// If the 3rd state is user-settable then the cycle is
	// unchecked->checked->undetermined.
	wxCheckBoxState checkedState = m_value.GetCheckedState();
	switch (checkedState)
	{
	case wxCHK_CHECKED:
		checkedState = m_allow3rdStateForUser ? wxCHK_UNDETERMINED
			: wxCHK_UNCHECKED;
		break;

	case wxCHK_UNDETERMINED:
		// Whether 3rd state is user-settable or not, the next state is
		// unchecked.
		checkedState = wxCHK_UNCHECKED;
		break;

	case wxCHK_UNCHECKED:
		checkedState = wxCHK_CHECKED;
		break;
	}

	m_value.SetCheckedState(checkedState);

	wxVariant value;
	value << m_value;

	model->ChangeValue(value, item, col);
	return true;
}

wxSize ibDataViewCheckIconTextRenderer::GetCheckSize() const
{
	return wxRendererNative::Get().GetCheckBoxSize(GetView());
}

//-----------------------------------------------------------------------------
// ibDataViewListStore
//-----------------------------------------------------------------------------

ibDataViewListStore::ibDataViewListStore()
{
}

ibDataViewListStore::~ibDataViewListStore()
{
	wxVector<ibDataViewListStoreLine*>::iterator it;
	for (it = m_data.begin(); it != m_data.end(); ++it)
	{
		ibDataViewListStoreLine* line = *it;
		delete line;
	}
}

void ibDataViewListStore::PrependColumn(const wxString& varianttype)
{
	m_cols.Insert(varianttype, 0);
}

void ibDataViewListStore::InsertColumn(unsigned int pos, const wxString& varianttype)
{
	m_cols.Insert(varianttype, pos);
}

void ibDataViewListStore::AppendColumn(const wxString& varianttype)
{
	m_cols.Add(varianttype);
}

unsigned int ibDataViewListStore::GetItemCount() const
{
	return m_data.size();
}

void ibDataViewListStore::AppendItem(const wxVector<wxVariant>& values, wxUIntPtr data)
{
	wxCHECK_RET(m_data.empty() || values.size() == m_data[0]->m_values.size(),
		"wrong number of values");

	ibDataViewListStoreLine* line = new ibDataViewListStoreLine(data);
	line->m_values = values;
	m_data.push_back(line);

	RowAppended();
}

void ibDataViewListStore::PrependItem(const wxVector<wxVariant>& values, wxUIntPtr data)
{
	wxCHECK_RET(m_data.empty() || values.size() == m_data[0]->m_values.size(),
		"wrong number of values");

	ibDataViewListStoreLine* line = new ibDataViewListStoreLine(data);
	line->m_values = values;
	m_data.insert(m_data.begin(), line);

	RowPrepended();
}

void ibDataViewListStore::InsertItem(unsigned int row, const wxVector<wxVariant>& values,
	wxUIntPtr data)
{
	wxCHECK_RET(m_data.empty() || values.size() == m_data[0]->m_values.size(),
		"wrong number of values");

	ibDataViewListStoreLine* line = new ibDataViewListStoreLine(data);
	line->m_values = values;
	m_data.insert(m_data.begin() + row, line);

	RowInserted(row);
}

void ibDataViewListStore::DeleteItem(unsigned int row)
{
	wxVector<ibDataViewListStoreLine*>::iterator it = m_data.begin() + row;
	delete* it;
	m_data.erase(it);

	RowDeleted(row);
}

void ibDataViewListStore::DeleteAllItems()
{
	wxVector<ibDataViewListStoreLine*>::iterator it;
	for (it = m_data.begin(); it != m_data.end(); ++it)
	{
		ibDataViewListStoreLine* line = *it;
		delete line;
	}

	m_data.clear();

	Reset(0);
}

void ibDataViewListStore::ClearColumns()
{
	m_cols.clear();
}

void ibDataViewListStore::SetItemData(const ibDataViewItem& item, wxUIntPtr data)
{
	ibDataViewListStoreLine* line = m_data[GetRow(item)];
	if (!line) return;

	line->SetData(data);
}

wxUIntPtr ibDataViewListStore::GetItemData(const ibDataViewItem& item) const
{
	ibDataViewListStoreLine* line = m_data[GetRow(item)];
	if (!line) return 0;

	return line->GetData();
}

void ibDataViewListStore::GetValueByRow(wxVariant& value, unsigned int row, unsigned int col) const
{
	ibDataViewListStoreLine* line = m_data[row];
	value = line->m_values[col];
}

bool ibDataViewListStore::SetValueByRow(const wxVariant& value, unsigned int row, unsigned int col)
{
	ibDataViewListStoreLine* line = m_data[row];
	line->m_values[col] = value;

	return true;
}

//-----------------------------------------------------------------------------
// ibDataViewListCtrl
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibDataViewListCtrl, ibDataViewCtrl);

ibDataViewListCtrl::ibDataViewListCtrl()
{
}

ibDataViewListCtrl::ibDataViewListCtrl(wxWindow* parent, wxWindowID id,
	const wxPoint& pos, const wxSize& size, long style,
	const wxValidator& validator)
{
	Create(parent, id, pos, size, style, validator);
}

ibDataViewListCtrl::~ibDataViewListCtrl()
{
}

bool ibDataViewListCtrl::Create(wxWindow* parent, wxWindowID id,
	const wxPoint& pos, const wxSize& size, long style,
	const wxValidator& validator)
{
	if (!ibDataViewCtrl::Create(parent, id, pos, size, style, validator))
		return false;

	ibDataViewListStore* store = new ibDataViewListStore;
	AssociateModel(store);
	store->DecRef();

	return true;
}

bool ibDataViewListCtrl::AppendColumn(ibDataViewColumn* column, const wxString& varianttype)
{
	GetStore()->AppendColumn(varianttype);
	return ibDataViewCtrl::AppendColumn(column);
}

bool ibDataViewListCtrl::PrependColumn(ibDataViewColumn* column, const wxString& varianttype)
{
	GetStore()->PrependColumn(varianttype);
	return ibDataViewCtrl::PrependColumn(column);
}

bool ibDataViewListCtrl::InsertColumn(unsigned int pos, ibDataViewColumn* column, const wxString& varianttype)
{
	GetStore()->InsertColumn(pos, varianttype);
	return ibDataViewCtrl::InsertColumn(pos, column);
}

bool ibDataViewListCtrl::PrependColumn(ibDataViewColumn* col)
{
	return PrependColumn(col, col->GetRenderer()->GetVariantType());
}

bool ibDataViewListCtrl::InsertColumn(unsigned int pos, ibDataViewColumn* col)
{
	return InsertColumn(pos, col, col->GetRenderer()->GetVariantType());
}

bool ibDataViewListCtrl::AppendColumn(ibDataViewColumn* col)
{
	return AppendColumn(col, col->GetRenderer()->GetVariantType());
}

bool ibDataViewListCtrl::ClearColumns()
{
	GetStore()->ClearColumns();
	return ibDataViewCtrl::ClearColumns();
}

ibDataViewColumn* ibDataViewListCtrl::AppendTextColumn(const wxString& label,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	GetStore()->AppendColumn(wxT("string"));

	ibDataViewColumn* ret = new ibDataViewColumn(label,
		new ibDataViewTextRenderer(wxT("string"), mode),
		GetColumnCount(), width, align, flags);

	ibDataViewCtrl::AppendColumn(ret);

	return ret;
}

ibDataViewColumn* ibDataViewListCtrl::AppendToggleColumn(const wxString& label,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	GetStore()->AppendColumn(wxT("bool"));

	ibDataViewColumn* ret = new ibDataViewColumn(label,
		new ibDataViewToggleRenderer(wxT("bool"), mode),
		GetColumnCount(), width, align, flags);

	return ibDataViewCtrl::AppendColumn(ret) ? ret : NULL;
}

ibDataViewColumn* ibDataViewListCtrl::AppendProgressColumn(const wxString& label,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	GetStore()->AppendColumn(wxT("long"));

	ibDataViewColumn* ret = new ibDataViewColumn(label,
		new ibDataViewProgressRenderer(wxEmptyString, wxT("long"), mode),
		GetColumnCount(), width, align, flags);

	return ibDataViewCtrl::AppendColumn(ret) ? ret : NULL;
}

ibDataViewColumn* ibDataViewListCtrl::AppendIconTextColumn(const wxString& label,
	ibDataViewCellMode mode, int width, wxAlignment align, int flags)
{
	GetStore()->AppendColumn(wxT("ibDataViewIconText"));

	ibDataViewColumn* ret = new ibDataViewColumn(label,
		new ibDataViewIconTextRenderer(wxT("ibDataViewIconText"), mode),
		GetColumnCount(), width, align, flags);

	return ibDataViewCtrl::AppendColumn(ret) ? ret : NULL;
}

//-----------------------------------------------------------------------------
// ibDataViewTreeStore
//-----------------------------------------------------------------------------

ibDataViewTreeStoreNode::ibDataViewTreeStoreNode(
	ibDataViewTreeStoreNode* parent,
	const wxString& text, const wxBitmapBundle& icon, wxClientData* data)
	: m_text(text)
	, m_icon(icon)
{
	m_parent = parent;
	m_data = data;
}

ibDataViewTreeStoreNode::~ibDataViewTreeStoreNode()
{
	delete m_data;
}

ibDataViewTreeStoreContainerNode::ibDataViewTreeStoreContainerNode(
	ibDataViewTreeStoreNode* parent, const wxString& text,
	const wxBitmapBundle& icon, const wxBitmapBundle& expanded, wxClientData* data)
	: ibDataViewTreeStoreNode(parent, text, icon, data)
	, m_iconExpanded(expanded)
{
	m_isExpanded = false;
}

ibDataViewTreeStoreContainerNode::~ibDataViewTreeStoreContainerNode()
{
	DestroyChildren();
}

ibDataViewTreeStoreNodes::iterator
ibDataViewTreeStoreContainerNode::FindChild(ibDataViewTreeStoreNode* node)
{
	ibDataViewTreeStoreNodes::iterator iter;
	for (iter = m_children.begin(); iter != m_children.end(); ++iter)
	{
		if (*iter == node)
			break;
	}

	return iter;
}

void ibDataViewTreeStoreContainerNode::DestroyChildren()
{
	ibDataViewTreeStoreNodes::const_iterator iter;
	for (iter = m_children.begin(); iter != m_children.end(); ++iter)
	{
		delete* iter;
	}

	m_children.clear();
}

//-----------------------------------------------------------------------------

ibDataViewTreeStore::ibDataViewTreeStore()
{
	m_root = new ibDataViewTreeStoreContainerNode(NULL, wxEmptyString);
}

ibDataViewTreeStore::~ibDataViewTreeStore()
{
	delete m_root;
}

ibDataViewItem ibDataViewTreeStore::AppendItem(const ibDataViewItem& parent,
	const wxString& text, const wxBitmapBundle& icon, wxClientData* data)
{
	ibDataViewTreeStoreContainerNode* parent_node = FindContainerNode(parent);
	if (!parent_node) return ibDataViewItem(0);

	ibDataViewTreeStoreNode* node =
		new ibDataViewTreeStoreNode(parent_node, text, icon, data);
	parent_node->GetChildren().push_back(node);

	return node->GetItem();
}

ibDataViewItem ibDataViewTreeStore::PrependItem(const ibDataViewItem& parent,
	const wxString& text, const wxBitmapBundle& icon, wxClientData* data)
{
	ibDataViewTreeStoreContainerNode* parent_node = FindContainerNode(parent);
	if (!parent_node) return ibDataViewItem(0);

	ibDataViewTreeStoreNode* node =
		new ibDataViewTreeStoreNode(parent_node, text, icon, data);
	ibDataViewTreeStoreNodes& children = parent_node->GetChildren();
	children.insert(children.begin(), node);

	return node->GetItem();
}

ibDataViewItem
ibDataViewTreeStore::InsertItem(const ibDataViewItem& parent,
	const ibDataViewItem& previous,
	const wxString& text,
	const wxBitmapBundle& icon,
	wxClientData* data)
{
	ibDataViewTreeStoreContainerNode* parent_node = FindContainerNode(parent);
	if (!parent_node) return ibDataViewItem(0);

	ibDataViewTreeStoreNode* previous_node = FindNode(previous);
	ibDataViewTreeStoreNodes& children = parent_node->GetChildren();
	const ibDataViewTreeStoreNodes::iterator iter = parent_node->FindChild(previous_node);
	if (iter == children.end()) return ibDataViewItem(0);

	ibDataViewTreeStoreNode* node =
		new ibDataViewTreeStoreNode(parent_node, text, icon, data);
	children.insert(iter, node);

	return node->GetItem();
}

ibDataViewItem ibDataViewTreeStore::PrependContainer(const ibDataViewItem& parent,
	const wxString& text, const wxBitmapBundle& icon, const wxBitmapBundle& expanded,
	wxClientData* data)
{
	ibDataViewTreeStoreContainerNode* parent_node = FindContainerNode(parent);
	if (!parent_node) return ibDataViewItem(0);

	ibDataViewTreeStoreContainerNode* node =
		new ibDataViewTreeStoreContainerNode(parent_node, text, icon, expanded, data);
	ibDataViewTreeStoreNodes& children = parent_node->GetChildren();
	children.insert(children.begin(), node);

	return node->GetItem();
}

ibDataViewItem
ibDataViewTreeStore::AppendContainer(const ibDataViewItem& parent,
	const wxString& text,
	const wxBitmapBundle& icon,
	const wxBitmapBundle& expanded,
	wxClientData* data)
{
	ibDataViewTreeStoreContainerNode* parent_node = FindContainerNode(parent);
	if (!parent_node) return ibDataViewItem(0);

	ibDataViewTreeStoreContainerNode* node =
		new ibDataViewTreeStoreContainerNode(parent_node, text, icon, expanded, data);
	parent_node->GetChildren().push_back(node);

	return node->GetItem();
}

ibDataViewItem
ibDataViewTreeStore::InsertContainer(const ibDataViewItem& parent,
	const ibDataViewItem& previous,
	const wxString& text,
	const wxBitmapBundle& icon,
	const wxBitmapBundle& expanded,
	wxClientData* data)
{
	ibDataViewTreeStoreContainerNode* parent_node = FindContainerNode(parent);
	if (!parent_node) return ibDataViewItem(0);

	ibDataViewTreeStoreNode* previous_node = FindNode(previous);
	ibDataViewTreeStoreNodes& children = parent_node->GetChildren();
	const ibDataViewTreeStoreNodes::iterator iter = parent_node->FindChild(previous_node);
	if (iter == children.end()) return ibDataViewItem(0);

	ibDataViewTreeStoreContainerNode* node =
		new ibDataViewTreeStoreContainerNode(parent_node, text, icon, expanded, data);
	children.insert(iter, node);

	return node->GetItem();
}

bool ibDataViewTreeStore::IsContainer(const ibDataViewItem& item) const
{
	ibDataViewTreeStoreNode* node = FindNode(item);
	if (!node) return false;

	return node->IsContainer();
}

ibDataViewItem ibDataViewTreeStore::GetNthChild(const ibDataViewItem& parent, unsigned int pos) const
{
	ibDataViewTreeStoreContainerNode* parent_node = FindContainerNode(parent);
	if (!parent_node) return ibDataViewItem(0);

	ibDataViewTreeStoreNode* const node = parent_node->GetChildren()[pos];
	if (node)
		return node->GetItem();

	return ibDataViewItem(0);
}

int ibDataViewTreeStore::GetChildCount(const ibDataViewItem& parent) const
{
	ibDataViewTreeStoreNode* node = FindNode(parent);
	if (!node) return -1;

	if (!node->IsContainer())
		return 0;

	ibDataViewTreeStoreContainerNode* container_node = (ibDataViewTreeStoreContainerNode*)node;
	return (int)container_node->GetChildren().size();
}

void ibDataViewTreeStore::SetItemText(const ibDataViewItem& item, const wxString& text)
{
	ibDataViewTreeStoreNode* node = FindNode(item);
	if (!node) return;

	node->SetText(text);
}

wxString ibDataViewTreeStore::GetItemText(const ibDataViewItem& item) const
{
	ibDataViewTreeStoreNode* node = FindNode(item);
	if (!node) return wxEmptyString;

	return node->GetText();
}

void ibDataViewTreeStore::SetItemIcon(const ibDataViewItem& item, const wxBitmapBundle& icon)
{
	ibDataViewTreeStoreNode* node = FindNode(item);
	if (!node) return;

	node->SetIcon(icon);
}

wxIcon ibDataViewTreeStore::GetItemIcon(const ibDataViewItem& item) const
{
	ibDataViewTreeStoreNode* node = FindNode(item);
	if (!node) return wxNullIcon;

	return node->GetIcon();
}

void ibDataViewTreeStore::SetItemExpandedIcon(const ibDataViewItem& item, const wxBitmapBundle& icon)
{
	ibDataViewTreeStoreContainerNode* node = FindContainerNode(item);
	if (!node) return;

	node->SetExpandedIcon(icon);
}

wxIcon ibDataViewTreeStore::GetItemExpandedIcon(const ibDataViewItem& item) const
{
	ibDataViewTreeStoreContainerNode* node = FindContainerNode(item);
	if (!node) return wxNullIcon;

	return node->GetExpandedIcon();
}

void ibDataViewTreeStore::SetItemData(const ibDataViewItem& item, wxClientData* data)
{
	ibDataViewTreeStoreNode* node = FindNode(item);
	if (!node) return;

	node->SetData(data);
}

wxClientData* ibDataViewTreeStore::GetItemData(const ibDataViewItem& item) const
{
	ibDataViewTreeStoreNode* node = FindNode(item);
	if (!node) return NULL;

	return node->GetData();
}

void ibDataViewTreeStore::DeleteItem(const ibDataViewItem& item)
{
	if (!item.IsOk()) return;

	ibDataViewItem parent_item = GetParent(item);

	ibDataViewTreeStoreContainerNode* parent_node = FindContainerNode(parent_item);
	if (!parent_node) return;

	const ibDataViewTreeStoreNodes::iterator
		iter = parent_node->FindChild(FindNode(item));
	if (iter != parent_node->GetChildren().end())
	{
		delete* iter;
		parent_node->GetChildren().erase(iter);
	}
}

void ibDataViewTreeStore::DeleteChildren(const ibDataViewItem& item)
{
	ibDataViewTreeStoreContainerNode* node = FindContainerNode(item);
	if (!node) return;

	node->DestroyChildren();
}

void ibDataViewTreeStore::DeleteAllItems()
{
	DeleteChildren(ibDataViewItem(m_root));
}

void
ibDataViewTreeStore::GetValue(wxVariant& variant,
	const ibDataViewItem& item,
	unsigned int WXUNUSED(col)) const
{
	// if (col != 0) return;

	ibDataViewTreeStoreNode* node = FindNode(item);
	if (!node) return;

	wxBitmapBundle bb;
	if (node->IsContainer())
	{
		ibDataViewTreeStoreContainerNode* container = (ibDataViewTreeStoreContainerNode*)node;
		if (container->IsExpanded())
			bb = container->GetExpandedBitmapBundle();
	}

	if (!bb.IsOk())
		bb = node->GetBitmapBundle();

	ibDataViewIconText data(node->GetText(), bb);

	variant << data;
}

bool
ibDataViewTreeStore::SetValue(const wxVariant& variant,
	const ibDataViewItem& item,
	unsigned int WXUNUSED(col))
{
	// if (col != 0) return false;

	ibDataViewTreeStoreNode* node = FindNode(item);
	if (!node) return false;

	ibDataViewIconText data;

	data << variant;

	node->SetText(data.GetText());
	node->SetIcon(data.GetIcon());

	return true;
}

ibDataViewItem ibDataViewTreeStore::GetParent(const ibDataViewItem& item) const
{
	ibDataViewTreeStoreNode* node = FindNode(item);
	if (!node) return ibDataViewItem(0);

	ibDataViewTreeStoreNode* parent = node->GetParent();
	if (!parent) return ibDataViewItem(0);

	if (parent == m_root)
		return ibDataViewItem(0);

	return parent->GetItem();
}

unsigned int ibDataViewTreeStore::GetChildren(const ibDataViewItem& item, ibDataViewItemArray& children) const
{
	ibDataViewTreeStoreContainerNode* node = FindContainerNode(item);
	if (!node) return 0;

	ibDataViewTreeStoreNodes::iterator iter;
	for (iter = node->GetChildren().begin(); iter != node->GetChildren().end(); ++iter)
	{
		ibDataViewTreeStoreNode* child = *iter;
		children.Add(child->GetItem());
	}

	return node->GetChildren().size();
}

int ibDataViewTreeStore::Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
	unsigned int WXUNUSED(column), bool WXUNUSED(ascending)) const
{
	ibDataViewTreeStoreNode* node1 = FindNode(item1);
	ibDataViewTreeStoreNode* node2 = FindNode(item2);

	if (!node1 || !node2 || (node1 == node2))
		return 0;

	ibDataViewTreeStoreContainerNode* const parent =
		(ibDataViewTreeStoreContainerNode*)node1->GetParent();

	wxCHECK_MSG(node2->GetParent() == parent, 0,
		wxS("Comparing items with different parent."));

	if (node1->IsContainer() && !node2->IsContainer())
		return -1;

	if (node2->IsContainer() && !node1->IsContainer())
		return 1;

	ibDataViewTreeStoreNodes::const_iterator iter;
	for (iter = parent->GetChildren().begin(); iter != parent->GetChildren().end(); ++iter)
	{
		if (*iter == node1)
			return -1;

		if (*iter == node2)
			return 1;
	}

	wxFAIL_MSG(wxS("Unreachable"));
	return 0;
}

ibDataViewTreeStoreNode* ibDataViewTreeStore::FindNode(const ibDataViewItem& item) const
{
	if (!item.IsOk())
		return m_root;

	return (ibDataViewTreeStoreNode*)item.GetID();
}

ibDataViewTreeStoreContainerNode* ibDataViewTreeStore::FindContainerNode(const ibDataViewItem& item) const
{
	if (!item.IsOk())
		return (ibDataViewTreeStoreContainerNode*)m_root;

	ibDataViewTreeStoreNode* node = (ibDataViewTreeStoreNode*)item.GetID();

	if (!node->IsContainer())
		return NULL;

	return (ibDataViewTreeStoreContainerNode*)node;
}

//-----------------------------------------------------------------------------
// ibDataViewTreeCtrl
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibDataViewTreeCtrl, ibDataViewCtrl);

wxBEGIN_EVENT_TABLE(ibDataViewTreeCtrl, ibDataViewCtrl)
EVT_DATAVIEW_ITEM_EXPANDED(-1, ibDataViewTreeCtrl::OnExpanded)
EVT_DATAVIEW_ITEM_COLLAPSED(-1, ibDataViewTreeCtrl::OnCollapsed)
EVT_SIZE(ibDataViewTreeCtrl::OnSize)
wxEND_EVENT_TABLE()

bool ibDataViewTreeCtrl::Create(wxWindow* parent, wxWindowID id,
	const wxPoint& pos, const wxSize& size, long style, const wxValidator& validator)
{
	if (!ibDataViewCtrl::Create(parent, id, pos, size, style, validator))
		return false;

	// create the standard model and a column in the tree
	ibDataViewTreeStore* store = new ibDataViewTreeStore;
	AssociateModel(store);
	store->DecRef();

	AppendIconTextColumn
	(
		wxString(),                 // no label (header is not shown anyhow)
		0,                          // the only model column
		wxDATAVIEW_CELL_EDITABLE,
		-1,                         // default width
		wxALIGN_NOT,                //  and alignment
		0                           // not resizable
	);

	return true;
}

ibDataViewItem ibDataViewTreeCtrl::AppendItem(const ibDataViewItem& parent,
	const wxString& text, int iconIndex, wxClientData* data)
{
	ibDataViewItem res = GetStore()->
		AppendItem(parent, text, GetBitmapBundle(iconIndex), data);

	GetStore()->ItemAdded(parent, res);

	return res;
}

ibDataViewItem ibDataViewTreeCtrl::PrependItem(const ibDataViewItem& parent,
	const wxString& text, int iconIndex, wxClientData* data)
{
	ibDataViewItem res = GetStore()->
		PrependItem(parent, text, GetBitmapBundle(iconIndex), data);

	GetStore()->ItemAdded(parent, res);

	return res;
}

ibDataViewItem ibDataViewTreeCtrl::InsertItem(const ibDataViewItem& parent, const ibDataViewItem& previous,
	const wxString& text, int iconIndex, wxClientData* data)
{
	ibDataViewItem res = GetStore()->
		InsertItem(parent, previous, text, GetBitmapBundle(iconIndex), data);

	GetStore()->ItemAdded(parent, res);

	return res;
}

ibDataViewItem ibDataViewTreeCtrl::PrependContainer(const ibDataViewItem& parent,
	const wxString& text, int iconIndex, int expandedIndex, wxClientData* data)
{
	ibDataViewItem res = GetStore()->
		PrependContainer(parent, text,
			GetBitmapBundle(iconIndex), GetBitmapBundle(expandedIndex), data);

	GetStore()->ItemAdded(parent, res);

	return res;
}

ibDataViewItem ibDataViewTreeCtrl::AppendContainer(const ibDataViewItem& parent,
	const wxString& text, int iconIndex, int expandedIndex, wxClientData* data)
{
	ibDataViewItem res = GetStore()->
		AppendContainer(parent, text,
			GetBitmapBundle(iconIndex), GetBitmapBundle(expandedIndex), data);

	GetStore()->ItemAdded(parent, res);

	return res;
}

ibDataViewItem ibDataViewTreeCtrl::InsertContainer(const ibDataViewItem& parent, const ibDataViewItem& previous,
	const wxString& text, int iconIndex, int expandedIndex, wxClientData* data)
{
	ibDataViewItem res = GetStore()->
		InsertContainer(parent, previous, text,
			GetBitmapBundle(iconIndex), GetBitmapBundle(expandedIndex), data);

	GetStore()->ItemAdded(parent, res);

	return res;
}

void ibDataViewTreeCtrl::SetItemText(const ibDataViewItem& item, const wxString& text)
{
	GetStore()->SetItemText(item, text);

	// notify control
	GetStore()->ValueChanged(item, 0);
}

void ibDataViewTreeCtrl::SetItemIcon(const ibDataViewItem& item, const wxBitmapBundle& icon)
{
	GetStore()->SetItemIcon(item, icon);

	// notify control
	GetStore()->ValueChanged(item, 0);
}

void ibDataViewTreeCtrl::SetItemExpandedIcon(const ibDataViewItem& item, const wxBitmapBundle& icon)
{
	GetStore()->SetItemExpandedIcon(item, icon);

	// notify control
	GetStore()->ValueChanged(item, 0);
}

void ibDataViewTreeCtrl::DeleteItem(const ibDataViewItem& item)
{
	ibDataViewItem parent_item = GetStore()->GetParent(item);

	GetStore()->DeleteItem(item);

	// notify control
	GetStore()->ItemDeleted(parent_item, item);
}

void ibDataViewTreeCtrl::DeleteChildren(const ibDataViewItem& item)
{
	ibDataViewTreeStoreContainerNode* node = GetStore()->FindContainerNode(item);
	if (!node) return;

	ibDataViewItemArray array;
	ibDataViewTreeStoreNodes::iterator iter;
	for (iter = node->GetChildren().begin(); iter != node->GetChildren().end(); ++iter)
	{
		ibDataViewTreeStoreNode* child = *iter;
		array.Add(child->GetItem());
	}

	GetStore()->DeleteChildren(item);

	// notify control
	GetStore()->ItemsDeleted(item, array);
}

void  ibDataViewTreeCtrl::DeleteAllItems()
{
	GetStore()->DeleteAllItems();

	GetStore()->Cleared();
}

void ibDataViewTreeCtrl::OnExpanded(ibDataViewEvent& event)
{
	ibDataViewTreeStoreContainerNode* container = GetStore()->FindContainerNode(event.GetItem());
	if (!container) return;

	container->SetExpanded(true);

	GetStore()->ItemChanged(event.GetItem());
}

void ibDataViewTreeCtrl::OnCollapsed(ibDataViewEvent& event)
{
	ibDataViewTreeStoreContainerNode* container = GetStore()->FindContainerNode(event.GetItem());
	if (!container) return;

	container->SetExpanded(false);

	GetStore()->ItemChanged(event.GetItem());
}

void ibDataViewTreeCtrl::OnSize(wxSizeEvent& event)
{
	// automatically resize our only column to take the entire control width
	if (GetColumnCount())
	{
		wxSize size = GetClientSize();
		GetColumn(0)->SetWidth(size.x);
	}

	event.Skip(true);
}

void ibDataViewTreeCtrl::OnImagesChanged()
{
	Refresh();
}

#endif // wxUSE_DATAVIEWCTRL
