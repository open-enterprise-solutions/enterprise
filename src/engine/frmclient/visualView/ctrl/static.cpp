////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a static of a view — a decoration
////////////////////////////////////////////////////////////////////////////

#include "static.h"

//*******************************************************************
//*                          The view's child                       *
//*******************************************************************

std::unique_ptr<ibStaticViewProvider> ibStaticViewProvider::Make(long long view)
{
	switch (static_cast<ibProtocol::ibForm::ibProperty::ibStaticView>(view)) {
	case ibProtocol::ibForm::ibProperty::ibStaticView::Line:
		return std::make_unique<ibStaticViewLine>();
	default:
		// Text — and a view of a newer server this client does not know: its caption.
		return std::make_unique<ibStaticViewText>();
	}
}

//*******************************************************************
//*                              The static                         *
//*******************************************************************

void ibValueStatic::Create(wxWindow* parent, const ibProtocolNode& node)
{
	// The view is what it is built with: a new one builds the static anew (the host's shape), never turns this one.
	m_provider = ibStaticViewProvider::Make(node.GetInt(ibProtocol::ibForm::ibProperty::View));
	m_provider->Create(parent, node);
}

void ibValueStatic::Update(const ibProtocolNode& node)
{
	m_provider->Update(node);
	UpdateWindow(m_provider->GetWindow(), node);
}

wxWindow* ibValueStatic::GetWindow() const
{
	return m_provider != nullptr ? m_provider->GetWindow() : nullptr;
}

CONTROL_REGISTER(ibValueStatic, ibProtocol::ibForm::ibType::Static)
