
#include "sizer.h"
#ifdef OES_USE_WEB
#include "frontend/web/webSizer.h"
#endif


//****************************************************************************
//*                             WrapSizer                                    *
//****************************************************************************

ibValueWrapSizer::ibValueWrapSizer() : ibValueSizer()
{
}

wxObject* ibValueWrapSizer::Create(ibFrontendWindow* /*parent*/, ibVisualHost* /*visualHost*/)
{
#ifdef OES_USE_WEB
	return new ibWebWrapSizer(m_propertyOrient->GetValueAsInteger());
#else
	return new wxWrapSizer(m_propertyOrient->GetValueAsInteger(), wxWRAPSIZER_DEFAULT_FLAGS);
#endif
}

void ibValueWrapSizer::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost *visualHost, bool firstСreated)
{
}

void ibValueWrapSizer::Update(wxObject* wxobject, ibVisualHost *visualHost)
{
	// static_cast: Create() always returns the concrete type we see
	// here (wxWrapSizer on desktop / ibWebWrapSizer on web); the walker
	// hands the same wxObject* back unchanged, so the dynamic check
	// via dynamic_cast is unnecessary overhead.
	if (wxobject == nullptr) return;
#ifdef OES_USE_WEB
	ibWebWrapSizer* wrapsizer = static_cast<ibWebWrapSizer*>(wxobject);
#else
	wxWrapSizer*    wrapsizer = static_cast<wxWrapSizer*>(wxobject);
#endif
	wrapsizer->SetOrientation(m_propertyOrient->GetValueAsInteger());
	wrapsizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	UpdateSizer(wrapsizer);
}

void ibValueWrapSizer::Cleanup(wxObject* obj, ibVisualHost *visualHost)
{
}

//**********************************************************************************
//*                            Data												   *
//**********************************************************************************

bool ibValueWrapSizer::LoadData(ibReaderMemory &reader)
{
	m_propertyOrient->SetValue(reader.r_u16());
	return ibValueSizer::LoadData(reader);
}

bool ibValueWrapSizer::SaveData(ibWriterMemory& writer)
{
	writer.w_u16(m_propertyOrient->GetValueAsInteger());
	return ibValueSizer::SaveData(writer);
}