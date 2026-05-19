#include "sizer.h"
#ifdef OES_USE_WEB
#include "frontend/web/webSizer.h"
#endif

wxIMPLEMENT_DYNAMIC_CLASS(ibValueBoxSizer, ibValueSizer)

//*******************************************************************
//*                             BoxSizer                            *
//*******************************************************************

ibValueBoxSizer::ibValueBoxSizer() : ibValueSizer()
{
}

wxObject* ibValueBoxSizer::Create(ibFrontendWindow* /*parent*/, ibVisualHost* /*visualHost*/)
{
#ifdef OES_USE_WEB
	// Sizers have no ctor parent on wx either — the owner (wxWindow or
	// another sizer) adopts them via SetSizer / Add. Walker does the
	// analogous SetParent on web.
	return new ibWebBoxSizer(m_propertyOrient->GetValueAsInteger());
#else
	return new wxBoxSizer(m_propertyOrient->GetValueAsInteger());
#endif
}

void ibValueBoxSizer::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost *visualHost, bool firstCreated)
{
}

void ibValueBoxSizer::Update(wxObject* wxobject, ibVisualHost *visualHost)
{
	// static_cast: Create() guarantees the type (wxBoxSizer desktop,
	// ibWebBoxSizer web); walker returns the same pointer unchanged.
	// Web's SetMinSize is a no-op (CSS layout), see ibWebSizer.
	if (wxobject == nullptr) return;
#ifdef OES_USE_WEB
	ibWebBoxSizer* boxSizer = static_cast<ibWebBoxSizer*>(wxobject);
#else
	wxBoxSizer*    boxSizer = static_cast<wxBoxSizer*>(wxobject);
#endif
	boxSizer->SetOrientation(m_propertyOrient->GetValueAsInteger());
	boxSizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	UpdateSizer(boxSizer);
}

void ibValueBoxSizer::Cleanup(wxObject* obj, ibVisualHost *visualHost)
{
}

//*******************************************************************
//*                            Data									*
//*******************************************************************

bool ibValueBoxSizer::LoadData(ibReaderMemory &reader)
{
	m_propertyOrient->SetValue(reader.r_u16());
	return ibValueSizer::LoadData(reader);
}

bool ibValueBoxSizer::SaveData(ibWriterMemory& writer)
{
	writer.w_u16(m_propertyOrient->GetValueAsInteger());
	return ibValueSizer::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueBoxSizer, "Boxsizer", "Sizer", string_to_clsid("CT_BSZR"));