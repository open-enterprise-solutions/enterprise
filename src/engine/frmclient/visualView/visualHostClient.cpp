////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the view of a tab
////////////////////////////////////////////////////////////////////////////

#include "visualHostClient.h"
#include "ctrl/frame.h"
#include "commandBar.h"
#include "ctrl/sizer.h"     // ibValueSizerItem — how a sizer item lays its child out

#include <algorithm>         // std::find — who stays (UpdateChildren)
#include <wx/wupdlock.h>   // wxWindowUpdateLocker — RAII Freeze/Thaw

#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/typeconv.h"
#include "frmclient/win/ctrls/dynamicBorder.h"

namespace {

// THE VIEW A CALL IS OF — a form by its key; a view that is no form (no key), by the tab it is drawn in.
void SetViewOf(ibProtocolNode& params, const wxString& formKey, long long tabId)
{
	if (formKey.IsEmpty())
		params.SetValue(ibProtocol::ibFetch::Tab, tabId);
	else
		params.SetValue(ibProtocol::ibFetch::Form, formKey);
}

// A node less what is in it — what its control draws; the walk goes on to its children. Its entries copied but the
// children's, not the whole of it cut back: each node's subtree was copied at every draw (2026-10-22).
ibProtocolNode WithoutChildren(const ibProtocolNode& node)
{
	ibProtocolNode bare;
	for (const auto& [name, value] : node.Entries()) {
		if (!(name == ibProtocol::ibNode::Children))
			bare.SetValue(name, value);
	}
	return bare;
}

// A control whose orientation is a style it is created with — a line, a slider, a gauge: a new Orient builds it anew
// (ShapeOf keeps it in the shape for the same reason).
bool IsOrientCreated(const wxString& type)
{
	return type == ibProtocol::ibForm::ibType::Staticline || type == ibProtocol::ibForm::ibType::Slider
		|| type == ibProtocol::ibForm::ibType::Gauge;
}

// A view as a list of what it is made of: each node's type and id, in order, nested as they are — and what a control
// is BUILT with rather than set afterwards: how a sizer item lays its child out, which way a line, a slider, a gauge runs.
void ShapeOf(const ibProtocolNode& node, std::string& shape)
{
	const wxString type = node.GetType();
	shape += std::string(type.utf8_str());
	shape += '#';
	shape += std::to_string(node.GetId());

	if (type == ibProtocol::ibForm::ibType::SizerItem) {
		shape += '(' + std::to_string(node.GetInt(ibProtocol::ibForm::ibProperty::Proportion)) + '|' + std::to_string(ibValueSizerItem::GetFlag(node))
			+ '|' + std::to_string(node.GetInt(ibProtocol::ibForm::ibProperty::BorderSize)) + ')';
	}
	else if (IsOrientCreated(type)) {
		shape += '(' + std::to_string(node.GetInt(ibProtocol::ibForm::ibProperty::Orient)) + ')';
	}

	shape += '[';
	for (const ibProtocolNode& child : node.Children())
		ShapeOf(child, shape);
	shape += ']';
}

// A patch that changes the view's shape — a control added, taken away or moved: said by the entries that order and
// remove the children with ids, at any control of it (framePatch.cpp); and a new Orient of a control created with it,
// read from the view's node of the same id. What changes inside a control's own entries — its State's lists — is no
// part of the shape.
bool IsShapeChanged(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (patch.Has(ibProtocol::ibNode::Order) || patch.Has(ibProtocol::ibNode::RemovedIds)
		|| patch.Has(ibProtocol::ibNode::ChildrenWhole))
		return true;
	if (patch.Has(ibProtocol::ibForm::ibProperty::Orient) && IsOrientCreated(view.GetType()))
		return true;
	for (const ibProtocolNode& changed : patch.Children()) {
		for (const ibProtocolNode& child : view.Children()) {
			if (child.GetId() == changed.GetId()) {
				if (IsShapeChanged(child, changed))
					return true;
				break;
			}
		}
	}
	return false;
}

// A patch that may move what is laid out — a control's properties set, or what of its State takes room: whether it is
// shown, its caption or title, how it is represented. The rest of a State — a cursor, a value, whether a command may
// be pressed — is drawn where it stands.
bool IsLayoutChanged(const ibProtocolNode& patch)
{
	if (patch.GetChangeCount() > (patch.Has(ibProtocol::ibForm::ibState::State) ? 1u : 0u))
		return true;

	static const char* const kLayoutState[] = { ibProtocol::ibForm::ibState::Visible, ibProtocol::ibForm::ibState::Caption, ibProtocol::ibForm::ibState::Title,
		ibProtocol::ibForm::ibState::Representation };
	const ibProtocolNode state = patch.FindChild(ibProtocol::ibForm::ibState::State);
	const std::vector<ibProtocolNode> removed = state.GetList(ibProtocol::ibNode::Removed);
	for (const char* const name : kLayoutState) {
		if (state.Has(name))
			return true;
		for (const ibProtocolNode& entry : removed) {
			if (entry.AsString() == name)
				return true;
		}
	}
	for (const ibProtocolNode& child : patch.Children()) {
		if (IsLayoutChanged(child))
			return true;
	}
	return false;
}

// THE LABEL COLUMNS — the desktop's two passes (ibContentWindow::CalculateLabelSize), over the controls drawn. The
// widest label of a vertical run is found, then given to each label of the run, so the fields after them start at
// one x. A horizontal sizer is a row, not a run: it stops the column.
void CalculateLabels(const ibValueFrame* control, wxBoxSizer* parentSizer, int& maxLabelWidth)
{
	const ibControlDynamicBorder* const label = dynamic_cast<const ibControlDynamicBorder*>(control->GetWindow());
	if (label != nullptr && label->AllowCalc()) {
		wxCoord w = 0, h = 0;
		label->CalculateLabelSize(&w, &h);
		if (w > maxLabelWidth)
			maxLabelWidth = w;
	}

	if (parentSizer->GetOrientation() == wxHORIZONTAL)
		return;

	// A PAGE stops it too — a window that lays out what is in it with a sizer of its own (a notebook's page, the one
	// control that is both): its labels are a column of their own, which ApplyLabels measures when it reaches the page.
	// Walked on, every page of a notebook was one column with the form, and the widest caption anywhere set every label.
	if (control->GetWindow() != nullptr && control->GetSizer() != nullptr)
		return;

	wxBoxSizer* const childSizer = dynamic_cast<wxBoxSizer*>(control->GetSizer());
	wxBoxSizer* const nextParent = childSizer != nullptr ? childSizer : parentSizer;
	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++)
		CalculateLabels(control->GetChild(idx), nextParent, maxLabelWidth);
}

void ApplyLabels(const ibValueFrame* control, wxBoxSizer* parentSizer, int& maxLabelWidth)
{
	wxBoxSizer* const childSizer = dynamic_cast<wxBoxSizer*>(control->GetSizer());
	ibControlDynamicBorder* const label = dynamic_cast<ibControlDynamicBorder*>(control->GetWindow());

	// A page starts a column of its own (see CalculateLabels): measured in its sizer, from nothing.
	if (control->GetWindow() != nullptr && childSizer != nullptr) {
		int pageMax = 0;
		if (childSizer->GetOrientation() == wxVERTICAL) {
			for (unsigned int idx = 0; idx < control->GetChildCount(); idx++)
				CalculateLabels(control->GetChild(idx), childSizer, pageMax);
		}
		for (unsigned int idx = 0; idx < control->GetChildCount(); idx++)
			ApplyLabels(control->GetChild(idx), childSizer, pageMax);
		return;
	}

	// What is in it sees a copy of the width: what it changes stays with it.
	int subtreeMax = maxLabelWidth;
	wxBoxSizer* const nextParent = childSizer != nullptr ? childSizer : parentSizer;
	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++)
		ApplyLabels(control->GetChild(idx), nextParent, subtreeMax);

	if (label != nullptr && label->AllowCalc()) {
		label->ApplyLabelSize(maxLabelWidth != wxNOT_FOUND ? wxSize(maxLabelWidth, wxNOT_FOUND) : wxDefaultSize);

		// After a label in a row the column is broken: the next run starts from nothing.
		if (parentSizer->GetOrientation() == wxHORIZONTAL)
			maxLabelWidth = wxNOT_FOUND;
	}
}

} // namespace

//***********************************************************************************
//*                                   viewHost                                      *
//***********************************************************************************

ibVisualHostClient::ibVisualHostClient(ibFrontendMainFrame& frame, wxWindow* parent, long long tabId)
	: wxPanel(parent, wxID_ANY), m_mainFrame(frame), m_tabId(tabId), m_valueForm(std::make_unique<ibValueForm>(*this))
{
	// ⭐ A FRAME AROUND THE WINDOW THE CONTROLS SCROLL IN, that one filling it — the desktop's facade (ibVisualHost): the
	// command bar stands above it (Build) and stays where it is while the controls scroll; it scrolled away with them when
	// this one was the scrolling window (2026-10-22).
	m_contentWindow = new wxScrolledCanvas(this, wxID_ANY);
	m_contentWindow->SetDoubleBuffered(true);
	m_contentWindow->SetScrollRate(5, 5);
#ifdef __WXOSX__
	m_contentWindow->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	m_contentWindow->SetBackgroundStyle(wxBG_STYLE_SYSTEM);
#endif
	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_contentWindow, 1, wxEXPAND);
	SetSizer(sizer);
}

ibVisualHostClient::~ibVisualHostClient()
{
	// The windows go while the controls that handle their events are still there.
	DestroyChildren();
}

void ibVisualHostClient::Draw(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	// A view of no type — a text's — is not one drawn here.
	const bool isForm = view.GetType() == ibProtocol::ibForm::ibType::ClientForm;
	if (!isForm && view.GetType().IsEmpty())
		return;

	const bool sameForm = !m_shape.empty() && m_formKey == view.GetString(ibProtocol::ibForm::Key);
	m_formKey = view.GetString(ibProtocol::ibForm::Key);
	m_valueForm->SetControlID(view.GetInt(ibProtocol::ibForm::ControlId));

	// Held still while it is laid out again — and repainted whole when let go; so only when it is laid out again.
	wxWindowUpdateLocker freeze;

	// ⭐ WHAT THE PATCH CHANGED IS DRAWN, NOTHING ELSE — a control whose node in it has entries of its own; the rest is
	// not looked at, nor compared, and laid out again only when what changed takes room. The view is built anew when the
	// patch changes its shape, when it was not built yet, and when there is no patch: a frame sent whole.
	if (patch.IsNode() && !m_shape.empty() && !IsShapeChanged(view, patch)) {
		const bool layoutChanged = IsLayoutChanged(patch);
		if (layoutChanged)
			freeze.Lock(this);
		UpdateNode(view, patch);
		ibJournalInfo(wxT("view"), wxT("form %s drawn by its patch%s"), m_formKey,
			layoutChanged ? wxT(", laid out") : wxT(""));
		if (layoutChanged) {
			AlignLabels();
			UpdateVirtualSize();
		}
		return;
	}

	freeze.Lock(this);
	// What the person is typing goes into the control made anew for the same node, wherever it is made anew — none: the
	// field is gone. The one typed into is held till then: one made anew is never taken for it.
	long long editing = 0;
	ibValuePtr<ibValueFrame> edited;
	ibViewEdit edit;
	for (const auto& [id, control] : m_controls) {
		if (control->SaveEdit(edit)) {
			editing = id;
			edited = control;
			break;
		}
	}

	// ⭐ THE SAME FORM IS DRAWN IN ONE WALK — what is in each control laid in where it changed, each node drawn where it
	// changed (UpdateChildren); its shape is not written out to be compared, nor the form walked again to draw it (ShapeOf
	// and a second walk were half of the host's draw, 2026-10-22). The form's look first, as Build gives it: what is laid
	// in takes it on. Built anew when none is, another form is drawn, or what is in it cannot be laid in; a view that is no
	// form, when its shape changed.
	bool drawnAgain = false;
	if (sameForm && isForm) {
		UpdateForm(view);
		drawnAgain = UpdateChildren(view, m_valueForm.get(), GetBackgroundWindow(), m_content);
	}
	std::string shape;
	if (!drawnAgain)
		ShapeOf(view, shape);
	if (drawnAgain) {
		ibJournalInfo(wxT("view"), wxT("form %s drawn again: %u controls"), m_formKey,
			static_cast<unsigned>(m_controls.size()));
	}
	else if (!isForm && shape == m_shape) {
		UpdateNode(view);
		const auto found = m_controls.find(view.GetId());
		if (found != m_controls.end() && !found->second->IsPlaceholder()) {
			wxWindow* const childParent = found->second->GetChildParent();
			UpdateChildren(view, found->second, childParent != nullptr ? childParent : GetBackgroundWindow(),
				found->second->GetSizer());
		}
	}
	else {
		Clear();
		Build(view);
		m_shape = shape;
		ibJournalInfo(wxT("view"), wxT("form %s built anew: %u controls"), m_formKey,
			static_cast<unsigned>(m_controls.size()));
	}

	const auto found = m_controls.find(editing);
	if (found != m_controls.end() && found->second != edited)
		found->second->RestoreEdit(edit);

	AlignLabels();
	UpdateVirtualSize();
}

void ibVisualHostClient::Send(long long controlId, ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args)
{
	// Raised while the view is torn down — a focus going with a closing tab — it is no one's; in a designer's — the
	// editor's.
	if (IsBeingDeleted() || IsDesignerHost())
		return;

	// What it says, as it says it — a sheet handed over whole by its size: written out, it was the sheet again in the
	// journal at every edit.
	const ibString said = args.IsNode() ? args.Write() : ibString(wxT("{}"));
	ibJournalInfo(wxT("event"), wxT("%d on %lld: %s"), static_cast<int>(event), controlId,
		said.length() <= 1024 ? said : ibString::Format(wxT("%zu characters"), said.length()));
	m_mainFrame.Post(ibProtocol::ibMethod::Event, EventOf(controlId, event, args));
}

bool ibVisualHostClient::Hold(long long controlId, ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args)
{
	// Raised while the view is torn down — no one's, as for Send; the editor's in a designer's.
	if (IsBeingDeleted() || IsDesignerHost())
		return true;
	if (!m_mainFrame.Hold(EventOf(controlId, event, args)))
		return false;
	// Counted as it is said: a walk through a list is the journal's own measure of what the wire was spared.
	ibJournalInfo(wxT("event"), wxT("%d on %lld held"), static_cast<int>(event), controlId);
	return true;
}

ibProtocolNode ibVisualHostClient::EventOf(long long controlId, ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args) const
{
	// The form named by its key, not taken for the active tab's: a field left for another tab commits after the tab
	// has changed — and a held event is carried after it.
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibEvent::Control, controlId)
		.SetValue(ibProtocol::ibEvent::Event, static_cast<int>(event))
		.SetValue(ibProtocol::ibEvent::Args, args);
	SetViewOf(params, m_formKey, m_tabId);
	return params;
}

ibViewFetcher ibVisualHostClient::MakeFetcher(long long controlId) const
{
	// What it fetches is the view's drawn now — held by its key, or its tab: the fetch may come after another tab is drawn.
	ibFrontendMainFrame* const frame = &m_mainFrame;
	const wxString form = m_formKey;
	const long long tab = m_tabId;
	// …none in a designer's: the editor's tab answers the ask as its own, with the whole form and its classes — some 280 KB,
	// a second on every drawing of a form with a table (2026-10-15). A document's tab in the designer is asked: its sheet
	// comes by Fetch alone (2026-10-20: the designer's whole process refused it, a sheet opened there came empty).
	const bool designer = IsDesignerHost();
	return [frame, controlId, form, tab, designer](const ibProtocolNode& request, ibProtocolNode& answer) {
		if (designer)
			return false;
		ibProtocolNode params;
		params.SetValue(ibProtocol::ibFetch::Control, controlId)
			.SetValue(ibProtocol::ibFetch::Request, request);
		SetViewOf(params, form, tab);
		return frame->Fetch(ibProtocol::ibMethod::Fetch, params, answer);
	};
}

// A NODE READ BY ITS CONTROL — its name, and what the server says it is called (a form's, a page's — its Title); its
// properties are the server's (ibValueFrame).
void ibVisualHostClient::ReadControl(ibValueFrame* control, const ibProtocolNode& node)
{
	control->SetControlName(node.GetString(ibProtocol::ibForm::Name));

	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);
	control->SetControlTitle(state.Has(ibProtocol::ibForm::ibState::Caption) ? state.GetString(ibProtocol::ibForm::ibState::Caption)
		: state.GetString(ibProtocol::ibForm::ibState::Title));
	control->SetSubscribedEvents(state.GetList(ibProtocol::ibForm::ibState::Subscribed));
}

void ibVisualHostClient::UpdateForm(const ibProtocolNode& form)
{
	// The form reads what it holds, as its controls do — its editor shows it.
	ReadControl(m_valueForm.get(), form);

	m_content->SetOrientation(static_cast<int>(form.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxVERTICAL)));
	m_commandBar->Update(form.FindChild(ibProtocol::ibForm::ibState::State).FindChild(ibProtocol::ibForm::ibCommandBar::CommandBar));

	// The form's colours — onto its command bar as well (the desktop's UpdateFormLayers).
	wxWindow* const window = GetBackgroundWindow();
	const wxString foreground = form.GetString(ibProtocol::ibForm::ibProperty::ForegroundColour);
	if (!foreground.IsEmpty()) {
		window->SetForegroundColour(typeConv::StringToColour(foreground));
		m_commandBar->GetWindow()->SetForegroundColour(window->GetForegroundColour());
	}
	const wxString background = form.GetString(ibProtocol::ibForm::ibProperty::BackgroundColour);
	if (!background.IsEmpty()) {
		window->SetBackgroundColour(typeConv::StringToColour(background));
		m_commandBar->GetWindow()->SetBackgroundColour(window->GetBackgroundColour());
	}
}

void ibVisualHostClient::Build(const ibProtocolNode& form)
{
	// The desktop's chrome: the command bar at the top, above the controls (CreateFormLayers) — on this window, above the
	// one they scroll in, so it stays where it is (InitMainSizer); a host that builds them deeper (the designer's card)
	// has it there with them, the whole card scrolling.
	wxWindow* const window = GetBackgroundWindow();
	wxWindow* const barParent = window == m_contentWindow ? this : window;
	m_commandBar = std::make_unique<ibViewCommandBar>(barParent, *this, [this](const ibProtocolNode& args) {
		Send(m_valueForm->GetControlID(), ibProtocol::ibEvent::ibKind::Command, args);
	});
	m_content = new wxBoxSizer(wxVERTICAL);
	if (barParent == this) {
		GetSizer()->Insert(0, m_commandBar->GetWindow(), 0, wxEXPAND);
		window->SetSizer(m_content);
	}
	else {
		wxBoxSizer* const column = new wxBoxSizer(wxVERTICAL);
		column->Add(m_commandBar->GetWindow(), 0, wxEXPAND);
		column->Add(m_content, 1, wxEXPAND);
		window->SetSizer(column);
	}

	// A VIEW THAT IS NO FORM IS ONE CONTROL, filling the tab — with no commands of a form's.
	if (form.GetType() != ibProtocol::ibForm::ibType::ClientForm) {
		m_commandBar->Update(ibProtocolNode());
		ibProtocolNode item;
		item.SetValue(ibProtocol::ibForm::ibProperty::Proportion, 1).SetValue(ibProtocol::ibForm::ibProperty::Stretch, static_cast<long long>(wxEXPAND));
		Generate(form, window, m_content, item, nullptr);
		return;
	}

	// The form's look first: the controls built after it take it on.
	UpdateForm(form);

	for (const ibProtocolNode& child : form.Children())
		Generate(child, window, m_content, ibProtocolNode(), nullptr);
}

void ibVisualHostClient::Generate(const ibProtocolNode& node, wxWindow* parent, wxSizer* sizer, const ibProtocolNode& item,
	ibValueFrame* parentControl)
{
	const wxString type = node.GetType();
	const long long controlId = node.GetInt(ibProtocol::ibForm::ControlId);

	// A SIZER ITEM — the desktop's ibValueSizerItem: an object of the form's tree, which draws nothing; it says how its
	// child sits in the sizer, and its child hangs on it, as the server's does.
	ibValueFrame* const parentFrame = parentControl != nullptr ? parentControl : m_valueForm.get();
	if (type == ibProtocol::ibForm::ibType::SizerItem) {
		ibValueFrame* const holder = ibValueFrame::Make(type, *this, controlId);
		ReadControl(holder, node);
		m_controls[node.GetId()] = holder;
		parentFrame->AddChild(holder);
		holder->SetParent(parentFrame);
		for (const ibProtocolNode& child : node.Children())
			Generate(child, parent, sizer, node, holder);
		return;
	}

	ibValueFrame* const control = ibValueFrame::Make(type, *this, controlId);
	m_controls[node.GetId()] = control;
	parentFrame->AddChild(control);
	control->SetParent(parentFrame);

	// What it holds first, as the desktop's form was loaded before it was drawn.
	ReadControl(control, node);
	control->Create(parent, node);

	const int proportion = static_cast<int>(item.GetInt(ibProtocol::ibForm::ibProperty::Proportion));
	const int flag = ibValueSizerItem::GetFlag(item);
	const int border = static_cast<int>(item.GetInt(ibProtocol::ibForm::ibProperty::BorderSize));
	if (wxWindow* const window = control->GetWindow()) {
		if (sizer != nullptr)
			sizer->Add(window, proportion, flag, border);
	}
	else if (wxSizer* const controlSizer = control->GetSizer()) {
		if (sizer != nullptr)
			sizer->Add(controlSizer, proportion, flag, border);
		else
			parent->SetSizer(controlSizer);
	}

	// Drawn before what is in it, as on the desktop.
	control->Update(node);
	m_drawn[node.GetId()] = WithoutChildren(node);
	// A control this client does not draw yet — a placeholder in its place says so; what is in it is not drawn.
	if (control->IsPlaceholder())
		return;

	wxWindow* const childParent = control->GetChildParent();
	for (const ibProtocolNode& child : node.Children())
		Generate(child, childParent != nullptr ? childParent : parent, control->GetSizer(), ibProtocolNode(), control);
}

void ibVisualHostClient::UpdateNode(const ibProtocolNode& node)
{
	const auto found = m_controls.find(node.GetId());
	// Not drawn.
	if (found == m_controls.end())
		return;

	// A node the frame left as it was is not read nor drawn again: each setter of a table's column repaints its header,
	// and a frame drawing every column anew for one changed field flickered. A sizer item draws nothing — it is read,
	// and sets its child's place in the sizer.
	ibProtocolNode bare = WithoutChildren(node);
	ibProtocolNode& drawn = m_drawn[node.GetId()];
	if (drawn != bare) {
		ReadControl(found->second, node);
		found->second->Update(node);
		drawn = std::move(bare);
	}
}

void ibVisualHostClient::UpdateNode(const ibProtocolNode& node, const ibProtocolNode& patch)
{
	if (patch.GetChangeCount() != 0) {
		if (node.GetType() == ibProtocol::ibForm::ibType::ClientForm) {
			UpdateForm(node);   // the form's own — its look and its command bar
		}
		else {
			const auto found = m_controls.find(node.GetId());
			// Not drawn — nor what is in it.
			if (found == m_controls.end())
				return;
			// What it holds read again, then drawn — a sizer item is only read.
			ReadControl(found->second, node);
			found->second->Update(node, patch);
			m_drawn[node.GetId()] = WithoutChildren(node);
		}
	}

	// Down the patch only: a child it does not name is as it was.
	for (const ibProtocolNode& changed : patch.Children()) {
		for (const ibProtocolNode& child : node.Children()) {
			if (child.GetId() == changed.GetId()) {
				UpdateNode(child, changed);
				break;
			}
		}
	}
}

// ⭐ WHAT IS IN A CONTROL IS LAID IN WHERE IT CHANGED — the rest keep their windows. A form built anew for each control
// added made every window again, and destroyed every one, half of the form editor's draw (2026-10-22). A control added,
// gone, moved among the others or made otherwise is laid in here; one moved here from another control is made anew here,
// and goes from there as that one is walked. Each that stayed is drawn in the same walk (UpdateNode), and gone into in
// turn; one whose children cannot be laid in is made anew in its place. False — none of it can be: a sizer item's child
// sits in the sizer of what holds the item, a notebook puts its pages on itself; made anew a level up, the form built anew.
bool ibVisualHostClient::UpdateChildren(const ibProtocolNode& node, ibValueFrame* control, wxWindow* parent, wxSizer* sizer)
{
	const std::vector<ibProtocolNode> children = node.Children();
	// One place in the sizer for each control in it, in its order.
	const bool placed = sizer != nullptr && control->GetClassName() != ibProtocol::ibForm::ibType::SizerItem
		&& sizer->GetItemCount() == control->GetChildCount();

	// A CHILD GONE — its windows destroyed while its control is still there to handle them, its place in the sizer with
	// them, then the control (the ids drawn under it with it).
	const auto remove = [this, control, sizer](unsigned int idx) {
		ibValueFrame* const gone = control->GetChild(idx);
		const ibValuePtr<ibValueFrame> keep(gone);   // while the ids under it are let go of
		sizer->GetItem(idx)->DeleteWindows();
		sizer->Remove(static_cast<int>(idx));
		for (auto it = m_controls.begin(); it != m_controls.end();) {
			const ibValueFrame* in = it->second;
			while (in != nullptr && in != gone)
				in = in->GetParent();
			if (in != nullptr) {
				m_drawn.erase(it->first);
				it = m_controls.erase(it);
			}
			else {
				++it;
			}
		}
		control->RemoveChild(gone);
		gone->SetParent(nullptr);
	};
	// A CHILD PUT IN ITS NODE'S PLACE — its place in the sizer and among the control's children moved there, and in the
	// tab order before the next window beside it.
	const auto place = [control, sizer](ibValueFrame* child, unsigned int idx) {
		const unsigned int position = control->GetChildPosition(child);
		if (position == idx)
			return;
		wxSizerItemList& items = sizer->GetChildren();
		const wxSizerItemList::compatibility_iterator at = items.Item(position);
		wxSizerItem* const item = at->GetData();
		items.DeleteNode(at);   // the list's node only — the item is the sizer's
		items.Insert(idx, item);
		control->ChangeChildPosition(child, idx);

		wxWindow* const window = item->GetWindow();
		for (size_t next = idx + 1; window != nullptr && next < sizer->GetItemCount(); next++) {
			wxWindow* const before = sizer->GetItem(next)->GetWindow();
			if (before != nullptr && before->GetParent() == window->GetParent()) {
				window->MoveBeforeInTabOrder(before);
				break;
			}
		}
	};
	// A CHILD MADE — generated at the end of the sizer and of the control, put in its place. One that takes no place in
	// the sizer (none to show it) would leave the order to guess: not this.
	const auto make = [this, &children, control, parent, sizer, &place](unsigned int idx) {
		const size_t count = sizer->GetItemCount();
		Generate(children[idx], parent, sizer, ibProtocolNode(), control);
		if (sizer->GetItemCount() != count + 1)
			return false;
		place(control->GetChild(control->GetChildCount() - 1), idx);
		return true;
	};

	// WHO STAYS — drawn in this control, of its node's class, a line's way as it was made (made with its window); in any
	// order. One drawn in another control, of another class, or turned, is made anew.
	std::vector<ibValueFrame*> stays(children.size(), nullptr);
	bool same = children.size() == control->GetChildCount();
	for (unsigned int idx = 0; idx < children.size(); idx++) {
		const ibProtocolNode& child = children[idx];
		const long long id = child.GetId();
		const auto found = m_controls.find(id);
		if (found != m_controls.end() && found->second->GetParent() == control) {
			const wxString type = child.GetType();
			if (found->second->GetClassName() == type && !(IsOrientCreated(type)
				&& m_drawn[id].GetInt(ibProtocol::ibForm::ibProperty::Orient) != child.GetInt(ibProtocol::ibForm::ibProperty::Orient)))
				stays[idx] = found->second;
		}
		if (stays[idx] == nullptr || !same || control->GetChild(idx) != stays[idx])
			same = false;
	}

	if (!same) {
		if (!placed)
			return false;
		// THE GONE first — each drawn here that does not stay; then each in its node's place, the new made.
		for (unsigned int idx = control->GetChildCount(); idx-- > 0;) {
			if (std::find(stays.begin(), stays.end(), control->GetChild(idx)) == stays.end())
				remove(idx);
		}
		for (unsigned int idx = 0; idx < children.size(); idx++) {
			if (stays[idx] != nullptr)
				place(stays[idx], idx);
			else if (!make(idx))
				return false;
		}
		// A page's sizer is not laid out with the form's.
		if (wxWindow* const window = sizer->GetContainingWindow())
			window->Layout();
	}

	// EACH THAT STAYED — its node drawn where it changed (a sizer item's place with it), then what is in it, where Generate
	// made it: a sizer item's child in the window and the sizer the item is in, a control's in those the control gives.
	for (unsigned int idx = 0; idx < children.size(); idx++) {
		ibValueFrame* const drawn = stays[idx];
		if (drawn == nullptr)
			continue;
		const ibProtocolNode& child = children[idx];
		UpdateNode(child);
		if (drawn->IsPlaceholder())
			continue;
		const bool sizerItem = drawn->GetClassName() == ibProtocol::ibForm::ibType::SizerItem;
		wxWindow* const childParent = sizerItem ? nullptr : drawn->GetChildParent();
		if (UpdateChildren(child, drawn, childParent != nullptr ? childParent : parent, sizerItem ? sizer : drawn->GetSizer()))
			continue;
		// …made anew in its place, what is in it with it.
		if (!placed)
			return false;
		remove(idx);
		if (!make(idx))
			return false;
	}
	return true;
}

void ibVisualHostClient::Clear()
{
	// The windows first, while the controls that handle their events are still there; the sizers with them.
	wxWindow* const window = GetBackgroundWindow();
	window->DestroyChildren();
	window->SetSizer(nullptr);
	// …and the command bar where it stands on this one, above them (Build).
	if (m_commandBar != nullptr && window == m_contentWindow)
		m_commandBar->GetWindow()->Destroy();
	m_content = nullptr;
	m_commandBar.reset();

	// THE FORM STAYS — only its controls are drawn anew, as the desktop's form outlived its own redraw: what holds it (the
	// form editor, across an Apply) holds it still.
	m_valueForm->ClearChildren();
	m_controls.clear();
	m_drawn.clear();
	m_shape.clear();
}

ibValueFrame* ibVisualHostClient::GetObjectBase(const wxObject* wxobject) const
{
	for (const auto& [id, control] : m_controls) {
		if (control->GetWindow() == wxobject)
			return control;
	}
	return nullptr;
}

void ibVisualHostClient::AlignLabels()
{
	if (m_content == nullptr)
		return;

	int maxLabelWidth = 0;
	if (m_content->GetOrientation() == wxVERTICAL) {
		for (unsigned int idx = 0; idx < m_valueForm->GetChildCount(); idx++)
			CalculateLabels(m_valueForm->GetChild(idx), m_content, maxLabelWidth);
	}

	for (unsigned int idx = 0; idx < m_valueForm->GetChildCount(); idx++)
		ApplyLabels(m_valueForm->GetChild(idx), m_content, maxLabelWidth);
}

void ibVisualHostClient::UpdateVirtualSize()
{
	// The scrolling is what the controls need: the scrollbars show only when the form does not fit — the command bar
	// above, laid out with the window they scroll in.
	Layout();
	m_contentWindow->Layout();
	m_contentWindow->FitInside();
	m_contentWindow->Refresh();
}
