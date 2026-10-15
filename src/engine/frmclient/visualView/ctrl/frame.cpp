////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a control of a view
////////////////////////////////////////////////////////////////////////////

#include "frame.h"
#include "frmclient/visualView/visualHostClient.h"

#include "gridBox.h"
#include "notebook.h"
#include "sizer.h"
#include "tableBox.h"
#include "widgets.h"

#include "core/stringUtils.h"   // stringUtils::CompareString — a name found as the desktop's finds it
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
	if (type == ibProtocolType::Notebook)
		return new ibValueNotebook(host, controlId);
	if (type == ibProtocolType::NotebookPage)
		return new ibValueNotebookPage(host, controlId);
	if (type == ibProtocolType::Radiobutton)
		return new ibValueRadioButton(host, controlId);
	if (type == ibProtocolType::Slider)
		return new ibValueSlider(host, controlId);
	if (type == ibProtocolType::Gauge)
		return new ibValueGauge(host, controlId);
	if (type == ibProtocolType::Htmlbox)
		return new ibValueHTMLBox(host, controlId);
	if (type == ibProtocolType::Chartbox)
		return new ibValueChartBox(host, controlId);
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

bool ibValueFrame::ChangeChildPosition(ibValueFrame* obj, unsigned int pos)
{
	// The desktop's re-lays its controls here (OnChangeChildPosition); this one's are laid out by the server's answer.
	return ibPropertyObjectHelper::ChangeChildPosition(obj, pos);
}

void ibValueFrame::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	m_host.ModifyProperty(property, oldValue, newValue);
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

	if (stringUtils::CompareString(controlName, top->GetControlName())) {
		return top;
	}

	return nullptr;
}

ibValueFrame* ibValueFrame::FindControlByName(const wxString& controlName) const
{
	ibValueForm* const valueForm = m_host.GetValueForm();
	return valueForm != nullptr ? DoFindControlByName(controlName, valueForm) : nullptr;
}

bool ibValueControl::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	if (property->GetName() == ibProtocolName::Name && FindControlByName(newValue.GetString()) != nullptr)
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

void ibValueFrame::Send(ibProtocolEvent event) const
{
	Send(event, ibProtocolNode());
}

void ibValueFrame::Send(ibProtocolEvent event, const ibProtocolNode& args) const
{
	// WHERE THE PERSON IS, the server not subscribed to it: it waits for the next call, whatever that call is — held unless
	// the server speaks no protocol that carries it.
	if (ibProtocolIsStateEvent(event) && !IsSubscribed(event) && m_host.Hold(m_controlId, event, args))
		return;
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
