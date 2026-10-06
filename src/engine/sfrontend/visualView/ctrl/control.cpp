////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "control.h"
#include "typeControl.h"   // ibTypeControlFactory + ibBackendTypeConfigFactory::GetDefaultTypeByFilter (AutoBindNewSource)
#include "form.h"
#include "backend/appData.h"                 // ibApplicationInstance::GetActiveMetaData — GetMetaData fallback (common form / metadata-free source)
#include "backend/metadataConfiguration.h"   // ibMetaDataConfigurationBase : ibMetaData — the GetActiveMetaData() base cast


//*************************************************************************
//*                          ValueControl		                          *
//*************************************************************************

ibValueControl::ibValueControl()
	: ibValueFrame(), m_formOwner(nullptr)
{
}

ibValueControl::~ibValueControl()
{
	SetOwnerForm(nullptr);
}

#include "backend/metaData.h"

// The element's own options, and everything it stands inside: a group, a page or a plain sizer the options
// switch off takes what it holds with it — the way a field of an unavailable object is unavailable too.
bool ibValueControl::IsAvailable() const
{
	const ibValueFrame* parent = GetParent();
	return m_propertyFunctionalOptions->IsAvailable() && (parent == nullptr || parent->IsAvailable());
}

void ibValueControl::SetOwnerForm(ibValueForm* ownerForm)
{
	// Just record the owner. The form derives its control list by walking the
	// hierarchy (ibValueForm::GetControlList) — there is no maintained set, so this
	// no longer touches the form. Removes the teardown hazard where ~ibValueControl
	// erased from an already-destroyed m_listControl.
	m_formOwner = ownerForm;
}

const ibMetaData* ibValueControl::GetMetaData() const
{
	const ibValueMetaObjectFormBase* metaFormObject = m_formOwner ?
		m_formOwner->GetFormMetaObject() : nullptr;

	//for form buider
	if (metaFormObject == nullptr) {

		ibSourceDataObject* srcValue = m_formOwner ?
			m_formOwner->GetSourceObject() :
			nullptr;
		
		if (srcValue != nullptr)
			return srcValue->GetSourceMetaData();
	}

	return metaFormObject ?
		metaFormObject->GetMetaData() :
		nullptr;
}

#include "backend/metaCollection/metaFormObject.h"

ibFormID ibValueControl::GetTypeForm() const
{
	if (m_formOwner == nullptr) {
		wxASSERT(m_formOwner);
		return 0;
	}

	const ibValueMetaObjectFormBase* creator = m_formOwner->GetFormMetaObject();
	if (creator != nullptr)
		return creator->GetTypeForm();
	return m_formOwner->GetTypeForm();
}

void ibValueControl::AutoBindNewSource(ibTypeControlFactory* factory)
{
	// Called by the source controls (checkbox / textbox / tablebox) from their CREATION event when the
	// first-created flag is set. A just-added control with no source provisions a fresh attribute NAMED
	// after this control and binds to it.
	//
	// ⭐ THE TYPE IS THE ONE THE CONTROL ALREADY DECLARES. A control that accepts particular types says
	// so in its own type description (a gridbox: spreadsheet document or composition) — that is what the
	// picker offers, so it is also what a provisioned attribute should BE. Asking a second question
	// ("and what is your default type?") was one more place to keep in step with the first, and it
	// answered for exactly one control.
	//
	// Where a control declares nothing, the shared filter-kind -> clsid mapping stands
	// (ibBackendTypeConfigFactory::GetDefaultTypeByFilter) — the SAME mapping
	// ibVariantDataAttribute::DoSetDefaultMetaType uses, so a control's auto-attribute and a variant's
	// default never diverge.
	if (factory == nullptr || m_formOwner == nullptr || !IsUnbound())
		return;
	const ibTypeDescription& declared = factory->GetTypeDesc();
	ibTypeDescription typeDesc;
	typeDesc.SetDefaultMetaType(declared.GetClsidCount() > 0
		? declared.GetFirstClsid()
		: ibBackendTypeConfigFactory::GetDefaultTypeByFilter(factory->GetFilterDataType()));
	const ibMetaID id = m_formOwner->AddAutoAttribute(GetControlName(), typeDesc);
	if (id != wxNOT_FOUND)
		factory->SetDefaultSourceType(id);   // one-hop bind, over the mutable GetSourceDesc (twin of SetDefaultMetaType over GetTypeDesc)
}

#include "backend/serialize/dataBuilder.h"                 // ibDataNode — the options list saved with the element

bool ibValueControl::ReadData(const ibDataNode& node)
{
	m_propertyFunctionalOptions->SetNodeValue(node.GetProperty(m_propertyFunctionalOptions->GetName()));
	return ibValueFrame::ReadData(node);
}

bool ibValueControl::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyFunctionalOptions->GetName(), m_propertyFunctionalOptions->GetNodeValue());
	return ibValueFrame::WriteData(node);
}