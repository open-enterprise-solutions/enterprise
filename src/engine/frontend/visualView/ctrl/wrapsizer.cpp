
#include "sizer.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
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
	// Not wxWRAPSIZER_DEFAULT_FLAGS: its wxEXTEND_LAST_ON_EACH_LINE stretches the last control of every
	// line to the line's end, whatever its own sizer item and MaximumSize say. A control is as wide as it
	// asks to be; leading spaces at a line's start are still dropped.
	return new wxWrapSizer(m_propertyOrient->GetValueAsInteger(), wxREMOVE_LEADING_SPACES);
#endif
}

void ibValueWrapSizer::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost *visualHost, bool firstCreated)
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
	wrapsizer->SetMinSize(typeConv::SizeToWxSize(m_propertyMinSize->GetValueAsSize()));
	UpdateSizer(wrapsizer);
}

void ibValueWrapSizer::Cleanup(wxObject* obj, ibVisualHost *visualHost)
{
}

//**********************************************************************************
//*                            Data												   *
//**********************************************************************************

bool ibValueWrapSizer::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return ibValueSizer::ReadData(node);
}

bool ibValueWrapSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return ibValueSizer::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueWrapSizer, "Wrapsizer", "Sizer");