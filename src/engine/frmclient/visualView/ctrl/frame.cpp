////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a control of a view
////////////////////////////////////////////////////////////////////////////

#include "frame.h"
#include "frmclient/visualView/visualHostClient.h"

#include <map>
#include <variant>
#include <wx/settings.h>   // wxSystemSettings::GetAppearance — the look in force (ApplyLook)
#include <wx/sizer.h>   // wxSizer::GetItem — the place a control stands in (GetSizerItem)

#include "widgets.h"   // ibViewPlaceholder — a type none is registered for

#include "core/stringUtils.h"   // stringUtils::IsSameAs — a name found as the desktop's finds it
#include "core/style/style.h"   // ibStyle::Platform — the platform's own colours, and the theme's for them (ApplyLook)
#include "frmclient/backend/propertyManager/property/propertySerializable.h"   // its properties — a row's each
#include "deskgui/typeconv.h"
#include "protocol/protocolStudio.h"   // ibProtocol::ibSchema::ibProperties

namespace {

// A node's type → how its control is made (CONTROL_REGISTER) — made at its first use: the sources register as their
// module is loaded, in whatever order they come.
std::map<wxString, ibValueFrame* (*)(ibVisualHostClient&, long long)>& Types()
{
	static std::map<wxString, ibValueFrame* (*)(ibVisualHostClient&, long long)> types;
	return types;
}

// A colour of the platform style, in a look (deskgui's theme asks it the same way, lunaTheme.cpp).
wxColour StyleColour(ibStyleItem item, ibStyleLook look)
{
	return typeConv::ColourToWxColour(std::get<ibColour>(ibStyle::Platform().Get(item, look)));
}

// …and an author's, chosen in the light look, as that look paints it.
wxColour StyleColour(const wxColour& colour, ibStyleLook look)
{
	return typeConv::ColourToWxColour(ibStyle::Platform().Get(typeConv::WxColourToColour(colour), look));
}

} // namespace

ibValueFrame* ibValueFrame::Make(const wxString& type, ibVisualHostClient& host, long long controlId)
{
	const auto& types = Types();
	const auto registered = types.find(type);
	ibValueFrame* const made = registered != types.end() ? registered->second(host, controlId)
		: new ibViewPlaceholder(host, controlId);
	made->m_className = type;
	return made;
}

void ibValueFrame::Register(const wxString& type, ibValueFrame* (*make)(ibVisualHostClient& host, long long controlId))
{
	Types()[type] = make;
}

bool ibValueFrame::ChangeChildPosition(ibValueFrame* obj, unsigned int pos)
{
	// The desktop's re-lays its controls here (OnChangeChildPosition); this one's are laid out by the server's answer.
	return ibPropertyObjectHelper::ChangeChildPosition(obj, pos);
}

bool ibValueFrame::Fetch()
{
	return m_host.Fetch(this);
}

wxSizerItem* ibValueFrame::GetSizerItem() const
{
	// It is in the sizer already (the host's Generate) — the item it stands in is found, not added again: a window knows
	// its sizer, a sizer the window it is laid out in.
	if (wxWindow* const window = GetWindow()) {
		if (wxSizer* const holder = window->GetContainingSizer())
			return holder->GetItem(window);
	}
	else if (wxSizer* const sizer = GetSizer()) {
		wxWindow* const owner = sizer->GetContainingWindow();
		if (owner != nullptr && owner->GetSizer() != nullptr)
			return owner->GetSizer()->GetItem(sizer, true);
	}
	return nullptr;
}

void ibValueFrame::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	m_host.ModifyProperty(property, oldValue, newValue);
}

ibProperty* ibValueFrame::CreateProperty(ibPropertyCategory* cat, ibDataNode&& row)
{
	return ibPropertyObject::CreateProperty<ibPropertySerializable>(cat, std::move(row));
}

ibValueFrame* ibValueFrame::DoFindControlByName(const wxString& controlName, ibValueFrame* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		ibValueFrame* child = top->GetChild(idx);
		wxASSERT(child);
		ibValueFrame* foundedMeta = DoFindControlByName(controlName, child);
		if (foundedMeta) {
			return foundedMeta;
		}
	}

	if (stringUtils::IsSameAs(controlName, top->GetControlName())) {
		return top;
	}

	return nullptr;
}

ibValueFrame* ibValueFrame::FindControlByName(const wxString& controlName) const
{
	ibValueForm* const valueForm = m_host.GetValueForm();
	return valueForm != nullptr ? DoFindControlByName(controlName, valueForm) : nullptr;
}

bool ibValueControl::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	if (property->GetName() == ibProtocol::ibSchema::ibProperties::Name && FindControlByName(newValue.GetString()) != nullptr)
		return false;

	return true;
}

void ibValueFrame::SetSubscribedEvents(const std::vector<ibProtocolNode>& events)
{
	m_subscribedEvents = 0;
	for (const ibProtocolNode& event : events) {
		const long long number = event.AsInt();
		if (number > 0 && number < 32)
			m_subscribedEvents |= 1u << number;
	}
}

void ibValueFrame::Send(ibProtocol::ibEvent::ibKind event) const
{
	Send(event, ibProtocolNode());
}

void ibValueFrame::Send(ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args) const
{
	// WHERE THE PERSON IS, the server not subscribed to it: it waits for the next call, whatever that call is — held unless
	// the server speaks no protocol that carries it.
	if (ibProtocol::ibEvent::IsStateEvent(event) && !IsSubscribed(event) && m_host.Hold(m_controlId, event, args))
		return;
	m_host.Send(m_controlId, event, args);
}

ibViewFetcher ibValueFrame::MakeFetcher() const
{
	return m_host.MakeFetcher(m_controlId);
}

void ibValueFrame::UpdateWindow(wxWindow* window, const ibProtocolNode& node, const wxColour& platformBackground,
	const wxColour& themedBackground)
{
	if (window == nullptr)
		return;

	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);
	const wxSize minSize = typeConv::StringToSize(node.GetString(ibProtocol::ibForm::ibProperty::MinimumSize));
	const wxSize maxSize = typeConv::StringToSize(node.GetString(ibProtocol::ibForm::ibProperty::MaximumSize));

	if (minSize != wxDefaultSize)
		window->SetMinSize(minSize);
	if (maxSize != wxDefaultSize)
		window->SetMaxSize(maxSize);

	ApplyLook(window, node, platformBackground, themedBackground);

	// The server has decided both: enabled with its form, shown where it is available and bound.
	window->Enable(state.GetBool(ibProtocol::ibForm::ibState::Enabled, true));
	window->Show(state.GetBool(ibProtocol::ibForm::ibState::Visible, true));
	window->SetToolTip(state.GetString(ibProtocol::ibForm::ibState::Tooltip));

	if (minSize != wxDefaultSize || maxSize != wxDefaultSize)
		window->Layout();
}

void ibValueFrame::ApplyLook(wxWindow* window, const ibProtocolNode& node, const wxColour& platformBackground,
	const wxColour& themedBackground)
{
	if (window == nullptr)
		return;

	// Unset — no text, or a stored form's text with no size: the window keeps its parent's. wxNullFont is not that (the
	// desktop's ApplyLook): a notebook draws every inactive tab with no font at all.
	const wxFont font = typeConv::StringToFont(node.GetString(ibProtocol::ibForm::ibProperty::Font));
	if (font.IsOk())
		window->SetFont(font);

	// THE PLATFORM'S OWN LOOK — a colour the form leaves empty: its author picked none (Max, 2026-11-24: the property
	// holds "the platform's", not the light look's colour). Both the platform's: the form's ink in the look in force, and
	// the kind's own ground - the theme's for it (`themedBackground`: a form's its ground, a table's the content's), or,
	// none given, no colour at all: the theme paints the control as its kind and state have it - a field its well while
	// it can be typed into (ibTextCtrl::HasContentBackground, 2026-11-13), a button its face, a label and a group the
	// surface under them (ibLunaColourScheme::GetBackground; Max: "an empty background - each with its own").
	// One of them the author's: the author chose it against the light look's other side, so that side is what it gets -
	// the light look's ink, or its ground for the kind (`platformBackground`) - and the pair reads as the author saw it.
	// In the dark look that pair is carried there whole (ibStyle::Get of a colour): each side's lightness turned over, its
	// hue kept - a light ground the dark look's, a dark ink a light one; a bright yellow ground no longer stands lit in a
	// dark form (Max, 2026-11-25).
	// The web client's the same (control.js, themedPair; a kind's pair, its platformBackground).
	const wxString foreground = node.GetString(ibProtocol::ibForm::ibProperty::ForegroundColour);
	const wxString background = node.GetString(ibProtocol::ibForm::ibProperty::BackgroundColour);
	const wxColour text = !foreground.IsEmpty() ? typeConv::StringToColour(foreground) : wxNullColour;
	const wxColour ground = !background.IsEmpty() ? typeConv::StringToColour(background) : wxNullColour;
	const ibStyleLook look = wxSystemSettings::GetAppearance().IsDark() ? ibStyleLook::Dark : ibStyleLook::Light;
	if (!text.IsOk() && !ground.IsOk()) {
		window->SetForegroundColour(StyleColour(ibStyleItem::FormText, look));
		window->SetBackgroundColour(themedBackground);
		return;
	}
	window->SetForegroundColour(StyleColour(text.IsOk() ? text : StyleColour(ibStyleItem::FormText, ibStyleLook::Light), look));
	window->SetBackgroundColour(StyleColour(ground.IsOk() ? ground : platformBackground.IsOk() ? platformBackground
		: StyleColour(ibStyleItem::FormBackground, ibStyleLook::Light), look));
}
