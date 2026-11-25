#include "style.h"

#include <algorithm>
#include <cmath>
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

const ibStyleValue& ibStyle::Get(ibStyleItem item, ibStyleLook look) const
{
	const bool dark = look == ibStyleLook::Dark;
	if (const ibStyleValue* const own = (dark ? m_dark : m_light).Find(item))
		return *own;
	const ibStyle& platform = Platform();
	if (const ibStyleValue* const given = (dark ? platform.m_dark : platform.m_light).Find(item))
		return *given;
	static const ibStyleValue* const s_none = new ibStyleValue();   // never torn down, as the platform's
	return *s_none;
}

namespace {

// OKLAB (Björn Ottosson, 2020) - a colour as its lightness L, from 0 to 1, and two opposing axes: what an eye reads as
// equally lighter is an equal step of L, so a lightness carried from one look to the other keeps how far apart two
// colours read.
struct OkLab { double l, a, b; };

double LinearOf(unsigned char channel)
{
	const double c = channel / 255.0;
	return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double GammaOf(double linear)
{
	return linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
}

OkLab OkLabOf(const ibColour& colour)
{
	const double r = LinearOf(colour.m_red), g = LinearOf(colour.m_green), b = LinearOf(colour.m_blue);
	const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
	const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
	const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
	return { 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
		1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
		0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s };
}

// …and back, in linear sRGB; a colour sRGB does not hold has a channel out of 0..1.
void LinearOf(const OkLab& lab, double& r, double& g, double& b)
{
	const double l = std::pow(lab.l + 0.3963377774 * lab.a + 0.2158037573 * lab.b, 3);
	const double m = std::pow(lab.l - 0.1055613458 * lab.a - 0.0638541728 * lab.b, 3);
	const double s = std::pow(lab.l - 0.0894841775 * lab.a - 1.2914855480 * lab.b, 3);
	r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
	g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
	b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
}

bool InGamut(const OkLab& lab)
{
	double r, g, b;
	LinearOf(lab, r, g, b);
	constexpr double e = 1e-7;
	return r >= -e && r <= 1 + e && g >= -e && g <= 1 + e && b >= -e && b <= 1 + e;
}

unsigned char ChannelOf(double linear)
{
	return static_cast<unsigned char>(std::lround(std::clamp(GammaOf(std::clamp(linear, 0.0, 1.0)), 0.0, 1.0) * 255));
}

} // namespace

ibColour ibStyle::Get(const ibColour& colour, ibStyleLook look) const
{
	if (!colour.IsOk() || look == ibStyleLook::Light)
		return colour;

	// The lightness, from the light look's form ground and ink onto the dark look's: the light ground the dark one, the
	// light ink the dark one, in between and on the ground's side along the same line. Past the ink, the rest of the way
	// to the light look's end (black, under a dark ink) onto the rest of it to the dark look's (white) - a navy ink a pale
	// blue, black white, not all of them the one white.
	const auto lightness = [this](ibStyleItem item, ibStyleLook in) { return OkLabOf(std::get<ibColour>(Get(item, in))).l; };
	const double lightGround = lightness(ibStyleItem::FormBackground, ibStyleLook::Light);
	const double lightInk = lightness(ibStyleItem::FormText, ibStyleLook::Light);
	const double darkGround = lightness(ibStyleItem::FormBackground, ibStyleLook::Dark);
	const double darkInk = lightness(ibStyleItem::FormText, ibStyleLook::Dark);
	const double lightEnd = lightInk < lightGround ? 0 : 1, darkEnd = darkInk > darkGround ? 1 : 0;

	OkLab lab = OkLabOf(colour);
	const double past = lightEnd - lightInk;
	const double span = lightInk - lightGround;   // none in a style whose form ink is as light as its ground: the ground
	lab.l = (lab.l - lightInk) * past > 0 ? darkInk + (lab.l - lightInk) / past * (darkEnd - darkInk)
		: span != 0 ? darkGround + (lab.l - lightGround) * (darkInk - darkGround) / span : darkGround;
	lab.l = std::clamp(lab.l, 0.0, 1.0);

	// The hue kept, the chroma as much of it as sRGB holds at that lightness.
	if (!InGamut(lab)) {
		double held = 0, lost = 1;
		for (int step = 0; step < 24; step++) {
			const double tried = (held + lost) / 2;
			(InGamut({ lab.l, lab.a * tried, lab.b * tried }) ? held : lost) = tried;
		}
		lab.a *= held;
		lab.b *= held;
	}

	double r, g, b;
	LinearOf(lab, r, g, b);
	return ibColour(ChannelOf(r), ChannelOf(g), ChannelOf(b), colour.m_alpha);
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
		// Each colour item: its light value and its dark one. The light look is what the screen shows — the forms' ink on
		// a ground a step below white, the fields white on it (frmserver's default style; cream until 2026-11-13), the
		// desktop's chrome, border, accent and buttons, which the Luna theme paints with from here
		// (deskgui/univ/theme/lunaTheme.cpp), and the browser the same (webserver/www/styles/tokens.css). The dark look is
		// the same tiers in the same order, a surface over another lighter than it, the chrome dusty blue darkened, the
		// accent the same terracotta lifted — the browser's dark tokens where it has them (docs/private/ui-palette.md).
		const struct { ibStyleItem item; unsigned int light; unsigned int dark; } colours[] = {
			{ ibStyleItem::FormBackground,        0xEEF2F6, 0x22262B },
			{ ibStyleItem::FormText,              0x3F5C77, 0xC9D6E2 },
			{ ibStyleItem::ButtonBackground,      0xFFFFFF, 0x2C3036 },
			{ ibStyleItem::ButtonText,            0x000000, 0xE4E6EA },
			{ ibStyleItem::FieldBackground,       0xFFFFFF, 0x1B1D21 },
			{ ibStyleItem::FieldText,             0x000000, 0xE4E6EA },
			{ ibStyleItem::Selection,             0xD97757, 0xB85A38 },
			{ ibStyleItem::SelectionText,         0xFFFFFF, 0xFFFFFF },
			{ ibStyleItem::AlternateBackground,   0xF7F8F9, 0x282B30 },
			{ ibStyleItem::TooltipBackground,     0xE6EEF5, 0x30343A },
			{ ibStyleItem::TooltipText,           0x000000, 0xE4E6EA },
			{ ibStyleItem::SpecialText,           0x3B50A0, 0x8197E8 },
			{ ibStyleItem::NegativeNumber,        0xC0392B, 0xE5675A },
			{ ibStyleItem::Border,                0xA8BAC8, 0x3A3F46 },
			{ ibStyleItem::TableHeaderBackground, 0xE6EEF5, 0x2C3036 },
			{ ibStyleItem::TableHeaderText,       0x3F5C77, 0xC9D6E2 },
			{ ibStyleItem::TableFooterBackground, 0xE6EEF5, 0x2C3036 },
			{ ibStyleItem::TableFooterText,       0x3F5C77, 0xC9D6E2 },
			{ ibStyleItem::ReportHeader,          0xC5DCE8, 0x2A3D4B },
			{ ibStyleItem::ReportLine,            0xD9D9D9, 0x3A3F46 },
			{ ibStyleItem::Activity,              0xD97757, 0xE58A6C },
			{ ibStyleItem::Accent,                0xD97757, 0xE58A6C },
			{ ibStyleItem::Navigation,            0xDDE4E8, 0x1F2A33 },
			{ ibStyleItem::SecondaryNavigation,   0xF5F7F8, 0x27333D },
			{ ibStyleItem::Important,             0xC0392B, 0xE5675A },
			{ ibStyleItem::DesktopBackground,     0xD3DBE1, 0x192229 },
			{ ibStyleItem::PanelBackground,       0xE7ECF0, 0x24272C },
			{ ibStyleItem::BarBackground,         0xDDE4E8, 0x1F2A33 },
			{ ibStyleItem::ControlBackground,     0xE6EEF5, 0x30343A },
			{ ibStyleItem::DisabledText,          0x94A6B4, 0x6B7480 },
			{ ibStyleItem::CaptionText,           0xFFFFFF, 0xE4E6EA },
			{ ibStyleItem::AccentPressed,         0xB85A38, 0xC96F52 },
			{ ibStyleItem::AccentText,            0xFFFFFF, 0x16181B },
			{ ibStyleItem::Shelf,                 0xE0D8CC, 0x3A352E },
		};
		for (const auto& colour : colours) {
			style.m_light.Set(colour.item, Rgb(colour.light));
			style.m_dark.Set(colour.item, Rgb(colour.dark));
		}
		// Three grouping levels of a report, the outermost the strongest.
		style.m_light.Set(ibStyleItem::ReportGroup, std::vector<ibColour>{ Rgb(0xE8EEF3), Rgb(0xF1F5F8), Rgb(0xF8FAFB) });
		style.m_dark.Set(ibStyleItem::ReportGroup, std::vector<ibColour>{ Rgb(0x2F3A44), Rgb(0x2A333B), Rgb(0x262D33) });
		// An active window's caption, lit from the top: its top, its bottom.
		style.m_light.Set(ibStyleItem::Caption, std::vector<ibColour>{ Rgb(0x5A7B95), Rgb(0x3F5C77) });
		style.m_dark.Set(ibStyleItem::Caption, std::vector<ibColour>{ Rgb(0x3E5568), Rgb(0x2C3F50) });
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
