#include "style.h"

#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

namespace {

// The kind a value is of, by the alternative it holds - in ibStyleValue's order, which these hold.
static_assert(std::is_same_v<std::variant_alternative_t<0, ibStyleValue>, ibColour>);
static_assert(std::is_same_v<std::variant_alternative_t<1, ibStyleValue>, std::vector<ibColour>>);
static_assert(std::is_same_v<std::variant_alternative_t<2, ibStyleValue>, ibFont>);
static_assert(std::is_same_v<std::variant_alternative_t<3, ibStyleValue>, ibBorder>);

ibStyleKind KindOf(const ibStyleValue& value)
{
	switch (value.index()) {
	case 0: return ibStyleKind::Colour;
	case 1: return ibStyleKind::ColourList;
	case 2: return ibStyleKind::Font;
	default: return ibStyleKind::Border;
	}
}

ibDataValue TextOf(const ibStyleValue& value)
{
	if (const ibColour* colour = std::get_if<ibColour>(&value))
		return ibDataValue::String(colour->ToString());
	if (const std::vector<ibColour>* list = std::get_if<std::vector<ibColour>>(&value)) {
		std::vector<ibDataValue> texts;
		for (const ibColour& colour : *list)
			texts.push_back(ibDataValue::String(colour.ToString()));
		return ibDataValue::Array(std::move(texts));
	}
	if (const ibFont* font = std::get_if<ibFont>(&value))
		return ibDataValue::String(font->ToString());
	return ibDataValue::String(std::get<ibBorder>(value).ToString());
}

// A value of the kind read from its text - false when the text names none.
bool ValueOf(ibStyleKind kind, const ibDataValue& text, ibStyleValue& value)
{
	if (kind == ibStyleKind::ColourList) {
		if (text.Kind() != ibDataKind::Array)
			return false;
		std::vector<ibColour> list;
		for (const ibDataValue& one : text.AsArray()) {
			ibColour colour;
			if (one.Kind() != ibDataKind::String || !colour.FromString(one.AsString()))
				return false;
			list.push_back(colour);
		}
		value = std::move(list);
		return true;
	}
	if (text.Kind() != ibDataKind::String)
		return false;
	const ibString read = text.AsString();
	switch (kind) {
	case ibStyleKind::Colour: {
		ibColour colour;
		if (!colour.FromString(read))
			return false;
		value = colour;
		return true;
	}
	case ibStyleKind::Font: {
		ibFont font;
		if (!font.FromString(read) || !font.IsOk())
			return false;
		value = font;
		return true;
	}
	default: {
		ibBorder border;
		if (!border.FromString(read))
			return false;
		value = border;
		return true;
	}
	}
}

// Whether a value's text holds it whole - what Set takes and ReadNode keeps: the value written as its text and read
// back is the value. The text decides, so nothing here can drift from what the text holds. A font's size is bounded
// first (an int is made of it), and a list is at least one colour.
bool Holdable(const ibStyleValue& value)
{
	if (const ibFont* font = std::get_if<ibFont>(&value))
		if (!(font->m_pointSize > 0 && font->m_pointSize <= ibStyleValues::kMaxPoints))
			return false;
	if (const std::vector<ibColour>* list = std::get_if<std::vector<ibColour>>(&value))
		if (list->empty())
			return false;
	ibStyleValue back;
	return ValueOf(KindOf(value), TextOf(value), back) && back == value;
}

ibColour Rgb(unsigned int rgb)
{
	return ibColour(static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8),
		static_cast<unsigned char>(rgb));
}

ibFont Font(double points)
{
	ibFont font;
	font.m_pointSize = points;
	return font;
}

} // namespace

const ibStyleValue* ibStyleValues::Find(ibStyleItem item) const
{
	const auto found = m_values.find(item);
	return found != m_values.end() ? &found->second : nullptr;
}

bool ibStyleValues::Set(ibStyleItem item, const ibStyleValue& value)
{
	const ibStyleItemInfo* const info = ibStyleItemInfo::Of(item);
	if (info == nullptr || info->m_kind != KindOf(value) || !Holdable(value))
		return false;
	m_values[item] = value;
	return true;
}

bool ibStyleValues::ReadNode(const ibDataValue& value)
{
	m_values.clear();
	if (value.Kind() != ibDataKind::Child)
		return false;
	const auto node = value.AsChild();
	if (!node)
		return false;
	for (const auto& [name, text] : node->Fields()) {
		const ibStyleItemInfo* const info = ibStyleItemInfo::Of(name);
		ibStyleValue read;
		if (info != nullptr && ValueOf(info->m_kind, text, read) && Holdable(read))
			m_values[info->m_item] = std::move(read);
	}
	return true;
}

bool ibStyleValues::WriteNode(ibDataValue& value) const
{
	auto node = std::make_shared<ibDataNode>();
	for (const auto& [item, held] : m_values)
		node->AddField(ibString(ibStyleItemInfo::Of(item)->m_name), TextOf(held));
	value = ibDataValue::Child(node);
	return true;
}

const ibStyleValue& ibStyle::Get(ibStyleItem item, bool dark) const
{
	if (const ibStyleValue* const own = (dark ? m_dark : m_light).Find(item))
		return *own;
	const ibStyle& platform = Platform();
	if (const ibStyleValue* const given = (dark ? platform.m_dark : platform.m_light).Find(item))
		return *given;
	static const ibStyleValue* const s_none = new ibStyleValue();   // never torn down, as the platform's
	return *s_none;
}

bool ibStyle::ReadNode(const ibDataValue& value)
{
	m_light = ibStyleValues();
	m_dark = ibStyleValues();
	if (value.Kind() != ibDataKind::Child)
		return false;
	const auto node = value.AsChild();
	if (!node)
		return false;
	if (const auto light = node->FindField(ibT("Light")))
		m_light.ReadNode(*light);
	if (const auto dark = node->FindField(ibT("Dark")))
		m_dark.ReadNode(*dark);
	return true;
}

bool ibStyle::WriteNode(ibDataValue& value) const
{
	auto node = std::make_shared<ibDataNode>();
	ibDataValue light, dark;
	m_light.WriteNode(light);
	m_dark.WriteNode(dark);
	node->AddField(ibT("Light"), light);
	node->AddField(ibT("Dark"), dark);
	value = ibDataValue::Child(node);
	return true;
}

const ibStyle& ibStyle::Platform()
{
	static const ibStyle* const s_platform = [] {
		ibStyle* const made = new ibStyle();   // never torn down: a client may ask it while the process ends
		ibStyle& style = *made;
		// Each colour item: its light value and its dark one. The light look keeps what the platform painted with - the
		// forms' ink on cream (frmserver's default style), the Luna theme's chrome, border, accent and button colours
		// (frmclient/uikit/theme/lunaTheme.cpp); the items it had no colour for, and the whole dark look, are new.
		const struct { ibStyleItem item; unsigned int light; unsigned int dark; } colours[] = {
			{ ibStyleItem::FormBackground,        0xFAF7F0, 0x22262B },
			{ ibStyleItem::FormText,              0x3F5C77, 0xC9D6E2 },
			{ ibStyleItem::ButtonBackground,      0xC8D6DF, 0x2C3036 },
			{ ibStyleItem::ButtonText,            0x000000, 0xE4E6EA },
			{ ibStyleItem::FieldBackground,       0xFAF7F0, 0x1B1D21 },
			{ ibStyleItem::FieldText,             0x000000, 0xE4E6EA },
			{ ibStyleItem::Selection,             0xD97757, 0xB85A38 },
			{ ibStyleItem::SelectionText,         0xFFFFFF, 0xFFFFFF },
			{ ibStyleItem::AlternateBackground,   0xF7F8F9, 0x282B30 },
			{ ibStyleItem::TooltipBackground,     0xFFFFE1, 0x30343A },
			{ ibStyleItem::TooltipText,           0x222222, 0xE4E6EA },
			{ ibStyleItem::SpecialText,           0x3B50A0, 0x8197E8 },
			{ ibStyleItem::NegativeNumber,        0xC0392B, 0xE5675A },
			{ ibStyleItem::Border,                0xA8BAC8, 0x3A3F46 },
			{ ibStyleItem::TableHeaderBackground, 0xF2F3F4, 0x2C3036 },
			{ ibStyleItem::TableHeaderText,       0x222222, 0xE4E6EA },
			{ ibStyleItem::TableFooterBackground, 0xF2F3F4, 0x2C3036 },
			{ ibStyleItem::TableFooterText,       0x222222, 0xE4E6EA },
			{ ibStyleItem::ReportHeader,          0xC5DCE8, 0x2A3D4B },
			{ ibStyleItem::ReportLine,            0xD9D9D9, 0x3A3F46 },
			{ ibStyleItem::Activity,              0xD97757, 0xE58A6C },
			{ ibStyleItem::Accent,                0xD97757, 0xE58A6C },
			{ ibStyleItem::Navigation,            0xB8C9D4, 0x1F2A33 },
			{ ibStyleItem::SecondaryNavigation,   0xC8D6DF, 0x27333D },
			{ ibStyleItem::Important,             0xC0392B, 0xE5675A },
		};
		for (const auto& colour : colours) {
			style.m_light.Set(colour.item, Rgb(colour.light));
			style.m_dark.Set(colour.item, Rgb(colour.dark));
		}
		// Three grouping levels of a report, the outermost the strongest.
		style.m_light.Set(ibStyleItem::ReportGroup, std::vector<ibColour>{ Rgb(0xE8EEF3), Rgb(0xF1F5F8), Rgb(0xF8FAFB) });
		style.m_dark.Set(ibStyleItem::ReportGroup, std::vector<ibColour>{ Rgb(0x2F3A44), Rgb(0x2A333B), Rgb(0x262D33) });
		// The fonts are the same in both looks: the family's default face, by size.
		const struct { ibStyleItem item; double points; } fonts[] = {
			{ ibStyleItem::TextFont, 9 }, { ibStyleItem::SmallFont, 8 }, { ibStyleItem::RegularFont, 9 },
			{ ibStyleItem::LargeFont, 11 }, { ibStyleItem::VeryLargeFont, 14 },
		};
		for (const auto& font : fonts) {
			style.m_light.Set(font.item, Font(font.points));
			style.m_dark.Set(font.item, Font(font.points));
		}
		for (ibStyleValues* look : { &style.m_light, &style.m_dark }) {
			look->Set(ibStyleItem::ControlBorder, ibBorder(ibBorderKind::Line, 1));
			look->Set(ibStyleItem::ButtonBorder, ibBorder(ibBorderKind::Rounded, 1));
		}
		return made;
	}();
	return *s_platform;
}
