#include "variantSize.h"

wxString ibVariantDataSize::MakeString() const
{
	return typeConv::SizeToString(m_size);
}
