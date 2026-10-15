#include "variantAction.h"

wxString ibVariantDataAction::MakeString() const
{
	return m_actionData.GetCustomAction();
}
