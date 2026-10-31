#include "propertyForm.h"
#include "backend/propertyManager/property/variant/variantForm.h"
#include "core/serialize/dataBuilder.h"          // ibDataValue / ibDataNode
#include "backend/metaCollection/metaFormObject.h"  // ibValueMetaObjectFormBase — the form-blob<->node shim

#define chunkForm 0x023456543


////////////////////////////////////////////////////////////////////////

ibVariant ibPropertyForm::CreateVariantData()
{
	return ibVariant(new ibVariantForm());
}

////////////////////////////////////////////////////////////////////////

ibString ibPropertyForm::GetValueAsString() const
{
	return get_cell_variant<ibVariantForm>()->GetModuleText();
}

ibMemoryBuffer& ibPropertyForm::GetValueAsMemoryBuffer() const
{
	return get_cell_variant<ibVariantForm>()->GetFormData();
}

void ibPropertyForm::SetValue(const ibString& val)
{
	get_cell_variant<ibVariantForm>()->SetModuleText(val);
}

void ibPropertyForm::SetValue(const ibMemoryBuffer& val)
{
	get_cell_variant<ibVariantForm>()->SetFormData(val);
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
	const auto root = value.AsChild();
	if (root) {
		ibPropertyForm::SetValue(ibValueMetaObjectFormBase::FormNodeToBlob(root->GetProperty(ibT("Layout"))));
		ibPropertyForm::SetValue(root->GetValue<ibString>(ibT("Module")));
	}
	return true;
}

bool ibPropertyForm::WriteNodeValue(ibDataValue& value) const
{
	auto root = std::make_shared<ibDataNode>();
	root->SetProperty(ibT("Layout"), ibValueMetaObjectFormBase::FormBlobToNode(GetValueAsMemoryBuffer()));
	root->SetValue(ibT("Module"),    GetValueAsString());
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
	root->SetProperty(ibT("Layout"), metaForm->CopyFormData());
	root->SetValue(ibT("Module"),    GetValueAsString());
	value = ibDataValue::Child(root);
	return true;
}

bool ibPropertyForm::PasteNodeValue(const ibDataValue& value)
{
	ibValueMetaObjectFormBase* metaForm = dynamic_cast<ibValueMetaObjectFormBase*>(m_owner);
	if (metaForm == nullptr) return false;
	const auto root = value.AsChild();
	if (root) {
		ibPropertyForm::SetValue(ibValueMetaObjectFormBase::FormNodeToBlob(root->GetProperty(ibT("Layout"))));
		ibPropertyForm::SetValue(root->GetValue<ibString>(ibT("Module")));
	}
	return metaForm->PasteFormData();
}

#include "protocol/protocol.h"   // ibProtocol::ibSchema::ibProperties::Object — what the link opens

bool ibPropertyForm::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;
	row.SetValue(ibProtocol::ibSchema::ibProperties::Object, static_cast<std::int32_t>(static_cast<const ibValueMetaObject*>(m_owner)->GetMetaID()));
	return true;
}

//////////////////////////////////////////////////////////