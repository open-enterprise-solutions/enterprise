#include "variantTemplate.h"

// What it reads as — its kind.
ibString ibVariantTemplate::GetString() const
{
	if (m_templateDesc.GetType() == ibTemplateType::Spreadsheet)
		return _("Spreadsheet document");
	if (m_templateDesc.GetType() == ibTemplateType::Composer)
		return _("Data composer");
	return ibString();
}
