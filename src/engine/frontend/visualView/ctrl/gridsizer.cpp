
#include "sizer.h"
#ifdef OES_USE_WEB
#include "frontend/web/webSizer.h"
#endif


//****************************************************************************
//*                             GridSizer                                    *
//****************************************************************************

ibValueGridSizer::ibValueGridSizer() : ibValueSizer()
{
}

wxObject* ibValueGridSizer::Create(ibFrontendWindow* /*parent*/, ibVisualHost* /*visualHost*/)
{
#ifdef OES_USE_WEB
	return new ibWebGridSizer(
		m_propertyRows->GetValueAsUInteger(),
		m_propertyCols->GetValueAsUInteger());
#else
	return new wxGridSizer(m_propertyRows->GetValueAsUInteger(), m_propertyCols->GetValueAsUInteger(), 0, 0);
#endif
}

void ibValueGridSizer::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

void ibValueGridSizer::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	// static_cast: Create-known type (wxGridSizer / ibWebGridSizer). Both
	// expose SetRows / SetCols / SetMinSize with matching semantics.
	if (wxobject == nullptr) return;
#ifdef OES_USE_WEB
	ibWebGridSizer* gridsizer = static_cast<ibWebGridSizer*>(wxobject);
#else
	wxGridSizer*    gridsizer = static_cast<wxGridSizer*>(wxobject);
#endif
	gridsizer->SetRows(m_propertyRows->GetValueAsUInteger());
	gridsizer->SetCols(m_propertyCols->GetValueAsUInteger());
	gridsizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	UpdateSizer(gridsizer);
}

void ibValueGridSizer::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//**********************************************************************************
//*                           Property                                             *
//**********************************************************************************

bool ibValueGridSizer::LoadData(ibReaderMemory& reader)
{
	m_propertyRows->SetValue(reader.r_s32());
	m_propertyCols->SetValue(reader.r_s32());

	return ibValueSizer::LoadData(reader);
}

bool ibValueGridSizer::SaveData(ibWriterMemory& writer)
{
	writer.w_s32(m_propertyRows->GetValueAsUInteger());
	writer.w_s32(m_propertyCols->GetValueAsUInteger());

	return ibValueSizer::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueGridSizer, "Gridsizer", "Sizer", string_to_clsid("CT_GSZR"));