#include "propertyForm.h"
#include "backend/propertyManager/property/variant/variantForm.h"
#include "backend/serialize/dataBuilder.h"          // ibDataValue / ibDataNode
#include "backend/metaCollection/metaFormObject.h"  // ibValueMetaObjectFormBase — the form-blob<->node shim

#define chunkForm 0x023456543


////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyForm::CreateVariantData()
{
	return new ibVariantDataForm();
}

////////////////////////////////////////////////////////////////////////

wxString ibPropertyForm::GetValueAsString() const
{
	return get_cell_variant<ibVariantDataForm>()->GetModuleText();
}

wxMemoryBuffer& ibPropertyForm::GetValueAsMemoryBuffer() const
{
	return get_cell_variant<ibVariantDataForm>()->GetFormData();
}

void ibPropertyForm::SetValue(const wxString& val)
{
	get_cell_variant<ibVariantDataForm>()->SetModuleText(val);
}

void ibPropertyForm::SetValue(const wxMemoryBuffer& val)
{
	get_cell_variant<ibVariantDataForm>()->SetFormData(val);
}

////////////////////////////////////////////////////////////////////////

//base property for "form"
bool ibPropertyForm::SetDataValue(const ibValue& varPropVal)
{
	return false;
}

bool ibPropertyForm::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = GetName();
	return true;
}

// node form: a Child { Layout: <control tree, Child>, Module: <module text, String> }.
// The Layout node comes straight from the form-blob<->node shim — the stored cell keeps
// its runtime blob, the metadata sees a transparent control-tree subtree.
bool ibPropertyForm::ReadNodeValue(const ibDataValue& value)
{
	const std::shared_ptr<ibDataNode>& root = value.AsChild();
	if (root) {
		ibPropertyForm::SetValue(ibValueMetaObjectFormBase::FormNodeToBlob(root->GetProperty(wxT("Layout"))));
		ibPropertyForm::SetValue(root->GetValue<wxString>(wxT("Module")));
	}
	return true;
}

bool ibPropertyForm::WriteNodeValue(ibDataValue& value) const
{
	auto root = std::make_shared<ibDataNode>();
	root->SetProperty(wxT("Layout"), ibValueMetaObjectFormBase::FormBlobToNode(GetValueAsMemoryBuffer()));
	root->SetValue(wxT("Module"),    GetValueAsString());
	value = ibDataValue::Child(root);
	return true;
}

//////////////////////////////////////////////////////////

// clipboard copy pulls the LIVE form (CopyFormData → control tree node), not the stored
// buffer — the one case where CopyNodeValue differs from WriteNodeValue.
bool ibPropertyForm::CopyNodeValue(ibDataValue& value) const
{
	ibValueMetaObjectFormBase* metaForm = dynamic_cast<ibValueMetaObjectFormBase*>(m_owner);
	if (metaForm == nullptr) return false;
	auto root = std::make_shared<ibDataNode>();
	root->SetProperty(wxT("Layout"), metaForm->CopyFormData());
	root->SetValue(wxT("Module"),    GetValueAsString());
	value = ibDataValue::Child(root);
	return true;
}

bool ibPropertyForm::PasteNodeValue(const ibDataValue& value)
{
	ibValueMetaObjectFormBase* metaForm = dynamic_cast<ibValueMetaObjectFormBase*>(m_owner);
	if (metaForm == nullptr) return false;
	const std::shared_ptr<ibDataNode>& root = value.AsChild();
	if (root) {
		ibPropertyForm::SetValue(ibValueMetaObjectFormBase::FormNodeToBlob(root->GetProperty(wxT("Layout"))));
		ibPropertyForm::SetValue(root->GetValue<wxString>(wxT("Module")));
	}
	return metaForm->PasteFormData();
}

//////////////////////////////////////////////////////////