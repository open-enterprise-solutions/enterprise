////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the view of a tab
////////////////////////////////////////////////////////////////////////////

#include "visualHostClient.h"
#include "ctrl/frame.h"
#include "commandBar.h"
#include "ctrl/layout.h"    // ibViewUpdateSizer — a control's place in the sizer above, as its node says

#include <algorithm>         // std::find, std::all_of — who stays (UpdateChildren)
#include <span>              // what a control is built with (BuiltWith)
#include <variant>           // std::get — a style's value (UpdateForm)
#include <wx/settings.h>     // wxSystemSettings::GetAppearance — the look in force (UpdateForm)
#include <wx/wupdlock.h>   // wxWindowUpdateLocker — RAII Freeze/Thaw

#include "frmclient/mainFrame/mainFrame.h"
#include "core/style/style.h"   // ibStyle::Platform — the form's ground in a look (UpdateForm)
#include "deskgui/typeconv.h"
#include "deskgui/univ/ctrl/control.h"   // ibControl — a title beside it, lined up into a column

namespace {

// THE VIEW A CALL IS OF — a form by its key; a view that is no form (no key), by the tab it is drawn in.
void SetViewOf(ibProtocolNode& params, const wxString& formKey, long long tabId)
{
	if (formKey.IsEmpty())
		params.SetValue(ibProtocol::ibFetch::Tab, tabId);
	else
		params.SetValue(ibProtocol::ibFetch::Form, formKey);
}

// A node less what is in it — what its control draws; the walk goes on to its children. Its bytes as they lie, its
// children's cut off: its entries set one by one, a handle made for each, were a quarter of the view's draw at every
// control dropped (a Debug profile, 2026-10-26).
ibProtocolNode WithoutChildren(const ibProtocolNode& node)
{
	const ibDataNode* const read = node.GetBody();
	if (read == nullptr)
		return ibProtocolNode();
	ibDataNode bare = *read;
	bare.RemoveChildren();
	return ibProtocolNode(bare);
}

// The properties a control of the type is created with rather than set afterwards — the view a field is drawn as (its
// widget), a layout lays out by (its sizer), a column's cells are drawn as (its renderer), a static shows (its widget),
// and the orientation a static's line takes as a style: a new value builds it anew (ShapeOf keeps them in the shape for
// the same reason). None — empty.
std::span<const char* const> BuiltWith(const wxString& type)
{
	static constexpr const char* s_view[]   = { ibProtocol::ibForm::ibProperty::View };
	static constexpr const char* s_static[] = { ibProtocol::ibForm::ibProperty::View, ibProtocol::ibForm::ibProperty::Orient };
	if (type == ibProtocol::ibForm::ibType::Static)
		return s_static;
	if (type == ibProtocol::ibForm::ibType::Field || type == ibProtocol::ibForm::ibType::Layout
		|| type == ibProtocol::ibForm::ibType::TableColumn)
		return s_view;
	return {};
}

// A view as a list of what it is made of: each node's type and id, in order, nested as they are — and what a control
// is BUILT with rather than set afterwards: which way a line runs, what a field is drawn as.
void ShapeOf(const ibProtocolNode& node, std::string& shape)
{
	const wxString type = node.GetType();
	shape += std::string(type.utf8_str());
	shape += '#';
	shape += std::to_string(node.GetId());

	for (const char* const built : BuiltWith(type))
		shape += '(' + std::to_string(node.GetInt(built)) + ')';

	shape += '[';
	for (const ibProtocolNode& child : node.Children())
		ShapeOf(child, shape);
	shape += ']';
}

// A patch that changes the view's shape — a control added, taken away or moved: said by the entries that order and
// remove the children with ids, at any control of it (framePatch.cpp); and a new value of what a control is created
// with (BuiltWith), read from the view's node of the same id. What changes inside a control's own entries — its State's
// lists — is no part of the shape.
bool IsShapeChanged(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (patch.Has(ibProtocol::ibNode::Order) || patch.Has(ibProtocol::ibNode::RemovedIds)
		|| patch.Has(ibProtocol::ibNode::ChildrenWhole))
		return true;
	for (const char* const built : BuiltWith(view.GetType())) {
		if (patch.Has(built))
			return true;
	}
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
	const ibControl* const label = dynamic_cast<const ibControl*>(control->GetWindow());
	if (label != nullptr && !label->GetTitle().empty() && label->GetTitlePosition() == wxLEFT) {
		const wxCoord w = label->GetTitleExtent().x;
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
	ibControl* const label = dynamic_cast<ibControl*>(control->GetWindow());

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

	if (label != nullptr && !label->GetTitle().empty() && label->GetTitlePosition() == wxLEFT) {
		label->SetTitleWidth(maxLabelWidth != wxNOT_FOUND ? maxLabelWidth : wxDefaultCoord);

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
	: ibPanel(parent, wxID_ANY), m_mainFrame(frame), m_tabId(tabId), m_valueForm(std::make_unique<ibValueForm>(*this))
{
	// ⭐ A FRAME AROUND THE WINDOW THE CONTROLS SCROLL IN, that one filling it — the desktop's facade (ibVisualHost): the
	// command bar stands above it (Build) and stays where it is while the controls scroll; it scrolled away with them when
	// this one was the scrolling window (2026-10-22). A panel's (wxNavigationEnabled): Tab goes through the controls in it.
	// The theme's panel scrolled, not wx's: painted in the theme's colour where the form's controls do not cover it (a
	// colour the form gives it it keeps, GetBackgroundWindow), on every port - wx's erased itself in the system's grey,
	// and growing in a resize before its controls did it showed that grey round them (2026-11-11, read off the window's
	// surface during a step; Max: "artefacts even without the spreadsheet")
	m_contentWindow = new ibScrolled<ibPanel>(this, wxID_ANY);
	m_contentWindow->SetDoubleBuffered(true);
	m_contentWindow->SetScrollRate(5, 5);
	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_contentWindow, 1, wxEXPAND);
	SetSizer(sizer);

	// ⭐ A FORM REPAINTED IS A FORM UPDATED WHOLE (Max, 2026-11-24) — the look changed: its windows took the colours in
	// force as they were made, and what is held of each control is its node without the State (m_drawn), drawn again from
	// which a command bar comes out empty and a table reads its rows anew. Its shape forgotten, the next draw builds it
	// anew from the view (Draw): the shown tab's at once (ibFrontendMainFrame::OnSysColourChanged), the studio's form
	// at once (its editor's), another tab's when it comes up.
	Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
		m_shape.clear();
		event.Skip();
	});
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
		drawnAgain = UpdateChildren(view, patch, m_valueForm.get(), GetBackgroundWindow(), m_content);
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
			UpdateChildren(view, patch, found->second, childParent != nullptr ? childParent : GetBackgroundWindow(),
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
	// Raised while the view is torn down — a focus going with a closing tab — it is no one's; in a studio's — the
	// editor's.
	if (IsBeingDeleted() || IsStudioHost())
		return;

	// What it says, as it says it — a sheet handed over whole by its size: written out, it was the sheet again in the
	// journal at every edit. ⚠ AND THE SIZE IS THE NODE'S, NOT THE TEXT'S: written out only to be counted, the sheet of
	// an edit in a 100 000-line report was 19 % of it (2026-11-16, load-million-rows.md § 12). Asked inside the
	// journal's call, so a build without the journal works none of it out.
	ibJournalInfo(wxT("event"), wxT("%d on %lld: %s"), static_cast<int>(event), controlId,
		args.GetBody() == nullptr ? ibString(wxT("{}"))
		: args.GetBody()->Weight() <= 1024 ? args.Write()
		: ibString::Format(wxT("<%zu bytes>"), args.GetBody()->Weight()));
	m_mainFrame.Post(ibProtocol::ibMethod::Event, EventOf(controlId, event, args));
}

bool ibVisualHostClient::Hold(long long controlId, ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args)
{
	// Raised while the view is torn down — no one's, as for Send; the editor's in a studio's.
	if (IsBeingDeleted() || IsStudioHost())
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
	// …none in a studio's: the editor's tab answers the ask as its own, with the whole form and its classes — some 280 KB,
	// a second on every drawing of a form with a table (2026-10-15). A document's tab in the studio is asked: its sheet
	// comes by Fetch alone (2026-10-20: the studio's whole process refused it, a sheet opened there came empty).
	const bool studio = IsStudioHost();
	return [frame, controlId, form, tab, studio](const ibProtocolNode& request, ibProtocolNode& answer) {
		if (studio)
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

	// The form's colours as a control's are (the platform's own the theme's, in the look in force), its kind's ground the
	// style's form background — onto its command bar as well (the desktop's UpdateFormLayers).
	const auto ground = [](bool dark) {
		return typeConv::ColourToWxColour(std::get<ibColour>(ibStyle::Platform().Get(ibStyleItem::FormBackground, dark)));
	};
	const wxColour platform = ground(false), themed = ground(wxSystemSettings::GetAppearance().IsDark());
	ibValueFrame::ApplyLook(GetBackgroundWindow(), form, platform, themed);
	ibValueFrame::ApplyLook(m_commandBar->GetWindow(), form, platform, themed);
}

void ibVisualHostClient::Build(const ibProtocolNode& form)
{
	// The desktop's chrome: the command bar at the top, above the controls (CreateFormLayers) — on this window, above the
	// one they scroll in, so it stays where it is (InitMainSizer); a host that builds them deeper (the studio's card)
	// has it there with them, the whole card scrolling.
	wxWindow* const window = GetBackgroundWindow();
	wxWindow* const barParent = window == m_contentWindow ? this : window;
	m_commandBar = std::make_unique<ibViewCommandBar>(barParent, *this, [this](ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args) {
		Send(m_valueForm->GetControlID(), event, args);
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

	// A VIEW THAT IS NO FORM IS ONE CONTROL, filling the tab, whatever place its node says (UpdateNode leaves it so) — with
	// no commands of a form's.
	if (form.GetType() != ibProtocol::ibForm::ibType::ClientForm) {
		m_commandBar->Update(ibProtocolNode());
		Generate(form, window, m_content, nullptr);
		ibProtocolNode place;
		place.SetValue(ibProtocol::ibForm::ibProperty::Proportion, 1).SetValue(ibProtocol::ibForm::ibProperty::Stretch, static_cast<long long>(wxEXPAND));
		if (m_content->GetItemCount() != 0)
			ibViewUpdateSizer(m_content->GetItem(static_cast<size_t>(0)), place);
		return;
	}

	// The form's look first: the controls built after it take it on.
	UpdateForm(form);

	for (const ibProtocolNode& child : form.Children())
		Generate(child, window, m_content, nullptr);
}

void ibVisualHostClient::Generate(const ibProtocolNode& node, wxWindow* parent, wxSizer* sizer, ibValueFrame* parentControl)
{
	const wxString type = node.GetType();
	const long long controlId = node.GetInt(ibProtocol::ibForm::ControlId);

	ibValueFrame* const parentFrame = parentControl != nullptr ? parentControl : m_valueForm.get();
	ibValueFrame* const control = ibValueFrame::Make(type, *this, controlId);
	m_controls[node.GetId()] = control;
	parentFrame->AddChild(control);
	control->SetParent(parentFrame);

	// What it holds first, as the desktop's form was loaded before it was drawn.
	ReadControl(control, node);
	control->Create(parent, node);

	// …in the sizer it sits in, at the place its own node says (the server's ibSizerItem).
	if (wxWindow* const window = control->GetWindow()) {
		if (sizer != nullptr)
			ibViewUpdateSizer(sizer->Add(window), node);
	}
	else if (wxSizer* const controlSizer = control->GetSizer()) {
		if (sizer != nullptr)
			ibViewUpdateSizer(sizer->Add(controlSizer), node);
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
		Generate(child, childParent != nullptr ? childParent : parent, control->GetSizer(), control);
}

void ibVisualHostClient::UpdateNode(const ibProtocolNode& node)
{
	const auto found = m_controls.find(node.GetId());
	// Not drawn.
	if (found == m_controls.end())
		return;

	// A node the frame left as it was is not read nor drawn again: each setter of a table's column repaints its header,
	// and a frame drawing every column anew for one changed field flickered.
	ibProtocolNode bare = WithoutChildren(node);
	ibProtocolNode& drawn = m_drawn[node.GetId()];
	if (drawn != bare) {
		ReadControl(found->second, node);
		found->second->Update(node);
		// …and its place in the sizer above, as its node says — a view that is no form fills its tab (Build).
		if (found->second->GetControlID() != m_valueForm->GetControlID())
			ibViewUpdateSizer(found->second->GetSizerItem(), node);
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
			// What it holds read again, then drawn, and placed — a view that is no form fills its tab (Build).
			ReadControl(found->second, node);
			found->second->Update(node, patch);
			if (found->second->GetControlID() != m_valueForm->GetControlID())
				ibViewUpdateSizer(found->second->GetSizerItem(), node);
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
// turn; one whose children cannot be laid in is made anew in its place. False — none of it can be: a notebook puts its
// pages on itself; made anew a level up, the form built anew.
bool ibVisualHostClient::UpdateChildren(const ibProtocolNode& node, const ibProtocolNode& patch, ibValueFrame* control,
	wxWindow* parent, wxSizer* sizer)
{
	const std::vector<ibProtocolNode> children = node.Children();
	const std::vector<ibProtocolNode> changes = patch.IsNode() ? patch.Children() : std::vector<ibProtocolNode>();
	// One place in the sizer for each control in it, in its order.
	const bool placed = sizer != nullptr && sizer->GetItemCount() == control->GetChildCount();

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
		Generate(children[idx], parent, sizer, control);
		if (sizer->GetItemCount() != count + 1)
			return false;
		place(control->GetChild(control->GetChildCount() - 1), idx);
		return true;
	};

	// WHO STAYS — drawn in this control, of its node's class, what it is built with as it was made (a line's way, a
	// field's view); in any order. One drawn in another control, of another class, or turned, is made anew.
	std::vector<ibValueFrame*> stays(children.size(), nullptr);
	bool same = children.size() == control->GetChildCount();
	for (unsigned int idx = 0; idx < children.size(); idx++) {
		const ibProtocolNode& child = children[idx];
		const long long id = child.GetId();
		const auto found = m_controls.find(id);
		if (found != m_controls.end() && found->second->GetParent() == control) {
			const wxString type = child.GetType();
			const ibProtocolNode& drawn = m_drawn[id];
			const std::span<const char* const> built = BuiltWith(type);
			if (found->second->GetClassName() == type && std::all_of(built.begin(), built.end(),
				[&](const char* property) { return drawn.GetInt(property) == child.GetInt(property); }))
				stays[idx] = found->second;
		}
		if (stays[idx] == nullptr || !same || control->GetChild(idx) != stays[idx])
			same = false;
	}

	// Whether a window here was laid in — the sizer laid out once, at the end.
	bool laidIn = false;
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
		laidIn = true;
	}

	// EACH THAT STAYED — its node drawn where it changed (its place in the sizer with it), then what is in it, where
	// Generate made it: in the window and the sizer the control gives.
	// ⭐ BY THE PATCH, when there is one: one it does not name is as it was, and what is in it too — not copied nor compared;
	// one whose shape it keeps is drawn down it (UpdateNode). Only those on the way to a control added, gone or moved are
	// compared with what was drawn — each control was, at every control dropped on a form (a Debug profile, 2026-10-26).
	for (unsigned int idx = 0; idx < children.size(); idx++) {
		ibValueFrame* const drawn = stays[idx];
		if (drawn == nullptr)
			continue;
		const ibProtocolNode& child = children[idx];
		// No patch — none down it either. A node made here is a node of no entries, not none: handed down as a patch it
		// skipped every control under this one, and a column dropped on a table was drawn only when the form was opened
		// again (2026-11-02).
		ibProtocolNode changed = patch;
		if (patch.IsNode()) {
			const auto named = std::find_if(changes.begin(), changes.end(),
				[&child](const ibProtocolNode& change) { return change.GetId() == child.GetId(); });
			if (named == changes.end())
				continue;
			if (!IsShapeChanged(child, *named)) {
				UpdateNode(child, *named);
				continue;
			}
			changed = *named;
		}
		UpdateNode(child);
		if (drawn->IsPlaceholder())
			continue;
		wxWindow* const childParent = drawn->GetChildParent();
		if (UpdateChildren(child, changed, drawn, childParent != nullptr ? childParent : parent, drawn->GetSizer()))
			continue;
		// …made anew in its place, what is in it with it.
		if (!placed)
			return false;
		remove(idx);
		if (!make(idx))
			return false;
		laidIn = true;
	}

	// ⭐ A PAGE'S SIZER IS NOT LAID OUT WITH THE FORM'S — a window laid in here, or made anew in its place, is laid out by
	// the window the sizer is in. Laid out for the first alone, a table made anew on a page for a column dropped on it stood
	// in the page's corner at its own size, the one beside it where it had been (2026-11-24, Max: "it won't stretch inside").
	if (laidIn) {
		if (wxWindow* const window = sizer->GetContainingWindow())
			window->Layout();
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

ibValueFrame* ibVisualHostClient::GetObjectBase(const wxObject* ibobject) const
{
	for (const auto& [id, control] : m_controls) {
		if (control->GetWindow() == ibobject)
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
