////////////////////////////////////////////////////////////////////////////
//	Description : the type picker over a field holding a type description — see typeSelector.h
////////////////////////////////////////////////////////////////////////////

#include "typeSelector.h"

#include <memory>
#include <optional>

#include "backend/backend_mainFrame.h"                                   // ibBackendDocFrame::Request — the client's window
#include "backend/session/session.h"
#include "backend/system/value/valueType.h"                              // ibValueTypeDescription — what the field holds
#include "core/serialize/dataBuilder.h"
#include "frmserver/visualView/ctrl/frame.h"                             // ibControlFrame::ChoiceProcessing
#include "frmserver/win/dlgs/settings/settingsRequest.h"                 // ibWriteSettingsClasses — the offer, named
#include "protocol/protocol.h"                                           // ibProtocol::ibRequest::ibTypeSelector

bool ibShowTypeSelector(ibControlFrame* ownerValue, const ibTypeDescription& offered, const ibMetaData* metaData)
{
	ibValue current;
	if (ownerValue == nullptr || offered.GetClsidCount() == 0 || !ownerValue->GetControlValue(current))
		return false;
	ibValueTypeDescription* held = nullptr;
	if (!current.ConvertToValue(held) || held == nullptr)
		return false;

	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return false;

	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::TypeSelector));
	ibDataValue now;
	ibTypeDescriptionMemory::WriteNode(now, held->m_typeDesc);
	request.SetProperty(ibProtocol::ibRequest::ibTypeSelector::Value, now);
	ibWriteSettingsClasses(request.Child(ibProtocol::ibRequest::ibTypeSelector::Classes), { offered }, metaData);

	ibDataNode response;
	if (frame->Request(request, response)) {
		if (const std::optional<ibDataNode> chosen = response.FindChild(ibProtocol::ibRequest::ibTypeSelector::Value)) {
			ibTypeDescription edited;
			if (ibTypeDescriptionMemory::ReadNode(ibDataValue::Child(std::make_shared<ibDataNode>(*chosen)), edited)) {
				ibValue value(new ibValueTypeDescription(edited));
				ownerValue->ChoiceProcessing(value);
			}
		}
	}
	return true;   // the window was shown, whatever the person did with it
}
