#include "metaObjectComposite.h"
#include "backend/metaData.h"

//***********************************************************************
//*							ibPropertyPredefinedType			        *
//***********************************************************************

#include "core/description/typeDescription.h"   // ibTypeDescriptionMemory — the type written in the attribute's node
#include "backend/backend_picture.h"            // ibBackendPicture::SendPicture — a type offered, drawn
#include "protocol/protocolDesigner.h"                  // ibProtocol::ibSchema::ibProperties — the row's keys

bool ibPropertyPredefinedType::ReadNodeValue(const ibDataValue& value)
{
	ibTypeDescription chosen;
	if (!ibTypeDescriptionMemory::ReadNode(value, chosen))
		return false;
	if (chosen.GetClsidCount() > 1 || (chosen.GetClsidCount() == 1
		&& std::find(m_offered.begin(), m_offered.end(), chosen.GetFirstClsid()) == m_offered.end()))
		return false;
	if (!ibPropertyPredefined<>::ReadNodeValue(value))
		return false;
	if (chosen.GetClsidCount() == 1)
		GetMetaObject()->GetTypeDesc() = chosen;
	return true;
}

bool ibPropertyPredefinedType::WriteNodeValue(ibDataValue& value) const
{
	auto child = std::make_shared<ibDataNode>();
	GetMetaObject()->SaveNode(*child);
	ibDataValue type;
	ibTypeDescriptionMemory::WriteNode(type, GetMetaObject()->GetTypeDesc());
	if (const auto fields = type.AsChild())
		for (const auto& field : fields->Fields())
			child->SetField(field.first, field.second);
	value = ibDataValue::Child(child);
	return true;
}

bool ibPropertyPredefinedType::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;
	// …read as the type's name, as a type property's row is: the holder itself has no value to say.
	const ibTypeDescription& typeDesc = GetMetaObject()->GetTypeDesc();
	if (const ibCtorAbstractType* const held = typeDesc.GetClsidCount() == 1 ? ibValue::GetAvailableCtor(typeDesc.GetFirstClsid()) : nullptr)
		row.SetField(ibProtocol::ibSchema::ibProperties::Presentation, ibDataValue::String(held->GetClassName()));
	row.SetValue(ibProtocol::ibSchema::ibProperties::Multiple, false);
	for (const ibClassID clsid : m_offered) {
		const ibCtorAbstractType* const so = ibValue::GetAvailableCtor(clsid);
		if (so == nullptr)
			continue;
		ibDataNode& type = row.AddChild(0, static_cast<ibMetaID>(row.Children().size() + 1));
		type.SetValue(ibProtocol::ibSchema::ibProperties::Type, ibString::Format(ibT("%llu"), static_cast<unsigned long long>(clsid)));
		type.SetValue(ibProtocol::ibSchema::ibProperties::Title, so->GetClassName());
		type.SetValue(ibProtocol::ibSchema::ibProperties::Picture, ibBackendPicture::SendPicture(clsid));
	}
	return true;
}

//***********************************************************************
//*							ibValueMetaObjectCompositeData				        *
//***********************************************************************

const ibCtorMetaValueType* ibValueMetaObjectCompositeData::GetTypeCtor(const ibCtorObjectMetaType& refType) const
{
	return m_metaData->GetTypeCtor(this, refType);
}