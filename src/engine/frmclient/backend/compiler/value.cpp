#include "value.h"

#include <map>
#include <memory>

#include "frmclient/backend/system/value/composition/valueComposerField.h"   // a field of a composition, read back as one
#include "frmclient/backend/system/value/valueColour.h"   // …an appearance's colour
#include "frmclient/backend/system/value/valueFont.h"     // …and its font
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/picture.h"
#include "protocol/protocol.h"

bool ibValueSerializable::CompareValueEQ(const ibVariant& value) const
{
	const ibValueSerializable* const other = value.ConvertToType<ibValueSerializable>();
	if (other == this)
		return true;
	if (other == nullptr || other->m_classType != m_classType)
		return false;
	const auto left = m_node.FindField(kValueFieldData);
	const auto right = other->m_node.FindField(kValueFieldData);
	return (left ? left->AsString() : ibString()) == (right ? right->AsString() : ibString());
}

// An object of the client's own, of its class, read out of the node it wrote itself into.
template <class T>
static ibValue ReadObject(const ibDataNode& node)
{
	T* const made = new T();
	ibValue held(made);
	made->Deserialize(node);
	return held;
}

ibValue ibValue::FromNode(const ibDataNode& node)
{
	unsigned long long parsed = 0;
	const ibClassID clsid = node.GetValue<ibString>(kValueFieldClsid).ToULongLong(&parsed) && parsed != 0
		? static_cast<ibClassID>(parsed) : g_valueUndefinedCLSID;

	// A PRIMITIVE — the variant's own reading of it.
	if (GetVTByID(clsid) != ibValueTypes::TYPE_VALUE)
		return ibValue(ibVariant::FromNode(node));

	// A FIELD OF A COMPOSITION — the client's own object (a condition's other side may be one): made and read, as the
	// engine's registry makes it. …and an appearance's colour and font, the same way.
	if (clsid == g_compositionFieldCLSID)
		return ReadObject<ibValueCompositionField>(node);
	if (clsid == ibValueColour().GetClassType())
		return ReadObject<ibValueColour>(node);
	if (clsid == ibValueFont().GetClassType())
		return ReadObject<ibValueFont>(node);

	// A type of the server's — kept as it was written, to be given back.
	return ibValue(new ibValueSerializable(clsid, node,
		node.GetValue<ibString>(ibProtocol::ibRequest::ibListSettings::Presentation)));
}

bool ibValue::FindValue(const ibString& findData, std::vector<ibValue>& listValue) const
{
	if (m_pRef != nullptr && IsReference()) {
		const ibValue* held = dynamic_cast<const ibValue*>(GetRef());   // an object held here is the client's own value
		return held != nullptr && held->FindValue(findData, listValue);
	}

	try {
		if (m_typeClass == ibValueTypes::TYPE_BOOLEAN) {
			ibValue cFounded;
			cFounded.SetBoolean(findData);
			listValue.emplace_back(cFounded);
			if (cFounded.GetBoolean()) listValue.emplace_back(false);
			else listValue.emplace_back(true);
			return true;
		}
		else if (m_typeClass == ibValueTypes::TYPE_NUMBER) {
			listValue.emplace_back().SetNumber(findData);
			return true;
		}
		else if (m_typeClass == ibValueTypes::TYPE_DATE) {
			listValue.emplace_back().SetDate(findData);
			return true;
		}
		else if (m_typeClass == ibValueTypes::TYPE_STRING) {
			listValue.emplace_back().SetString(findData);
			return true;
		}
	}
	catch (...) {
		return false;
	}

	return false;
}

namespace {
std::map<ibClassID, std::unique_ptr<ibCtorAbstractType>>& NamedTypes()
{
	static std::map<ibClassID, std::unique_ptr<ibCtorAbstractType>> s_types;
	return s_types;
}
} // namespace

ibCtorAbstractType* ibValue::GetAvailableCtor(const ibClassID& clsid)
{
	const auto found = NamedTypes().find(clsid);
	return found != NamedTypes().end() ? found->second.get() : nullptr;
}

void ibValue::RegisterCtor(const ibClassID& clsid, const ibString& className, const wxIcon& classIcon)
{
	if (clsid == 0 || className.IsEmpty())
		return;
	// A type the server drew no picture of wears a value's own (value_res.cpp): a list of types puts up a picture for
	// each, and an empty one is refused.
	NamedTypes()[clsid] = std::make_unique<ibCtorAbstractType>(className, classIcon.IsOk() ? classIcon : GetIconGroup());
}

std::vector<ibCtorAbstractType*> ibValue::GetListCtorsByType(ibCtorObjectType objectType)
{
	// The question pending names them — a form editor's Classes, its controls'. Kept until the next ask, as the engine's
	// registry keeps its own for the run.
	static std::vector<std::unique_ptr<ibCtorAbstractType>> s_ctors;
	s_ctors.clear();

	std::vector<ibCtorAbstractType*> listed;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || objectType != ibCtorObjectType::ibCtorObjectType_object_control)
		return listed;

	for (const ibProtocolNode& described : frame->GetRequest().FindChild(ibProtocol::ibRequest::ibFormEditor::Classes).Children()) {
		wxIcon icon;
		const wxBitmap picture = ibProtocolPicture(described, ibProtocol::ibRequest::ibFormEditor::Picture);
		if (picture.IsOk())
			icon.CopyFromBitmap(picture);
		s_ctors.push_back(std::make_unique<ibCtorAbstractType>(described.GetString(ibProtocol::ibRequest::ibFormEditor::Name), icon));
		listed.push_back(s_ctors.back().get());
	}
	return listed;
}
