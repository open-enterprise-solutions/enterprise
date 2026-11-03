#include "templateDescription.h"

#include "core/exception.h"                 // ibCoreException — the other kind's schema asked for
#include "core/serialize/dataBuilder.h"     // ibDataNode / ibDataValue
#include "core/localization/dictionary.h"   // _()

const ibChar* const kTemplateType = ibT("TemplateType");

ibTemplateDescription::ibTemplateDescription(ibTemplateType type)
{
	if (type == ibTemplateType::Spreadsheet)
		m_data.emplace<ibSpreadsheetDescription>();
	else if (type == ibTemplateType::Composer)
		m_data.emplace<ibCompositionDescription>();
}

ibTemplateType ibTemplateDescription::GetType() const
{
	if (std::holds_alternative<ibSpreadsheetDescription>(m_data))
		return ibTemplateType::Spreadsheet;
	if (std::holds_alternative<ibCompositionDescription>(m_data))
		return ibTemplateType::Composer;
	return ibTemplateType::None;
}

ibSpreadsheetDescription& ibTemplateDescription::GetSpreadsheetDesc()
{
	ibSpreadsheetDescription* spreadsheetDesc = std::get_if<ibSpreadsheetDescription>(&m_data);
	if (spreadsheetDesc == nullptr)
		ibCoreException::Error(_("The template is not a spreadsheet document"));
	return *spreadsheetDesc;
}

const ibSpreadsheetDescription& ibTemplateDescription::GetSpreadsheetDesc() const
{
	const ibSpreadsheetDescription* spreadsheetDesc = std::get_if<ibSpreadsheetDescription>(&m_data);
	if (spreadsheetDesc == nullptr)
		ibCoreException::Error(_("The template is not a spreadsheet document"));
	return *spreadsheetDesc;
}

ibCompositionDescription& ibTemplateDescription::GetCompositionDesc()
{
	ibCompositionDescription* compositionDesc = std::get_if<ibCompositionDescription>(&m_data);
	if (compositionDesc == nullptr)
		ibCoreException::Error(_("The template is not a data composer"));
	return *compositionDesc;
}

const ibCompositionDescription& ibTemplateDescription::GetCompositionDesc() const
{
	const ibCompositionDescription* compositionDesc = std::get_if<ibCompositionDescription>(&m_data);
	if (compositionDesc == nullptr)
		ibCoreException::Error(_("The template is not a data composer"));
	return *compositionDesc;
}

////////////////////////////////////////////////////////////////////////

// ⚠ A KIND THIS VERSION DOES NOT KNOW IS REFUSED — a template of a newer one read as nothing would be saved back
// as nothing.
bool ibTemplateDescriptionMemory::ReadNode(const ibDataValue& value, ibTemplateDescription& templateDesc)
{
	std::int32_t type = static_cast<std::int32_t>(ibTemplateType::Spreadsheet);
	if (const auto node = value.AsChild())
		node->GetValue<std::int32_t>(kTemplateType, type);

	templateDesc = ibTemplateDescription(static_cast<ibTemplateType>(type));
	if (templateDesc.GetType() == ibTemplateType::Spreadsheet)
		return ibSpreadsheetDescriptionMemory::ReadNode(value, templateDesc.GetSpreadsheetDesc());
	if (templateDesc.GetType() == ibTemplateType::Composer)
		return ibCompositionDescriptionMemory::ReadNode(value, templateDesc.GetCompositionDesc());

	ibCoreException::Error(_("The template's kind %i is not one this version knows"), static_cast<int>(type));
}

bool ibTemplateDescriptionMemory::WriteNode(ibDataValue& value, const ibTemplateDescription& templateDesc)
{
	bool written = false;
	if (templateDesc.GetType() == ibTemplateType::Spreadsheet)
		written = ibSpreadsheetDescriptionMemory::WriteNode(value, templateDesc.GetSpreadsheetDesc());
	else if (templateDesc.GetType() == ibTemplateType::Composer)
		written = ibCompositionDescriptionMemory::WriteNode(value, templateDesc.GetCompositionDesc());

	std::optional<ibDataNode> node = written ? value.AsChild() : std::nullopt;
	if (!node)
		return false;

	node->SetValue<std::int32_t>(kTemplateType, static_cast<std::int32_t>(templateDesc.GetType()));
	value = ibDataValue::Child(std::make_shared<ibDataNode>(std::move(*node)));
	return true;
}
