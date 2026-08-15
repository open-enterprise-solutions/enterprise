
#include "sizer.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
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

void ibValueGridSizer::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
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

bool ibValueGridSizer::ReadData(const ibDataNode& node)
{
	m_propertyRows->SetNodeValue(node.GetProperty(m_propertyRows->GetName()));
	m_propertyCols->SetNodeValue(node.GetProperty(m_propertyCols->GetName()));

	return ibValueSizer::ReadData(node);
}

bool ibValueGridSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyRows->GetName(), m_propertyRows->GetNodeValue());
	node.SetProperty(m_propertyCols->GetName(), m_propertyCols->GetNodeValue());

	return ibValueSizer::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueGridSizer, "Gridsizer", "Sizer", control_to_clsid("CT_GSZR"));
