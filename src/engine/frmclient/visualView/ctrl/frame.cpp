////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
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
#include "frmclient/backend/propertyManager/property/propertySerializable.h"   // its properties — a row's each
#include "frmclient/win/typeconv.h"

namespace {

ibValueFrame* MakeOfType(const wxString& type, ibVisualHostClient& host, long long controlId)
{
	if (type == ibProtocol::ibForm::ibType::SizerItem)
		return new ibValueSizerItem(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Boxsizer)
		return new ibValueBoxSizer(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Wrapsizer)
		return new ibValueWrapSizer(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Gridsizer)
		return new ibValueGridSizer(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Staticboxsizer)
		return new ibValueStaticBoxSizer(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Statictext)
		return new ibValueStaticText(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Textctrl)
		return new ibValueTextCtrl(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Button)
		return new ibValueButton(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Checkbox)
		return new ibValueCheckbox(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Staticline)
		return new ibValueStaticLine(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Tablebox)
		return new ibValueModelTableBox(host, controlId);
	if (type == ibProtocol::ibForm::ibType::TableboxColumn)
		return new ibValueModelTableBoxColumn(host, controlId);
	if (type == ibProtocol::ibForm::ibType::TableboxColumnGroup)
		return new ibValueModelTableBoxColumnGroup(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Gridbox)
		return new ibValueGridBox(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Textbox)
		return new ibValueTextBox(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Notebook)
		return new ibValueNotebook(host, controlId);
	if (type == ibProtocol::ibForm::ibType::NotebookPage)
		return new ibValueNotebookPage(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Radiobutton)
		return new ibValueRadioButton(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Slider)
		return new ibValueSlider(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Gauge)
		return new ibValueGauge(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Htmlbox)
		return new ibValueHTMLBox(host, controlId);
	if (type == ibProtocol::ibForm::ibType::Chartbox)
		return new ibValueChartBox(host, controlId);
	return nullptr;
}

} // namespace

ibValueFrame* ibValueFrame::Make(const wxString& type, ibVisualHostClient& host, long long controlId)
{
	ibValueFrame* made = MakeOfType(type, host, controlId);
	if (made == nullptr)
		made = new ibViewPlaceholder(host, controlId);
	made->m_className = type;
	return made;
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

void ibValueFrame::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
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

void ibValueFrame::UpdateWindow(wxWindow* window, const ibProtocolNode& node)
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

	ApplyLook(window, node);

	// The server has decided both: enabled with its form, shown where it is available and bound.
	window->Enable(state.GetBool(ibProtocol::ibForm::ibState::Enabled, true));
	window->Show(state.GetBool(ibProtocol::ibForm::ibState::Visible, true));
	window->SetToolTip(state.GetString(ibProtocol::ibForm::ibState::Tooltip));

	if (minSize != wxDefaultSize || maxSize != wxDefaultSize)
		window->Layout();
}

void ibValueFrame::ApplyLook(wxWindow* window, const ibProtocolNode& node)
{
	if (window == nullptr)
		return;

	// Unset — no text, or a stored form's text with no size: the window keeps its parent's. wxNullFont is not that (the
	// desktop's ApplyLook): a notebook draws every inactive tab with no font at all.
	const wxFont font = typeConv::StringToFont(node.GetString(ibProtocol::ibForm::ibProperty::Font));
	if (font.IsOk())
		window->SetFont(font);
	const wxString foreground = node.GetString(ibProtocol::ibForm::ibProperty::ForegroundColour);
	if (!foreground.IsEmpty())
		window->SetForegroundColour(typeConv::StringToColour(foreground));
	const wxString background = node.GetString(ibProtocol::ibForm::ibProperty::BackgroundColour);
	if (!background.IsEmpty())
		window->SetBackgroundColour(typeConv::StringToColour(background));
}
