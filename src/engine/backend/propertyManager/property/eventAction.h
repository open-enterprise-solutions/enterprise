#ifndef __EVENT_LIST_H__
#define __EVENT_LIST_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/actionInfo.h"

//base event for "list"
class BACKEND_API ibEventAction : public ibEvent {

	wxVariantData* CreateVariantData(const ibPropertyObject* property, const ibActionDescription& act) const;
	wxPGChoices GetEventList() const {
		wxPGChoices constants;
		for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
			wxPGChoiceEntry item(
				m_listPropValue.GetItemLabel(idx),
				m_listPropValue.GetItemId(idx)
			);
			item.SetBitmap(m_listPropValue.GetItemBitmap(idx));
			constants.Add(item);
		}
		return constants;
	}

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
			if (idx > m_listValue.size())
				return ibEventOptionItem();
			auto it = m_listValue.begin();
			std::advance(it, idx);
			return *it;
		};

		ibEventOptionItem GetItemById(const ibActionID& id) const {
			auto it = std::find_if(m_listValue.begin(), m_listValue.end(),
				[id](const ibEventOptionItem& p) { return id == p.m_id; }
			);
			if (it != m_listValue.end())
				return *it;
			return ibEventOptionItem();
		};

	public:

		void ResetListItem() { m_listValue.clear(); }

		void AppendItem(const wxString& name, const ibActionID& l, const wxBitmap& b, const ibValue& v) { (void)m_listValue.emplace_back(name, l, b, v); }
		void AppendItem(const wxString& name, const wxString& label, const ibActionID& l, const wxBitmap& b, const ibValue& v) { (void)m_listValue.emplace_back(name, label, l, b, v); }
		void AppendItem(const wxString& name, const wxString& label, const wxString& help, const ibActionID& l, const wxBitmap& b, const ibValue& v) { (void)m_listValue.emplace_back(label, help, l, b, v); }

		bool HasValue(const ibActionID& l) const { return GetItemById(l); }

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
	ibActionDescription& GetValueAsActionDesc() const;
	void SetValue(const ibActionDescription& val);
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

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (!m_functor->Invoke(const_cast<ibEventAction*>(this)))
			return nullptr;
		if (ms_propertyEventAction != nullptr)
			return ms_propertyEventAction(m_propLabel, m_propName, GetEventList(), m_propValue);
		return nullptr;
	}

	// Set/Get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

public:

	static wxObject* (*ms_propertyEventAction)(const wxString&, const wxString&, const wxPGChoices&, const wxVariant&);

private:

	ibEventOptionList m_listPropValue;
	ibEventFunctor* m_functor;
};

#endif