#ifndef __EVENT_LIST_H__
#define __EVENT_LIST_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/standardCommand.h"

//base event for "list"
class BACKEND_API ibEventAction : public ibEvent {
public:

	// Public + fires its own functor, same as ibPropertyList::GetValueList — the front
	// builds the editor now and reads the actions from here. NOT const: the functor
	// refills m_listPropValue, so this mutates.
	ibPropertyChoiceList GetEventList() {
		ibPropertyChoiceList constants;
		if (!m_functor->Invoke(this))
			return constants;
		for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
			constants.Add(
				m_listPropValue.GetItemLabel(idx),
				m_listPropValue.GetItemId(idx),
				m_listPropValue.GetItemBitmap(idx)
			);
		}
		return constants;
	}

private:

	wxVariantData* CreateVariantData(const ibPropertyObject* property, const ibStandardCommandDescription& act) const;

	class BACKEND_API ibEventOptionList {

		struct ibEventOptionItem {

			ibEventOptionItem() :
				m_isOk(true), m_strName(), m_strLabel(), m_id(wxNOT_FOUND), m_value()
			{
			}

			ibEventOptionItem(const wxString& name, const ibActionID& l, const wxBitmap& b, const ibValue& v) :
				m_isOk(true), m_strName(name), m_strLabel(name), m_bmp(b), m_id(l), m_value(v)
			{
			}

			ibEventOptionItem(const wxString& name, const wxString& label, const ibActionID& l, const wxBitmap& b, const ibValue& v) :
				m_isOk(true), m_strName(name), m_strLabel(label), m_bmp(b), m_id(l), m_value(v)
			{
			}

			ibEventOptionItem(const wxString& name, const wxString& label, const wxString& help, const ibActionID& l, const wxBitmap& b, const ibValue& v) :
				m_isOk(true), m_strName(name), m_strLabel(label), m_strHelp(help), m_bmp(b), m_id(l), m_value(v)
			{
			}

			ibEventOptionItem(const ibEventOptionItem& item) :
				m_isOk(true), m_strName(item.m_strName), m_strLabel(item.m_strLabel), m_strHelp(item.m_strHelp), m_bmp(item.m_bmp), m_id(item.m_id), m_value(item.m_value)
			{
			}

			ibEventOptionItem& operator = (const ibEventOptionItem& src) {
				m_strName = src.m_strName;
				m_strLabel = src.m_strLabel;
				m_strHelp = src.m_strHelp;
				m_bmp = src.m_bmp;
				m_id = src.m_id;
				m_value = src.m_value;
				return *this;
			}

			operator const ibActionID() const { return m_id; }

			bool m_isOk;
			wxString m_strName;
			wxString m_strLabel;
			wxString m_strHelp;
			wxBitmap m_bmp;
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

		void AppendItem(const wxString& name, const ibActionID& l, const wxBitmap& b, const ibValue& v) { (void)m_listValue.emplace_back(name, l, b, v); }
		void AppendItem(const wxString& name, const wxString& label, const ibActionID& l, const wxBitmap& b, const ibValue& v) { (void)m_listValue.emplace_back(name, label, l, b, v); }
		void AppendItem(const wxString& name, const wxString& label, const wxString& help, const ibActionID& l, const wxBitmap& b, const ibValue& v) { (void)m_listValue.emplace_back(label, help, l, b, v); }

		wxString GetItemName(const unsigned int idx) const { return GetItemAt(idx).m_strName; }
		wxString GetItemLabel(const unsigned int idx) const { return GetItemAt(idx).m_strLabel; }
		wxString GetItemHelp(const unsigned int idx) const { return GetItemAt(idx).m_strHelp; }
		wxBitmap GetItemBitmap(const unsigned int idx) const { return GetItemAt(idx).m_bmp; }
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
	wxString GetValueAsString() const;
	ibStandardCommandDescription& GetValueAsActionDesc() const;
	void SetValue(const ibStandardCommandDescription& val);
#pragma endregion 

#pragma region item
	void AppendItem(const wxString& name, const ibActionID& l, const wxBitmap& b, const ibValue& v) { (void)m_listPropValue.AppendItem(name, l, b, v); }
	void AppendItem(const wxString& name, const wxString& label, const ibActionID& l, const wxBitmap& b, const ibValue& v) { (void)m_listPropValue.AppendItem(name, label, l, b, v); }
	void AppendItem(const wxString& name, const wxString& label, const wxString& help, const ibActionID& l, const wxBitmap& b, const ibValue& v) { (void)m_listPropValue.AppendItem(name, label, help, l, b, v); }
#pragma endregion

	template <typename optClass>
	ibEventAction(ibPropertyCategory* cat, const wxString& name, const wxArrayString& args,
		bool (optClass::* funcHandler)(ibEventAction* evt), const ibActionID& value) : ibEvent(cat, name, args, CreateVariantData(cat->GetPropertyObject(), value))
	{
		m_functor = new ibEventValueFunctor<optClass>(funcHandler, (optClass*)cat->GetPropertyObject());
	}

	template <typename optClass>
	ibEventAction(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxArrayString& args,
		bool (optClass::* funcHandler)(ibEventAction* evt), const ibActionID& value) : ibEvent(cat, name, label, args, CreateVariantData(cat->GetPropertyObject(), value))
	{
		m_functor = new ibEventValueFunctor<optClass>(funcHandler, (optClass*)cat->GetPropertyObject());
	}

	template <typename optClass>
	ibEventAction(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const wxArrayString& args,
		bool (optClass::* funcHandler)(ibEventAction* evt), const ibActionID& value) : ibEvent(cat, name, label, helpString, args, CreateVariantData(cat->GetPropertyObject(), value))
	{
		m_functor = new ibEventValueFunctor<optClass>(funcHandler, (optClass*)cat->GetPropertyObject());
	}

	virtual ~ibEventAction() { wxDELETE(m_functor); }

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
	ibEventFunctor* m_functor;
};

#endif