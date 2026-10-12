#include "webSizer.h"
#include "webWindow.h"

#include <algorithm>

ibWebSizer::ibWebSizer() = default;

ibWebSizer::~ibWebSizer()
{
	// Detach from owner so it doesn't dangle-point at us. Owner can be
	// either a window (SetSizer slot) or a parent sizer (items vector).
	if (m_owner != nullptr) {
		if (auto* parentSizer = dynamic_cast<ibWebSizer*>(m_owner)) {
			parentSizer->Remove(this);
		} else if (auto* parentWindow = dynamic_cast<ibWebWindow*>(m_owner)) {
			if (parentWindow->GetSizer() == this)
				parentWindow->SetSizer(nullptr);
		}
	}

	// Sizer owns its children. Clear their back-pointer first so their
	// dtors don't try to re-enter Remove while we're iterating.
	const auto items = m_items;
	m_items.clear();
	for (const Item& item : items) {
		if (auto* s = dynamic_cast<ibWebSizer*>(item.child))
			s->m_owner = nullptr;
		else if (auto* w = dynamic_cast<ibWebWindow*>(item.child))
			w->ClearContainer();
		delete item.child;
	}
}

void ibWebSizer::SetOwner(wxObject* owner)
{
	m_owner = owner;
}

void ibWebSizer::Add(ibWebWindow* child, const AddParams& params)
{
	AttachChild(child, params);
}

void ibWebSizer::Add(ibWebSizer* child, const AddParams& params)
{
	AttachChild(child, params);
}

void ibWebSizer::AttachChild(wxObject* child, const AddParams& params)
{
	if (child == nullptr)
		return;
	// Duplicate guard — dynamic_cast-free comparison on wxObject*.
	for (const Item& it : m_items) {
		if (it.child == child)
			return;
	}
	m_items.push_back(Item{ child, params });

	if (auto* s = dynamic_cast<ibWebSizer*>(child))
		s->SetOwner(this);
	else if (auto* w = dynamic_cast<ibWebWindow*>(child))
		w->SetContainer(this);
}

void ibWebSizer::Remove(wxObject* child)
{
	m_items.erase(
		std::remove_if(m_items.begin(), m_items.end(),
			[child](const Item& it) { return it.child == child; }),
		m_items.end());
}

bool ibWebSizer::UpdateItemParams(wxObject* child, const AddParams& params)
{
	for (auto& item : m_items) {
		if (item.child == child) {
			item.params = params;
			return true;
		}
	}
	return false;
}

nlohmann::json ibWebSizer::ToJSON() const
{
	nlohmann::json node = {
		{ "type", GetSizerType() },
		{ "visible", m_shown },
	};
	if (!m_items.empty()) {
		nlohmann::json arr = nlohmann::json::array();
		for (const Item& item : m_items) {
			nlohmann::json childNode;
			if (auto* w = dynamic_cast<ibWebWindow*>(item.child))
				childNode = w->ToJSON();
			else if (auto* s = dynamic_cast<ibWebSizer*>(item.child))
				childNode = s->ToJSON();
			else
				continue;
			// CSS-div analogue: proportion → flex-grow, flag → expand/
			// border sides / align, border → margin px. Layout math is
			// browser-side; the server just hands over the hints.
			childNode["layout"] = {
				{ "proportion", item.params.proportion },
				{ "flag",       item.params.flag       },
				{ "border",     item.params.border     },
			};
			arr.push_back(std::move(childNode));
		}
		node["children"] = std::move(arr);
	}
	return node;
}
