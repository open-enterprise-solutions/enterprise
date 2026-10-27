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

// That control's node in the view — its node's child, or one within; none: not drawn there.
std::optional<ibDataNode> FindDrawn(const ibDataNode& node, ibFormID id)
{
	for (const ibDataNode& child : node.Children()) {
		if (child.GetMetaId() == id)
			return child;
		if (auto within = FindDrawn(child, id))
			return within;
	}
	return std::nullopt;
}

// The view drawn again, that control's node drawn now in place of the one there. A node is its bytes: the nodes on the
// way to it are written again, every other one copied as it is.
ibDataNode Redrawn(const ibDataNode& node, ibFormID id, const ibDataNode& now)
{
	ibDataNode redrawn(node.GetClsid(), node.GetMetaId());
	if (node.HasRawData())
		redrawn.SetRawData(node.RawData());
	for (const auto& field : node.Fields())
		redrawn.AddField(field.first, field.second);
	for (const ibDataNode& child : node.Children()) {
		if (child.GetMetaId() == id)
			redrawn.AddChild(now);
		else if (FindDrawn(child, id))
			redrawn.AddChild(Redrawn(child, id, now));
		else
			redrawn.AddChild(child);
	}
	return redrawn;
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
	// drawn then. The whole view the first time, when the form itself is updated, and when an updated control is gone.
	std::set<ibFormID> updatedControlArray;
	updatedControlArray.swap(m_updatedControlArray);
	bool whole = m_drawnFrame == nullptr || updatedControlArray.count(form->GetControlID()) != 0;
	if (!whole) {
		ibDataNode drawn = *m_drawnFrame;
		for (const ibFormID id : updatedControlArray) {
			// An updated one drawn anew, what is in it with it. ADDED SINCE (CreateControl) — not in the frame — the control
			// it is in drawn again over its node as drawn: the new one made in it, the others in it as they were. The whole
			// form was, at every control dropped on it: each control's state read again for one added (a Debug profile,
			// 2026-10-26).
			ibValueFrame* control = form->FindControlByID(id);
			std::optional<ibDataNode> over;
			if (control != nullptr && !FindDrawn(drawn, id)) {
				do
					control = control->GetParent();
				while (control != nullptr && control != form && !(over = FindDrawn(drawn, control->GetControlID())));
				if (control == form)
					over = drawn;
			}
			if (control == nullptr) {
				whole = true;
				break;
			}
			ibDataNode node(control->GetClassType(), control->GetControlID());
			RefreshControl(control, node, over ? *over : ibDataNode());
			drawn = control == form ? std::move(node) : Redrawn(drawn, control->GetControlID(), node);
		}
		if (!whole)
			frame = std::move(drawn);
	}
	if (whole) {
		frame.SetClsid(form->GetClassType());
		frame.SetMetaId(form->EnsureControlID());
		RefreshControl(form, frame);
	}
	ibJournalInfo(ibT("client"), ibT("      the view %s: %u controls drawn, properties written %lld ms, states %lld ms"),
		whole ? ibT("whole") : ibT("in part"), s_drawTimes.controls, s_drawTimes.propertiesUs / 1000,
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

// Added, the next frame draws it in the control it is in — that one's node drawn again, the others in it as they were
// (UpdateVisualHost).
void ibVisualHost::CreateControl(ibValueFrame* control, bool firstCreated)
{
	if (control != nullptr) {
		GenerateControl(control, firstCreated);
		UpdateControl(control);
	}
}

void ibVisualHost::UpdateControl(ibValueFrame* control)
{
	if (control != nullptr)
		m_updatedControlArray.insert(control->GetControlID());
}

// Taken off, the next frame draws the form whole: where the others stand is new.
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
	// None drawn before: each in it read whole.
	RefreshControl(control, node, ibDataNode());
}

void ibVisualHost::RefreshControl(ibValueFrame* control, ibDataNode& node, const ibDataNode& drawn)
{
	// The id is what an event comes back naming, so a control that never had one gets one now.
	node.SetValue(ibProtocol::ibForm::ControlId, control->EnsureControlID());
	node.SetValue(ibProtocol::ibForm::Name, control->GetControlName());
	// …and, for the designer, whether it is open in its tree — the header's last field: the form it edits is handed
	// back as drawn and loaded by LoadNode, which reads it.
	if (IsDesignerHost())
		node.SetValue(ibProtocol::ibForm::Expanded, control->GetExpanded());
	// A reading, not a save: what a save refuses (an attribute not finished — a dynamic list with no source) is drawn
	// all the same, its refusal is the save's (SaveNode). Stopping here sent the form without its controls (2026-10-15).
	const long long writing = NowUs();
	control->WriteData(node);
	const long long updating = NowUs();
	s_drawTimes.propertiesUs += updating - writing;
	++s_drawTimes.controls;

	ibDataNode& state = node.Child(ibProtocol::ibForm::ibState::State);
	control->OnUpdate(state, this);
	// The state events the server is subscribed to on it — the client sends these at once, and holds the others.
	std::vector<ibDataValue> subscribed;
	for (int number = 1; number < 32; ++number) {
		const ibProtocol::ibEvent::ibKind event = static_cast<ibProtocol::ibEvent::ibKind>(number);
		if (ibProtocol::ibEvent::IsStateEvent(event) && control->IsSubscribed(event))
			subscribed.push_back(ibDataValue::Int(number));
	}
	if (!subscribed.empty())
		state.AddField(ibProtocol::ibForm::ibState::Subscribed, ibDataValue::Array(std::move(subscribed)));
	const long long updated = NowUs() - updating;
	s_drawTimes.statesUs += updated;
	if (updated > 2000)
		ibJournalInfo(ibT("client"), ibT("        %s: its state %lld ms"), control->GetControlName(), updated / 1000);

	// What is in it, in its order now — one drawn there copied as it was, one not read whole.
	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++) {
		ibValueFrame* const child = control->GetChild(idx);
		if (child == nullptr)
			continue;
		const ibFormID childId = child->EnsureControlID();
		std::optional<ibDataNode> was;
		for (const ibDataNode& drawnChild : drawn.Children()) {
			if (drawnChild.GetMetaId() == childId) {
				was = drawnChild;
				break;
			}
		}
		if (was) {
			node.AddChild(*was);
			continue;
		}
		ibDataNode& childNode = node.AddChild(child->GetClassType(), childId);
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
