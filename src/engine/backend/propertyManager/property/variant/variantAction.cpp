#include "variantAction.h"

ibString ibPropertyValueAction::GetString() const
{
	return m_actionData.GetCustomAction();
}