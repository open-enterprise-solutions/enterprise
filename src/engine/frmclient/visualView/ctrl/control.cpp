#include "control.h"

#include "core/serialize/dataBuilder.h"

// (The desktop's reads the functional options here too — the server's: what a control offers, the frame draws.)
bool ibValueControl::ReadData(const ibDataNode& node)
{
	return ibValueFrame::ReadData(node);
}

bool ibValueControl::WriteData(ibDataNode& node) const
{
	return ibValueFrame::WriteData(node);
}
