////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the layout of a view
////////////////////////////////////////////////////////////////////////////

#include "layout.h"

#include "deskgui/typeconv.h"

void ibViewUpdateSizer(wxSizer* sizer, const ibProtocolNode& node)
{
	if (sizer == nullptr)
		return;

	const wxSize minSize = typeConv::StringToSize(node.GetString(ibProtocol::ibForm::ibProperty::MinimumSize));
	if (minSize != wxDefaultSize) {
		sizer->SetMinSize(minSize);
		sizer->Layout();
	}
}

//*******************************************************************
//*                          The view's child                       *
//*******************************************************************

std::unique_ptr<ibLayoutViewProvider> ibLayoutViewProvider::Make(long long view)
{
	switch (static_cast<ibProtocol::ibForm::ibProperty::ibLayoutView>(view)) {
	case ibProtocol::ibForm::ibProperty::ibLayoutView::Wrap:
		return std::make_unique<ibLayoutViewWrap>();
	case ibProtocol::ibForm::ibProperty::ibLayoutView::Grid:
		return std::make_unique<ibLayoutViewGrid>();
	case ibProtocol::ibForm::ibProperty::ibLayoutView::Group:
		return std::make_unique<ibLayoutViewGroup>();
	default:
		// Box — and a view of a newer server this client does not know: a row or a column.
		return std::make_unique<ibLayoutViewBox>();
	}
}

void ibLayoutViewProvider::ApplyLook(wxWindow* window, const ibProtocolNode& node)
{
	ibValueLayout::ApplyLook(window, node);
}

//*******************************************************************
//*                              Layout                             *
//*******************************************************************

void ibValueLayout::Create(wxWindow* parent, const ibProtocolNode& node)
{
	// The view is what it is built with: a new one builds the layout anew (the host's shape), never turns this one.
	m_provider = ibLayoutViewProvider::Make(node.GetInt(ibProtocol::ibForm::ibProperty::View));
	m_provider->Create(parent, node);
}

void ibValueLayout::Update(const ibProtocolNode& node)
{
	wxSizer* const sizer = m_provider->GetSizer();
	sizer->SetMinSize(typeConv::StringToSize(node.GetString(ibProtocol::ibForm::ibProperty::MinimumSize)));
	m_provider->Update(node);

	ibViewUpdateSizer(sizer, node);
}

wxSizer* ibValueLayout::GetSizer() const
{
	return m_provider != nullptr ? m_provider->GetSizer() : nullptr;
}

wxWindow* ibValueLayout::GetChildParent() const
{
	return m_provider != nullptr ? m_provider->GetChildParent() : nullptr;
}

//*******************************************************************
//*                     the place in the sizer                      *
//*******************************************************************

void ibViewUpdateSizer(wxSizerItem* item, const ibProtocolNode& node)
{
	if (item == nullptr)
		return;

	// The flag — the borders it has, and how it stretches (wxSizerItem::GetFlag).
	int flag = static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Stretch));
	if (node.GetBool(ibProtocol::ibForm::ibProperty::BorderLeft))
		flag |= wxLEFT;
	if (node.GetBool(ibProtocol::ibForm::ibProperty::BorderRight))
		flag |= wxRIGHT;
	if (node.GetBool(ibProtocol::ibForm::ibProperty::BorderTop))
		flag |= wxUP;
	if (node.GetBool(ibProtocol::ibForm::ibProperty::BorderBottom))
		flag |= wxDOWN;

	item->SetProportion(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Proportion)));
	item->SetFlag(flag);
	item->SetBorder(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::BorderSize)));
}

CONTROL_REGISTER(ibValueLayout, ibProtocol::ibForm::ibType::Layout)
