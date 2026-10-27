#ifndef __EVENT_LIST_H__
#define __EVENT_LIST_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/standardCommand.h"

//base event for "list"
class BACKEND_API ibEventAction : public ibEvent {
public:

	// The actions it may run — the family's own verb, as ibPropertyList::GetValueList answers it: the front builds the
	// editor of them, the server sends them with the row. NOT const: the functor refills m_listPropValue.
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override {
		if (!m_functor->Invoke(this))
			return ibPropertyChoiceMode::None;
		for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
			list.Add(
				m_listPropValue.GetItemLabel(idx),
				m_listPropValue.GetItemId(idx),
				m_listPropValue.GetItemPicture(idx)
			);
		}
		return ibPropertyChoiceMode::Single;
	}

private:

	static ibVariant CreateVariantData(const ibPropertyObject* property, const ibStandardCommandDescription& act);

	class BACKEND_API ibEventOptionList {

		struct ibEventOptionItem {

			ibEventOptionItem() :
				m_isOk(true), m_strName(), m_strLabel(), m_id(wxNOT_FOUND), m_value()
			{
			}

			ibEventOptionItem(const ibString& name, const ibActionID& l, const ibPictureDescription& p, const ibValue& v) :
				m_isOk(true), m_strName(name), m_strLabel(name), m_picture(p), m_id(l), m_value(v)
			{
			}

			ibEventOptionItem(const ibString& name, const ibString& label, const ibActionID& l, const ibPictureDescription& p, const ibValue& v) :
				m_isOk(true), m_strName(name), m_strLabel(label), m_picture(p), m_id(l), m_value(v)
			{
			}

			ibEventOptionItem(const ibString& name, const ibString& label, const ibString& help, const ibActionID& l, const ibPictureDescription& p, const ibValue& v) :
				m_isOk(true), m_strName(name), m_strLabel(label), m_strHelp(help), m_picture(p), m_id(l), m_value(v)
			{
			}

			ibEventOptionItem(const ibEventOptionItem& item) :
				m_isOk(true), m_strName(item.m_strName), m_strLabel(item.m_strLabel), m_strHelp(item.m_strHelp), m_picture(item.m_picture), m_id(item.m_id), m_value(item.m_value)
			{
			}

			ibEventOptionItem& operator = (const ibEventOptionItem& src) {
				m_strName = src.m_strName;
				m_strLabel = src.m_strLabel;
				m_strHelp = src.m_strHelp;
				m_picture = src.m_picture;
				m_id = src.m_id;
				m_value = src.m_value;
				return *this;
			}

			operator const ibActionID() const { return m_id; }

			bool m_isOk;
			ibString m_strName;
			ibString m_strLabel;
			ibString m_strHelp;
			ibPictureDescription m_picture;
			ibActionID m_id;
			ibValue m_value;
		};

		ibEventOptionItem GetItemAt(const unsigned int idx) const {
			if (idx >= m_listValue.size())
				return ibEventOptionItem();
			auto it = m_listValue.begin();
			std::advance(it, idx);
			return *it;
		};

	public:

		void ResetListItem() { m_listValue.clear(); }

		void AppendItem(const ibString& name, const ibActionID& l, const ibPictureDescription& p, const ibValue& v) { (void)m_listValue.emplace_back(name, l, p, v); }
		void AppendItem(const ibString& name, const ibString& label, const ibActionID& l, const ibPictureDescription& p, const ibValue& v) { (void)m_listValue.emplace_back(name, label, l, p, v); }
		void AppendItem(const ibString& name, const ibString& label, const ibString& help, const ibActionID& l, const ibPictureDescription& p, const ibValue& v) { (void)m_listValue.emplace_back(label, help, l, p, v); }

		ibString GetItemName(const unsigned int idx) const { return GetItemAt(idx).m_strName; }
		ibString GetItemLabel(const unsigned int idx) const { return GetItemAt(idx).m_strLabel; }
		ibString GetItemHelp(const unsigned int idx) const { return GetItemAt(idx).m_strHelp; }
		ibPictureDescription GetItemPicture(const unsigned int idx) const { return GetItemAt(idx).m_picture; }
		ibActionID GetItemId(const unsigned int idx) const { return GetItemAt(idx).m_id; }
		ibValue GetItemValue(const unsigned int idx) const { return GetItemAt(idx).m_value; }

		unsigned int GetItemCount() const { return (unsigned int)m_listValue.size(); }

	private:
		std::vector<ibEventOptionItem> m_listValue;
	};

	class BACKEND_API ibEventFunctor {
	public:
		virtual ~ibEventFunctor() {}
		virtual bool Invoke(ibEventAction* property) = 0;
	};

	template <typename optClass>
	class ibEventValueFunctor : public ibEventFunctor {
		bool (optClass::* m_funcHandler)(ibEventAction* evt);
	public:
		ibEventValueFunctor(bool (optClass::* funcHandler)(ibEventAction* evt), optClass* handler)
			: m_funcHandler(funcHandler), m_handler(handler)
		{
		}
		virtual bool Invoke(ibEventAction* property) override {
			const ibEventOptionList listPropValue = property->m_listPropValue;
			if (property != nullptr) property->ResetListItem();
			return (m_handler->*m_funcHandler)(property);
		}
	private:
		optClass* m_handler;
	};

#pragma region item 
	void ResetListItem() { (void)m_listPropValue.ResetListItem(); }
#pragma endregion
public:

#pragma region value
	ibActionID GetValueAsInteger() const;
	ibString GetValueAsString() const;
	ibStandardCommandDescription& GetValueAsActionDesc() const;
	void SetValue(const ibStandardCommandDescription& val);
#pragma endregion 

#pragma region item
	void AppendItem(const ibString& name, const ibActionID& l, const ibPictureDescription& p, const ibValue& v) { (void)m_listPropValue.AppendItem(name, l, p, v); }
	void AppendItem(const ibString& name, const ibString& label, const ibActionID& l, const ibPictureDescription& p, const ibValue& v) { (void)m_listPropValue.AppendItem(name, label, l, p, v); }
	void AppendItem(const ibString& name, const ibString& label, const ibString& help, const ibActionID& l, const ibPictureDescription& p, const ibValue& v) { (void)m_listPropValue.AppendItem(name, label, help, l, p, v); }
#pragma endregion

	template <typename optClass>
	ibEventAction(ibPropertyCategory* cat, const ibString& name, const std::vector<ibString>& args,
		bool (optClass::* funcHandler)(ibEventAction* evt), const ibActionID& value) : ibEvent(cat, name, args, CreateVariantData(cat->GetPropertyObject(), value))
	{
		m_functor = std::make_unique<ibEventValueFunctor<optClass>>(funcHandler, (optClass*)cat->GetPropertyObject());
	}

	template <typename optClass>
	ibEventAction(ibPropertyCategory* cat, const ibString& name, const ibString& label, const std::vector<ibString>& args,
		bool (optClass::* funcHandler)(ibEventAction* evt), const ibActionID& value) : ibEvent(cat, name, label, args, CreateVariantData(cat->GetPropertyObject(), value))
	{
		m_functor = std::make_unique<ibEventValueFunctor<optClass>>(funcHandler, (optClass*)cat->GetPropertyObject());
	}

	template <typename optClass>
	ibEventAction(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString, const std::vector<ibString>& args,
		bool (optClass::* funcHandler)(ibEventAction* evt), const ibActionID& value) : ibEvent(cat, name, label, helpString, args, CreateVariantData(cat->GetPropertyObject(), value))
	{
		m_functor = std::make_unique<ibEventValueFunctor<optClass>>(funcHandler, (optClass*)cat->GetPropertyObject());
	}

	virtual ~ibEventAction() {}

	virtual bool IsEmptyProperty() const { return GetValueAsInteger() == wxNOT_FOUND; }

	// An action event does NOT dispatch through CallAsEvent — it runs via its own functor (Invoke). No procedure /
	// lambda dispatcher, so the CallAsEvent path is a no-op for it (nullptr).
	virtual ibEventDispatcher* GetDispatcher() const override { return nullptr; }

	// Set/Get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:


private:

	ibEventOptionList m_listPropValue;
	std::unique_ptr<ibEventFunctor> m_functor;
};

#endif