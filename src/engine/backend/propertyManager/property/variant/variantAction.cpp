#include "variantAction.h"

ibString ibVariantAction::GetString() const
{
	return m_actionData.GetCustomAction();
}