#include "valueFont.h"

#include "core/serialize/dataBuilder.h"
#include "frmclient/win/typeconv.h"   // FontToString / StringToFont — the engine's text of a font

namespace {
const wxString kFontText = wxT("v");
}

ibString ibValueFont::GetString() const
{
	return typeConv::FontToString(m_font);
}

bool ibValueFont::DoSerialize(ibDataNode& node) const
{
	node.SetValue(kFontText, typeConv::FontToString(m_font));
	return true;
}

bool ibValueFont::DoDeserialize(const ibDataNode& node)
{
	m_font = typeConv::StringToFont(node.GetValue<wxString>(kFontText));
	return true;
}
