////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : the view of a tab
////////////////////////////////////////////////////////////////////////////

#include "visualHostClient.h"
#include "ctrl/frame.h"
#include "commandBar.h"
#include "ctrl/widgets.h"   // ibViewPlaceholder — in the place of a control not drawn yet

#include <wx/wupdlock.h>   // wxWindowUpdateLocker — RAII Freeze/Thaw

#include "frmclient/backend/serialize/dataProtocol.h"   // a control's node read as the engine reads its JSON
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/typeconv.h"
#include "frmclient/win/ctrls/dynamicBorder.h"

namespace {

// How a sizer item lays its child out — the desktop's ibValueSizerItem: the borders it has, and how it stretches.
int FlagOf(const ibProtocolNode& item)
{
	int flag = static_cast<int>(item.GetInt(ibProtocolName::Stretch));
	if (item.GetBool(ibProtocolName::BorderLeft))
		flag |= wxLEFT;
	if (item.GetBool(ibProtocolName::BorderRight))
		flag |= wxRIGHT;
	if (item.GetBool(ibProtocolName::BorderTop))
		flag |= wxUP;
	if (item.GetBool(ibProtocolName::BorderBottom))
		flag |= wxDOWN;
	return flag;
}

// THE VIEW A CALL IS OF — a form by its key; a view that is no form (no key), by the tab it is drawn in.
void SetViewOf(ibProtocolNode& params, const wxString& formKey, long long tabId)
{
	if (formKey.IsEmpty())
		params.SetValue(ibProtocolName::Tab, tabId);
	else
		params.SetValue(ibProtocolName::Form, formKey);
}

// A node less what is in it — what its control draws; the walk goes on to its children.
ibProtocolNode WithoutChildren(const ibProtocolNode& node)
{
	ibProtocolNode bare = node.Clone();
	bare.Remove(ibProtocolName::NodeChildren);
	return bare;
}

// THE NODE A CONTROL'S ReadData READS — the desktop's form node of it: its entries as the server's WriteData wrote them,
// less what is in it (controls of their own) and its State (the runtime's, beside its properties).
ibDataNode DataOf(const ibProtocolNode& node)
{
	// Read as the engine reads its JSON — and then all of it as properties: the server wrote them as its control's
	// properties (WriteData), and the wire keeps fields and properties in one set.
	ibDataNode read;
	ibReadProtocolNode(node, read);

	ibDataNode data;
	for (const auto& [name, value] : read.Fields())
		data.SetProperty(name, value);
	for (const auto& [name, value] : read.Properties()) {
		if (name != ibProtocolName::State)
			data.SetProperty(name, value);
	}
	return data;
}

// …read by the control: its name, then its properties — the desktop's LoadNode, as a control is built or patched — and
// what the server says it is called (a form's, a page's — its Title).
void ReadControl(ibValueFrame* control, const ibProtocolNode& node)
{
	control->SetControlName(node.GetString(ibProtocolName::Name));
	control->ReadData(DataOf(node));

	const ibProtocolNode state = node.FindChild(ibProtocolName::State);
	control->SetControlTitle(state.Has(ibProtocolName::Caption) ? state.GetString(ibProtocolName::Caption)
		: state.GetString(ibProtocolName::Title));
}

// A view as a list of what it is made of: each node's type and id, in order, nested as they are — and what a control
// is BUILT with rather than set afterwards: how a sizer item lays its child out, which way a line runs.
void ShapeOf(const ibProtocolNode& node, std::string& shape)
{
	const wxString type = node.GetType();
	shape += std::string(type.utf8_str());
	shape += '#';
	shape += std::to_string(node.GetId());

	if (type == ibProtocolType::SizerItem) {
		shape += '(' + std::to_string(node.GetInt(ibProtocolName::Proportion)) + '|' + std::to_string(FlagOf(node))
			+ '|' + std::to_string(node.GetInt(ibProtocolName::BorderSize)) + ')';
	}
	else if (type == ibProtocolType::Staticline) {
		shape += '(' + std::to_string(node.GetInt(ibProtocolName::Orient)) + ')';
	}

	shape += '[';
	for (const ibProtocolNode& child : node.Children())
		ShapeOf(child, shape);
	shape += ']';
}

// A patch that changes the view's shape — a control added, taken away or moved: said by the entries that order and
// remove the children with ids, at any control of it (framePatch.cpp). What changes inside a control's own entries — its
// State's lists — is no part of the shape.
bool IsShapeChanged(const ibProtocolNode& patch)
{
	if (patch.Has(ibProtocolName::NodeOrder) || patch.Has(ibProtocolName::NodeRemovedIds)
		|| patch.Has(ibProtocolName::NodeChildrenWhole))
		return true;
	for (const ibProtocolNode& child : patch.Children()) {
		if (IsShapeChanged(child))
			return true;
	}
	return false;
}

// A patch that may move what is laid out — a control's properties set, or what of its State takes room: whether it is
// shown, its caption or title, how it is represented. The rest of a State — a cursor, a value, whether a command may
// be pressed — is drawn where it stands.
bool IsLayoutChanged(const ibProtocolNode& patch)
{
	if (patch.GetChangeCount() > (patch.Has(ibProtocolName::State) ? 1u : 0u))
		return true;

	static const char* const kLayoutState[] = { ibProtocolName::Visible, ibProtocolName::Caption, ibProtocolName::Title,
		ibProtocolName::Representation };
	const ibProtocolNode state = patch.FindChild(ibProtocolName::State);
	const std::vector<ibProtocolNode> removed = state.GetList(ibProtocolName::NodeRemoved);
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

	wxBoxSizer* const childSizer = dynamic_cast<wxBoxSizer*>(control->GetSizer());
	wxBoxSizer* const nextParent = childSizer != nullptr ? childSizer : parentSizer;
	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++)
		CalculateLabels(control->GetChild(idx), nextParent, maxLabelWidth);
}

void ApplyLabels(const ibValueFrame* control, wxBoxSizer* parentSizer, int& maxLabelWidth)
{
	wxBoxSizer* const childSizer = dynamic_cast<wxBoxSizer*>(control->GetSizer());
	ibControlDynamicBorder* const label = dynamic_cast<ibControlDynamicBorder*>(control->GetWindow());

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
	: wxScrolledCanvas(parent, wxID_ANY), m_mainFrame(frame), m_tabId(tabId), m_valueForm(std::make_unique<ibValueForm>(*this))
{
	SetDoubleBuffered(true);
	SetScrollRate(5, 5);
#ifdef __WXOSX__
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	SetBackgroundStyle(wxBG_STYLE_SYSTEM);
#endif
}

ibVisualHostClient::~ibVisualHostClient()
{
	// The windows go while the controls that handle their events are still there.
	DestroyChildren();
}

void ibVisualHostClient::Draw(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	// A view of no type — a text's — is not one drawn here.
	const bool isForm = view.GetType() == ibProtocolType::ClientForm;
	if (!isForm && view.GetType().IsEmpty())
		return;

	m_formKey = view.GetString(ibProtocolName::Key);
	m_valueForm->SetControlID(view.GetInt(ibProtocolName::ControlId));

	// Held still while it is laid out again — and repainted whole when let go; so only when it is laid out again.
	wxWindowUpdateLocker freeze;

	// ⭐ WHAT THE PATCH CHANGED IS DRAWN, NOTHING ELSE — a control whose node in it has entries of its own; the rest is
	// not looked at, nor compared, and laid out again only when what changed takes room. The view is built anew when the
	// patch changes its shape, when it was not built yet, and when there is no patch: a frame sent whole.
	if (patch.IsNode() && !m_shape.empty() && !IsShapeChanged(patch)) {
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
	std::string shape;
	ShapeOf(view, shape);
	if (shape != m_shape) {
		// What the person is typing goes into the control built anew for the same node — none: the field is gone.
		long long editing = 0;
		ibViewEdit edit;
		for (const auto& [id, control] : m_controls) {
			if (control->SaveEdit(edit)) {
				editing = id;
				break;
			}
		}

		Clear();
		Build(view);
		m_shape = shape;

		const auto found = m_controls.find(editing);
		if (found != m_controls.end())
			found->second->RestoreEdit(edit);
		ibJournalInfo(wxT("view"), wxT("form %s built anew: %u controls"), m_formKey,
			static_cast<unsigned>(m_controls.size()));
	}
	else if (isForm) {
		UpdateForm(view);
		for (const ibProtocolNode& child : view.Children())
			UpdateNode(child);
		ibJournalInfo(wxT("view"), wxT("form %s drawn again"), m_formKey);
	}
	else {
		UpdateNode(view);
	}

	AlignLabels();
	UpdateVirtualSize();
}

void ibVisualHostClient::Send(long long controlId, ibProtocolEvent event, const ibProtocolNode& args)
{
	// Raised while the view is torn down — a focus going with a closing tab — it is no one's.
	if (IsBeingDeleted())
		return;

	// The form named by its key, not taken for the active tab's: a field left for another tab commits after the tab
	// has changed.
	ibProtocolNode params;
	params.SetValue(ibProtocolName::Control, controlId)
		.SetValue(ibProtocolName::Event, static_cast<int>(event))
		.SetValue(ibProtocolName::Args, args);
	SetViewOf(params, m_formKey, m_tabId);

	// What it says, as it says it — a sheet handed over whole by its size: written out, it was the sheet again in the
	// journal at every edit.
	const std::string said = args.IsNode() ? args.Write() : std::string("{}");
	ibJournalInfo(wxT("event"), wxT("%d on %lld: %s"), static_cast<int>(event), controlId,
		said.size() <= 1024 ? wxString::FromUTF8(said) : wxString::Format(wxT("%zu bytes"), said.size()));
	m_mainFrame.Post(ibProtocolMethod::Event, params);
}

ibViewFetcher ibVisualHostClient::MakeFetcher(long long controlId) const
{
	// What it fetches is the view's drawn now — held by its key, or its tab: the fetch may come after another tab is drawn.
	ibFrontendMainFrame* const frame = &m_mainFrame;
	const wxString form = m_formKey;
	const long long tab = m_tabId;
	return [frame, controlId, form, tab](const ibProtocolNode& request, ibProtocolNode& answer) {
		ibProtocolNode params;
		params.SetValue(ibProtocolName::Control, controlId)
			.SetValue(ibProtocolName::Request, request);
		SetViewOf(params, form, tab);
		return frame->Fetch(params, answer);
	};
}

void ibVisualHostClient::UpdateForm(const ibProtocolNode& form)
{
	// The form reads what it holds, as its controls do — its editor shows it.
	ReadControl(m_valueForm.get(), form);

	m_content->SetOrientation(static_cast<int>(form.GetInt(ibProtocolName::Orient, wxVERTICAL)));
	m_commandBar->Update(form.FindChild(ibProtocolName::State).FindChild(ibProtocolName::CommandBar));

	// The form's colours — onto its command bar as well (the desktop's UpdateFormLayers).
	const wxString foreground = form.GetString(ibProtocolName::ForegroundColour);
	if (!foreground.IsEmpty()) {
		SetForegroundColour(typeConv::StringToColour(foreground));
		m_commandBar->GetWindow()->SetForegroundColour(GetForegroundColour());
	}
	const wxString background = form.GetString(ibProtocolName::BackgroundColour);
	if (!background.IsEmpty()) {
		SetBackgroundColour(typeConv::StringToColour(background));
		m_commandBar->GetWindow()->SetBackgroundColour(GetBackgroundColour());
	}
}

void ibVisualHostClient::Build(const ibProtocolNode& form)
{
	// The desktop's chrome: the command bar at the top, above the controls (CreateFormLayers).
	m_sizer = new wxBoxSizer(wxVERTICAL);
	m_commandBar = std::make_unique<ibViewCommandBar>(this, [this](const ibProtocolNode& args) {
		Send(m_valueForm->GetControlID(), ibProtocolEvent::Command, args);
	});
	m_sizer->Add(m_commandBar->GetWindow(), 0, wxEXPAND);
	m_content = new wxBoxSizer(wxVERTICAL);
	m_sizer->Add(m_content, 1, wxEXPAND);
	SetSizer(m_sizer);

	// A VIEW THAT IS NO FORM IS ONE CONTROL, filling the tab — with no commands of a form's.
	if (form.GetType() != ibProtocolType::ClientForm) {
		m_commandBar->Update(ibProtocolNode());
		ibProtocolNode item;
		item.SetValue(ibProtocolName::Proportion, 1).SetValue(ibProtocolName::Stretch, static_cast<long long>(wxEXPAND));
		Generate(form, this, m_content, item, nullptr);
		return;
	}

	// The form's look first: the controls built after it take it on.
	UpdateForm(form);

	for (const ibProtocolNode& child : form.Children())
		Generate(child, this, m_content, ibProtocolNode(), nullptr);
}

void ibVisualHostClient::Generate(const ibProtocolNode& node, wxWindow* parent, wxSizer* sizer, const ibProtocolNode& item,
	ibValueFrame* parentControl)
{
	const wxString type = node.GetType();

	// A SIZER ITEM — the desktop's ibValueSizerItem: an object of the form's tree, which draws nothing; it says how its
	// child sits in the sizer, and its child hangs on it, as the server's does.
	ibValueFrame* const parentFrame = parentControl != nullptr ? parentControl : m_valueForm.get();
	if (type == ibProtocolType::SizerItem) {
		ibValueFrame* const holder = ibValueFrame::Make(type, *this, node.GetInt(ibProtocolName::ControlId));
		if (holder != nullptr) {
			ReadControl(holder, node);
			m_controls[node.GetId()] = holder;
			parentFrame->AddChild(holder);
			holder->SetParent(parentFrame);
		}
		for (const ibProtocolNode& child : node.Children())
			Generate(child, parent, sizer, node, holder != nullptr ? holder : parentControl);
		return;
	}

	// A control this client does not draw yet — a placeholder in its place says so; what is in it is not drawn.
	const long long controlId = node.GetInt(ibProtocolName::ControlId);
	ibValueFrame* const made = ibValueFrame::Make(type, *this, controlId);
	const bool drawn = made != nullptr;
	ibValueFrame* const control = drawn ? made : new ibViewPlaceholder(*this, controlId, type);

	m_controls[node.GetId()] = control;
	parentFrame->AddChild(control);
	control->SetParent(parentFrame);

	// What it holds first, as the desktop's form was loaded before it was drawn.
	ReadControl(control, node);
	control->Create(parent, node);

	const int proportion = static_cast<int>(item.GetInt(ibProtocolName::Proportion));
	const int flag = FlagOf(item);
	const int border = static_cast<int>(item.GetInt(ibProtocolName::BorderSize));
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
	if (!drawn)
		return;

	wxWindow* const childParent = control->GetChildParent();
	for (const ibProtocolNode& child : node.Children())
		Generate(child, childParent != nullptr ? childParent : parent, control->GetSizer(), ibProtocolNode(), control);
}

void ibVisualHostClient::UpdateNode(const ibProtocolNode& node)
{
	const auto found = m_controls.find(node.GetId());
	// Not drawn — nor what is in it.
	if (found == m_controls.end())
		return;

	// A node the frame left as it was is not read nor drawn again: each setter of a table's column repaints its header,
	// and a frame drawing every column anew for one changed field flickered. A sizer item draws nothing — it is read.
	ibProtocolNode bare = WithoutChildren(node);
	ibProtocolNode& drawn = m_drawn[node.GetId()];
	if (drawn != bare) {
		ReadControl(found->second, node);
		found->second->Update(node);
		drawn = std::move(bare);
	}

	for (const ibProtocolNode& child : node.Children())
		UpdateNode(child);
}

void ibVisualHostClient::UpdateNode(const ibProtocolNode& node, const ibProtocolNode& patch)
{
	if (patch.GetChangeCount() != 0) {
		if (node.GetType() == ibProtocolType::ClientForm) {
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

void ibVisualHostClient::Clear()
{
	// The windows first, while the controls that handle their events are still there; the sizers with them.
	DestroyChildren();
	SetSizer(nullptr);
	m_sizer = nullptr;
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
	// The scrolling is what the controls need: the scrollbars show only when the form does not fit.
	Layout();
	FitInside();
	Refresh();
}
