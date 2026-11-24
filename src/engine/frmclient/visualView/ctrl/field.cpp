////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a field of a view - one control, drawn with its view's widget
////////////////////////////////////////////////////////////////////////////

#include "field.h"

#include "deskgui/univ/ctrl/control.h"
#include "deskgui/typeconv.h"    // typeConv::ColourToWxColour
#include "core/style/style.h"    // ibStyle - a field's platform background

//*******************************************************************
//*                          The view's child                       *
//*******************************************************************

void ibFieldViewProvider::ShowCaption(const ibProtocolNode& state)
{
	GetControl()->SetTitle(state.GetString(ibProtocol::ibForm::ibState::Caption));
}

std::unique_ptr<ibFieldViewProvider> ibFieldViewProvider::Make(long long view, ibValueField& field)
{
	switch (static_cast<ibProtocol::ibForm::ibProperty::ibFieldView>(view)) {
	case ibProtocol::ibForm::ibProperty::ibFieldView::Flag:
		return std::make_unique<ibFieldViewFlag>(field);
	case ibProtocol::ibForm::ibProperty::ibFieldView::Label:
		return std::make_unique<ibFieldViewLabel>(field);
	case ibProtocol::ibForm::ibProperty::ibFieldView::Switch:
		return std::make_unique<ibFieldViewSwitch>(field);
	case ibProtocol::ibForm::ibProperty::ibFieldView::Gauge:
		return std::make_unique<ibFieldViewGauge>(field);
	case ibProtocol::ibForm::ibProperty::ibFieldView::Slider:
		return std::make_unique<ibFieldViewSlider>(field);
	default:
		// Input — and a view of a newer server this client does not know: a field is typed into.
		return std::make_unique<ibFieldViewInput>(field);
	}
}

void ibFieldViewProvider::Send(ibProtocol::ibEvent::ibKind event) const
{
	m_field.Send(event);
}

void ibFieldViewProvider::Send(ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args) const
{
	m_field.Send(event, args);
}

//*******************************************************************
//*                              The field                          *
//*******************************************************************

void ibValueField::Create(wxWindow* parent, const ibProtocolNode& node)
{
	// The view is what it is built with: a new one builds the field anew (the host's shape), never turns this one.
	m_provider = ibFieldViewProvider::Make(node.GetInt(ibProtocol::ibForm::ibProperty::View), *this);
	m_provider->Create(parent, node);
}

void ibValueField::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);
	ibControl* const control = m_provider->GetControl();

	// The caption and its side are the field's, whatever widget its view drew — the left column lined up with the other
	// fields' titles.
	control->SetTitlePosition(node.GetInt(ibProtocol::ibForm::ibProperty::TitleLocation)
		== static_cast<int>(ibProtocol::ibForm::ibProperty::ibTitleLocation::Left) ? wxLEFT : wxRIGHT);
	m_provider->ShowCaption(state);
	m_provider->Update(node);

	UpdateWindow(control, node, GetPlatformBackground());
}

wxWindow* ibValueField::GetWindow() const
{
	return m_provider != nullptr ? m_provider->GetControl() : nullptr;
}

wxColour ibValueField::GetPlatformBackground() const
{
	return typeConv::ColourToWxColour(std::get<ibColour>(ibStyle::Platform().Get(ibStyleItem::FieldBackground, false)));
}

bool ibValueField::SaveEdit(ibViewEdit& edit) const
{
	return m_provider->SaveEdit(edit);
}

void ibValueField::RestoreEdit(const ibViewEdit& edit)
{
	m_provider->RestoreEdit(edit);
}

CONTROL_REGISTER(ibValueField, ibProtocol::ibForm::ibType::Field)
