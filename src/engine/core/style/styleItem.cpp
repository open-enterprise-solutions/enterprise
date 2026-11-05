#include "styleItem.h"

#include <string>

namespace {

// Constant from the start: no constructor runs for it and none tears it down, so it answers at static teardown too.
constexpr ibStyleItemInfo s_items[] = {
	{ ibStyleItem::FormBackground,        ibStyleKind::Colour,     "FormBackground" },
	{ ibStyleItem::FormText,              ibStyleKind::Colour,     "FormText" },
	{ ibStyleItem::ButtonBackground,      ibStyleKind::Colour,     "ButtonBackground" },
	{ ibStyleItem::ButtonText,            ibStyleKind::Colour,     "ButtonText" },
	{ ibStyleItem::FieldBackground,       ibStyleKind::Colour,     "FieldBackground" },
	{ ibStyleItem::FieldText,             ibStyleKind::Colour,     "FieldText" },
	{ ibStyleItem::Selection,             ibStyleKind::Colour,     "Selection" },
	{ ibStyleItem::SelectionText,         ibStyleKind::Colour,     "SelectionText" },
	{ ibStyleItem::AlternateBackground,   ibStyleKind::Colour,     "AlternateBackground" },
	{ ibStyleItem::TooltipBackground,     ibStyleKind::Colour,     "TooltipBackground" },
	{ ibStyleItem::TooltipText,           ibStyleKind::Colour,     "TooltipText" },
	{ ibStyleItem::SpecialText,           ibStyleKind::Colour,     "SpecialText" },
	{ ibStyleItem::NegativeNumber,        ibStyleKind::Colour,     "NegativeNumber" },
	{ ibStyleItem::Border,                ibStyleKind::Colour,     "Border" },
	{ ibStyleItem::TableHeaderBackground, ibStyleKind::Colour,     "TableHeaderBackground" },
	{ ibStyleItem::TableHeaderText,       ibStyleKind::Colour,     "TableHeaderText" },
	{ ibStyleItem::TableFooterBackground, ibStyleKind::Colour,     "TableFooterBackground" },
	{ ibStyleItem::TableFooterText,       ibStyleKind::Colour,     "TableFooterText" },
	{ ibStyleItem::ReportHeader,          ibStyleKind::Colour,     "ReportHeader" },
	{ ibStyleItem::ReportGroup,           ibStyleKind::ColourList, "ReportGroup" },
	{ ibStyleItem::ReportLine,            ibStyleKind::Colour,     "ReportLine" },
	{ ibStyleItem::Activity,              ibStyleKind::Colour,     "Activity" },
	{ ibStyleItem::Accent,                ibStyleKind::Colour,     "Accent" },
	{ ibStyleItem::Navigation,            ibStyleKind::Colour,     "Navigation" },
	{ ibStyleItem::SecondaryNavigation,   ibStyleKind::Colour,     "SecondaryNavigation" },
	{ ibStyleItem::Important,             ibStyleKind::Colour,     "Important" },
	{ ibStyleItem::TextFont,              ibStyleKind::Font,       "TextFont" },
	{ ibStyleItem::SmallFont,             ibStyleKind::Font,       "SmallFont" },
	{ ibStyleItem::RegularFont,           ibStyleKind::Font,       "RegularFont" },
	{ ibStyleItem::LargeFont,             ibStyleKind::Font,       "LargeFont" },
	{ ibStyleItem::VeryLargeFont,         ibStyleKind::Font,       "VeryLargeFont" },
	{ ibStyleItem::ControlBorder,         ibStyleKind::Border,     "ControlBorder" },
	{ ibStyleItem::ButtonBorder,          ibStyleKind::Border,     "ButtonBorder" },
};

} // namespace

std::span<const ibStyleItemInfo> ibStyleItemInfo::All()
{
	return s_items;
}

const ibStyleItemInfo* ibStyleItemInfo::Of(ibStyleItem item)
{
	for (const ibStyleItemInfo& info : s_items)
		if (info.m_item == item)
			return &info;
	return nullptr;
}

const ibStyleItemInfo* ibStyleItemInfo::Of(const ibString& name)
{
	const std::string asked = name.ToUtf8();   // once, not an ibString per item
	for (const ibStyleItemInfo& info : s_items)
		if (asked == info.m_name)
			return &info;
	return nullptr;
}
