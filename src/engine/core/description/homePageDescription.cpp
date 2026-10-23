#include "homePageDescription.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode / ibDataValue — node form

////////////////////////////////////////////////////////////////////////

std::vector<ibHomePageItem> ibHomePageDescription::GetShownItems(ibHomePageColumn column) const
{
	std::vector<ibHomePageItem> shown;

	auto append = [&shown](const std::vector<ibHomePageItem>& items) {
		for (const ibHomePageItem& item : items) {
			if (item.m_visible && item.IsOk())
				shown.push_back(item);
		}
	};

	// A one-column template renders BOTH stored columns in one run — the right column keeps
	// its items (a template switch is reversible), it just stops being a column of its own.
	if (!IsTwoColumns()) {
		if (column == eHomePageColumn_Left) {
			append(m_columns[eHomePageColumn_Left]);
			append(m_columns[eHomePageColumn_Right]);
		}
		return shown;
	}

	append(m_columns[column]);
	return shown;
}

bool ibHomePageDescription::RemoveItem(ibHomePageColumn column, unsigned int index)
{
	std::vector<ibHomePageItem>& items = m_columns[column];
	if (index >= items.size())
		return false;
	items.erase(items.begin() + index);
	return true;
}

bool ibHomePageDescription::MoveItem(ibHomePageColumn column, unsigned int index, int offset)
{
	std::vector<ibHomePageItem>& items = m_columns[column];
	const int target = (int)index + offset;
	if (index >= items.size() || target < 0 || target >= (int)items.size())
		return false;
	std::swap(items[index], items[target]);
	return true;
}

////////////////////////////////////////////////////////////////////////
//                             node form                              //
////////////////////////////////////////////////////////////////////////

static const ibChar* s_columnNodeName[eHomePageColumn_Count] = { wxT("Left"), wxT("Right") };

bool ibHomePageDescription::ReadNode(const ibDataValue& value)
{
	if (value.Kind() != ibDataKind::Child)
		return false;   // absent / never written — the default (empty) description stands

	const std::shared_ptr<ibDataNode>& root = value.AsChild();
	if (!root)
		return false;

	m_template = (ibHomePageTemplate)root->GetValue<std::int32_t>(wxT("Template"));

	for (int idx = eHomePageColumn_Left; idx < eHomePageColumn_Count; idx++) {
		m_columns[idx].clear();
		// A field, as every description keeps a list — a JSON text does not say an entry's area, and its reader puts a
		// list among the fields (ibJsonProvider::ReadEntry); a file written before kept the column a property.
		const ibDataValue* columnVal = root->FindField(s_columnNodeName[idx]);
		if (columnVal == nullptr)
			columnVal = root->FindProperty(s_columnNodeName[idx]);
		if (columnVal == nullptr || columnVal->Kind() != ibDataKind::Array)
			continue;
		for (const ibDataValue& itemVal : columnVal->AsArray()) {
			if (itemVal.Kind() != ibDataKind::Child)
				continue;
			const std::shared_ptr<ibDataNode>& itemNode = itemVal.AsChild();
			if (!itemNode)
				continue;
			ibHomePageItem item;
			item.m_formId = (ibMetaID)itemNode->GetValue<std::int32_t>(wxT("Form"));
			item.m_height = (unsigned int)itemNode->GetValue<std::int32_t>(wxT("Height"));
			item.m_visible = itemNode->GetValue<bool>(wxT("Visible"));
			if (item.IsOk())
				m_columns[idx].push_back(item);
		}
	}

	return true;
}

bool ibHomePageDescription::WriteNode(ibDataValue& value) const
{
	auto root = std::make_shared<ibDataNode>();
	root->SetValue(wxT("Template"), (std::int32_t)m_template);

	for (int idx = eHomePageColumn_Left; idx < eHomePageColumn_Count; idx++) {
		std::vector<ibDataValue> items;
		for (const ibHomePageItem& item : m_columns[idx]) {
			auto itemNode = std::make_shared<ibDataNode>();
			itemNode->SetValue(wxT("Form"), (std::int32_t)item.m_formId);
			itemNode->SetValue(wxT("Height"), (std::int32_t)item.m_height);
			itemNode->SetValue(wxT("Visible"), item.m_visible);
			items.push_back(ibDataValue::Child(itemNode));
		}
		root->AddField(s_columnNodeName[idx], ibDataValue::Array(items));
	}

	value = ibDataValue::Child(root);
	return true;
}
