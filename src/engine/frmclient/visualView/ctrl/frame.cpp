////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a control of a view
////////////////////////////////////////////////////////////////////////////

#include "frame.h"
#include "frmclient/visualView/visualHostClient.h"

#include "gridBox.h"
#include "sizer.h"
#include "tableBox.h"
#include "widgets.h"

#include "frmclient/win/typeconv.h"

namespace {

ibValueFrame* MakeOfType(const wxString& type, ibVisualHostClient& host, long long controlId)
{
	if (type == ibProtocolType::SizerItem)
		return new ibValueSizerItem(host, controlId);
	if (type == ibProtocolType::Boxsizer)
		return new ibValueBoxSizer(host, controlId);
	if (type == ibProtocolType::Wrapsizer)
		return new ibValueWrapSizer(host, controlId);
	if (type == ibProtocolType::Gridsizer)
		return new ibValueGridSizer(host, controlId);
	if (type == ibProtocolType::Staticboxsizer)
		return new ibValueStaticBoxSizer(host, controlId);
	if (type == ibProtocolType::Statictext)
		return new ibValueStaticText(host, controlId);
	if (type == ibProtocolType::Textctrl)
		return new ibValueTextCtrl(host, controlId);
	if (type == ibProtocolType::Button)
		return new ibValueButton(host, controlId);
	if (type == ibProtocolType::Checkbox)
		return new ibValueCheckbox(host, controlId);
	if (type == ibProtocolType::Staticline)
		return new ibValueStaticLine(host, controlId);
	if (type == ibProtocolType::Tablebox)
		return new ibValueModelTableBox(host, controlId);
	if (type == ibProtocolType::TableboxColumn)
		return new ibValueModelTableBoxColumn(host, controlId);
	if (type == ibProtocolType::TableboxColumnGroup)
		return new ibValueModelTableBoxColumnGroup(host, controlId);
	if (type == ibProtocolType::Gridbox)
		return new ibValueGridBox(host, controlId);
	if (type == ibProtocolType::Textbox)
		return new ibValueTextBox(host, controlId);
	return nullptr;
}

} // namespace

ibValueFrame* ibValueFrame::Make(const wxString& type, ibVisualHostClient& host, long long controlId)
{
	ibValueFrame* const made = MakeOfType(type, host, controlId);
	if (made != nullptr)
		made->m_className = type;
	return made;
}

wxArrayString ibValueFrame::GetAllowedUserProperty()
{
	wxArrayString arr;

	arr.Add(wxT("title"));
	arr.Add(wxT("minimum_size"));
	arr.Add(wxT("maximum_size"));
	arr.Add(wxT("font"));
	arr.Add(wxT("fg"));
	arr.Add(wxT("bg"));
	arr.Add(wxT("align"));
	arr.Add(wxT("stretch"));
	arr.Add(wxT("proportion"));
	arr.Add(wxT("orient"));
	arr.Add(wxT("tooltip"));
	arr.Add(wxT("visible"));

	return arr;
}

bool ibValueFrame::ChangeChildPosition(ibValueFrame* obj, unsigned int pos)
{
	// The desktop's re-lays its controls here (OnChangeChildPosition); this one's are laid out by the server's answer.
	return ibPropertyObjectHelper::ChangeChildPosition(obj, pos);
}

void ibValueFrame::Send(ibProtocolEvent event) const
{
	m_host.Send(m_controlId, event, ibProtocolNode());
}

void ibValueFrame::Send(ibProtocolEvent event, const ibProtocolNode& args) const
{
	m_host.Send(m_controlId, event, args);
}

ibViewFetcher ibValueFrame::MakeFetcher() const
{
	return m_host.MakeFetcher(m_controlId);
}

void ibValueFrame::UpdateWindow(wxWindow* window, const ibProtocolNode& node)
{
	if (window == nullptr)
		return;

	const ibProtocolNode state = node.FindChild(ibProtocolName::State);
	const wxSize minSize = typeConv::StringToSize(node.GetString(ibProtocolName::MinimumSize));
	const wxSize maxSize = typeConv::StringToSize(node.GetString(ibProtocolName::MaximumSize));

	if (minSize != wxDefaultSize)
		window->SetMinSize(minSize);
	if (maxSize != wxDefaultSize)
		window->SetMaxSize(maxSize);

	ApplyLook(window, node);

	// The server has decided both: enabled with its form, shown where it is available and bound.
	window->Enable(state.GetBool(ibProtocolName::Enabled, true));
	window->Show(state.GetBool(ibProtocolName::Visible, true));
	window->SetToolTip(state.GetString(ibProtocolName::Tooltip));

	if (minSize != wxDefaultSize || maxSize != wxDefaultSize)
		window->Layout();
}

void ibValueFrame::ApplyLook(wxWindow* window, const ibProtocolNode& node)
{
	if (window == nullptr)
		return;

	// Unset, the form writes no text for it.
	const wxString font = node.GetString(ibProtocolName::Font);
	if (!font.IsEmpty())
		window->SetFont(typeConv::StringToFont(font));
	const wxString foreground = node.GetString(ibProtocolName::ForegroundColour);
	if (!foreground.IsEmpty())
		window->SetForegroundColour(typeConv::StringToColour(foreground));
	const wxString background = node.GetString(ibProtocolName::BackgroundColour);
	if (!background.IsEmpty())
		window->SetBackgroundColour(typeConv::StringToColour(background));
}
