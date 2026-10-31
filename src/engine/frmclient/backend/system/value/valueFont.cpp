#include "valueFont.h"

#include "core/serialize/dataBuilder.h"
#include "frmclient/win/typeconv.h"   // FontToString / StringToFont — the engine's text of a font

namespace {
const ibString kFontText = ibT("v");
}

ibString ibValueFont::GetString() const
{
	return typeConv::FontToString(m_font);
}

bool ibValueFont::DoSerialize(ibDataNode& node) const
{
	node.SetValue(kFontText, ibString(typeConv::FontToString(m_font)));
	return true;
}

bool ibValueFont::DoDeserialize(const ibDataNode& node)
{
	m_font = typeConv::StringToFont(node.GetValue<ibString>(kFontText));
	return true;
}
