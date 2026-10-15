#include "visualHost.h"

#include "core/serialize/dataBuilder.h"
#include "frmserver/visualView/ctrl/form.h"

#include <chrono>

namespace {

// THE VIEW'S DRAWING, TIMED (a debug build's journal): the controls drawn, the time their properties took to write and
// their states to read — where a frame's view spends what it costs, by the walk that draws it.
struct ibViewDrawTimes {
	unsigned int controls = 0;
	long long    propertiesUs = 0;
	long long    statesUs = 0;
};
thread_local ibViewDrawTimes s_drawTimes;

long long NowUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The node of that control in a view drawn — none when it is not drawn there.
ibDataNode* FindDrawnNode(ibDataNode& node, ibFormID id)
{
	for (ibDataNode& child : node.Children()) {
		if (child.GetMetaId() == id)
			return &child;
		if (ibDataNode* const found = FindDrawnNode(child, id))
			return found;
	}
	return nullptr;
}

} // namespace

ibVisualHost::~ibVisualHost() = default;

void ibVisualHost::CreateVisualHost()
{
	m_drawnFrame.reset();
	m_updatedControlArray.clear();
	if (ibValueForm* const form = GetValueForm()) {
		SetCaption(form->GetCaption());   // the title bar
		GenerateControl(form);
	}
}

bool ibVisualHost::UpdateVisualHost(ibDataNode& frame)
{
	ibValueForm* const form = GetValueForm();
	if (form == nullptr)
		return false;
	SetCaption(form->GetCaption());   // the title bar
	s_drawTimes = ibViewDrawTimes();

	// ⭐ ONLY WHAT CHANGED IS DRAWN ANEW — the controls updated since the last frame (UpdateControl), in the frame as
	// drawn then. The whole view the first time, when the form itself is updated, and when an updated control is not
	// in the frame drawn.
	std::set<ibFormID> updatedControlArray;
	updatedControlArray.swap(m_updatedControlArray);
	bool whole = m_drawnFrame == nullptr || updatedControlArray.count(form->GetControlID()) != 0;
	if (!whole) {
		ibDataNode drawn = *m_drawnFrame;
		for (const ibFormID id : updatedControlArray) {
			ibValueFrame* const control = form->FindControlByID(id);
			ibDataNode* const node = control != nullptr ? FindDrawnNode(drawn, id) : nullptr;
			if (node == nullptr) {
				whole = true;
				break;
			}
			*node = ibDataNode(control->GetClassType(), id);
			RefreshControl(control, *node);
		}
		if (!whole)
			frame = std::move(drawn);
	}
	if (whole) {
		frame.SetClsid(form->GetClassType());
		frame.SetMetaId(form->EnsureControlID());
		RefreshControl(form, frame);
	}
	ibJournalInfo(wxT("client"), wxT("      the view %s: %u controls drawn, properties written %lld ms, states %lld ms"),
		whole ? wxT("whole") : wxT("in part"), s_drawTimes.controls, s_drawTimes.propertiesUs / 1000,
		s_drawTimes.statesUs / 1000);

	m_drawnFrame = std::make_unique<ibDataNode>(frame);
	return true;
}

void ibVisualHost::ClearVisualHost()
{
	m_drawnFrame.reset();
	m_updatedControlArray.clear();
	if (ibValueForm* const form = GetValueForm())
		ClearControl(form);
}

// Added or taken off, the next frame draws the form whole: where the others stand is new.
void ibVisualHost::CreateControl(ibValueFrame* control, bool firstCreated)
{
	if (control != nullptr) {
		GenerateControl(control, firstCreated);
		UpdateControl(GetValueForm());
	}
}

void ibVisualHost::UpdateControl(ibValueFrame* control)
{
	if (control != nullptr)
		m_updatedControlArray.insert(control->GetControlID());
}

void ibVisualHost::RemoveControl(ibValueFrame* control)
{
	if (control != nullptr) {
		ClearControl(control);
		UpdateControl(GetValueForm());
	}
}

void ibVisualHost::SelectControl(ibValueFrame* control)
{
	if (control != nullptr)
		control->OnSelected(this);
}

void ibVisualHost::GenerateControl(ibValueFrame* control, bool firstCreated)
{
	control->OnCreate(this);
	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++) {
		if (ibValueFrame* const child = control->GetChild(idx))
			GenerateControl(child, firstCreated);
	}
	control->OnCreated(this, firstCreated);
}

// ⭐ THE FRAME OF A CONTROL AND ITS SUBTREE — SaveNode's shape (the header, the properties as saved, the children
// as nodes of their class) with each control's State beside its properties (OnUpdate): what a client draws from.
// Not saved anywhere — a reading of the form as it stands.
void ibVisualHost::RefreshControl(ibValueFrame* control, ibDataNode& node)
{
	// The id is what an event comes back naming, so a control that never had one gets one now.
	node.SetValue(ibProtocolName::ControlId, control->EnsureControlID());
	node.SetValue(ibProtocolName::Name, control->GetControlName());
	// …and, for the designer, whether it is open in its tree — the header's last field: the form it edits is handed
	// back as drawn and loaded by LoadNode, which reads it.
	if (IsDesignerHost())
		node.SetValue(ibProtocolName::Expanded, control->GetExpanded());
	// A reading, not a save: what a save refuses (an attribute not finished — a dynamic list with no source) is drawn
	// all the same, its refusal is the save's (SaveNode). Stopping here sent the form without its controls (2026-10-15).
	const long long writing = NowUs();
	control->WriteData(node);
	const long long updating = NowUs();
	s_drawTimes.propertiesUs += updating - writing;
	++s_drawTimes.controls;

	ibDataNode& state = node.Child(ibProtocolName::State);
	control->OnUpdate(state, this);
	// The state events the server is subscribed to on it — the client sends these at once, and holds the others.
	std::vector<ibDataValue> subscribed;
	for (int number = 1; number < 32; ++number) {
		const ibProtocolEvent event = static_cast<ibProtocolEvent>(number);
		if (ibProtocolIsStateEvent(event) && control->IsSubscribed(event))
			subscribed.push_back(ibDataValue::Int(number));
	}
	if (!subscribed.empty())
		state.AddField(ibProtocolName::Subscribed, ibDataValue::Array(std::move(subscribed)));
	const long long updated = NowUs() - updating;
	s_drawTimes.statesUs += updated;
	if (updated > 2000)
		ibJournalInfo(wxT("client"), wxT("        %s: its state %lld ms"), control->GetControlName(), updated / 1000);

	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++) {
		ibValueFrame* const child = control->GetChild(idx);
		if (child == nullptr)
			continue;
		ibDataNode& childNode = node.AddChild(child->GetClassType(), child->EnsureControlID());
		RefreshControl(child, childNode);
	}

	control->OnUpdated(state, this);
}

// Children first: a control's cleanup may still reach what it holds.
void ibVisualHost::ClearControl(ibValueFrame* control)
{
	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++) {
		if (ibValueFrame* const child = control->GetChild(idx))
			ClearControl(child);
	}
	control->OnCleanup(this);
}
