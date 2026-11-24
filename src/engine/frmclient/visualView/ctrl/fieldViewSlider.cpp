////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a field of a view - a number moved between two ends (Slider)
////////////////////////////////////////////////////////////////////////////

#include "field.h"

#include "deskgui/univ/ctrl/slider.h"

void ibFieldViewSlider::Create(wxWindow* parent, const ibProtocolNode& node)
{
	const int min = static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::MinValue, 0));
	m_slider = new ibSlider(parent, wxID_ANY, min, min,
		static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::MaxValue, 100)),
		wxDefaultPosition, wxDefaultSize, static_cast<long>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxHORIZONTAL)));

	// WHERE IT WAS LET GO — a key, a page, the thumb released; not while the thumb is dragged (THUMBTRACK), the value is
	// written once. A slider that is not editable says nothing.
	for (const wxEventTypeTag<wxScrollEvent>& type : { wxEVT_SCROLL_TOP, wxEVT_SCROLL_BOTTOM, wxEVT_SCROLL_LINEUP,
		wxEVT_SCROLL_LINEDOWN, wxEVT_SCROLL_PAGEUP, wxEVT_SCROLL_PAGEDOWN, wxEVT_SCROLL_THUMBRELEASE, wxEVT_SCROLL_CHANGED })
		m_slider->Bind(type, &ibFieldViewSlider::OnScroll, this);
}

void ibFieldViewSlider::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	// The way it runs, turned where it stands: the slider reads it from its style as it paints and measures.
	const long orient = static_cast<long>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxHORIZONTAL));
	const long style = (m_slider->GetWindowStyleFlag() & ~(wxSL_HORIZONTAL | wxSL_VERTICAL)) | orient;
	if (style != m_slider->GetWindowStyleFlag()) {
		m_slider->SetWindowStyleFlag(style);
		m_slider->InvalidateBestSize();
	}

	m_slider->SetRange(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::MinValue, 0)),
		static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::MaxValue, 100)));
	m_slider->SetEditable(!state.GetBool(ibProtocol::ibForm::ibState::ReadOnly));
	// The value — none in the designer.
	if (state.Has(ibProtocol::ibForm::ibState::Position))
		m_slider->SetValue(static_cast<int>(state.GetInt(ibProtocol::ibForm::ibState::Position)));
}

ibControl* ibFieldViewSlider::GetControl() const
{
	return m_slider;
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibFieldViewSlider::OnScroll(wxScrollEvent& event)
{
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Position, static_cast<long long>(event.GetPosition()));
	Send(ibProtocol::ibEvent::ibKind::Change, args);
}
