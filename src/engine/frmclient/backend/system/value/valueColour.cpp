#include "valueColour.h"

#include "core/serialize/dataBuilder.h"
#include "frmclient/win/typeconv.h"   // ColourToString / StringToColour — the engine's text of a colour

namespace {
const ibString kColourText = ibT("v");
}

ibString ibValueColour::GetString() const
{
	return typeConv::ColourToString(m_colour);
}

bool ibValueColour::DoSerialize(ibDataNode& node) const
{
	node.SetValue(kColourText, ibString(typeConv::ColourToString(m_colour)));
	return true;
}

bool ibValueColour::DoDeserialize(const ibDataNode& node)
{
	m_colour = typeConv::StringToColour(node.GetValue<ibString>(kColourText));
	return true;
}
