////////////////////////////////////////////////////////////////////////////
//	Description : A form by its full name — see formName.h
////////////////////////////////////////////////////////////////////////////

#include "formName.h"

#include "backend/backend_exception.h"
#include "backend/metaData.h"
#include "backend/metaCollection/genericData.h"

namespace {

// ⭐ THE KINDS THAT HAVE FORMS, each by every word a name may use for it: the metatype's own name
// first (it is also the one a refusal says), then the Russian and the Ukrainian.
struct ibFormKind {
	ibClassID m_clsid;
	const ibChar* m_names[3];
};

const ibFormKind s_formKinds[] = {
	{ g_metaCatalogCLSID,                    { ibT("Catalog"),                    ibT("Справочник"),             ibT("Довідник") } },
	{ g_metaDocumentCLSID,                   { ibT("Document"),                   ibT("Документ"),               ibT("Документ") } },
	{ g_metaEnumerationCLSID,                { ibT("Enumeration"),                ibT("Перечисление"),           ibT("Перелік") } },
	{ g_metaDataProcessorCLSID,              { ibT("DataProcessor"),              ibT("Обработка"),              ibT("Обробка") } },
	{ g_metaReportCLSID,                     { ibT("Report"),                     ibT("Отчет"),                  ibT("Звіт") } },
	{ g_metaInformationRegisterCLSID,        { ibT("InformationRegister"),        ibT("РегистрСведений"),        ibT("РегістрВідомостей") } },
	{ g_metaAccumulationRegisterCLSID,       { ibT("AccumulationRegister"),       ibT("РегистрНакопления"),      ibT("РегістрНакопичення") } },
	{ g_metaAccountingRegisterCLSID,         { ibT("AccountingRegister"),         ibT("РегистрБухгалтерии"),     ibT("РегістрБухгалтерії") } },
	{ g_metaCalculationRegisterCLSID,        { ibT("CalculationRegister"),        ibT("РегистрРасчета"),         ibT("РегістрРозрахунку") } },
	{ g_metaChartOfCharacteristicTypesCLSID, { ibT("ChartOfCharacteristicTypes"), ibT("ПланВидовХарактеристик"), ibT("ПланВидівХарактеристик") } },
	{ g_metaChartOfAccountsCLSID,            { ibT("ChartOfAccounts"),            ibT("ПланСчетов"),             ibT("ПланРахунків") } },
	{ g_metaChartOfCalculationTypesCLSID,    { ibT("ChartOfCalculationTypes"),    ibT("ПланВидовРасчета"),       ibT("ПланВидівРозрахунку") } },
	{ g_metaParameterizedJobCLSID,           { ibT("ScheduledJob"),               ibT("РегламентноеЗадание"),    ibT("РегламентнеЗавдання") } },
	{ g_metaCommonFormCLSID,                 { ibT("CommonForm"),                 ibT("ОбщаяФорма"),             ibT("ЗагальнаФорма") } },
};

// ⭐ A FORM OF A KIND, by the same three words, and the form types that answer it — a metatype
// names its own (GetFormType), and the first of these it has is the one: an object form is a
// record form in a register and the main form of a report or a data processor.
struct ibFormAlias {
	const ibChar* m_names[3];
	std::vector<ibString> m_formTypes;
};

const std::vector<ibFormAlias>& FormAliases()
{
	static const std::vector<ibFormAlias> aliases = {
		{ { ibT("ObjectForm"),      ibT("ФормаОбъекта"),      ibT("ФормаОб'єкта") },     { ibT("FormObject"), ibT("FormRecord"), ibT("FormReport"), ibT("FormDataProcessor") } },
		{ { ibT("Form"),            ibT("Форма"),             ibT("Форма") },            { ibT("FormObject"), ibT("FormRecord"), ibT("FormReport"), ibT("FormDataProcessor") } },
		{ { ibT("RecordForm"),      ibT("ФормаЗаписи"),       ibT("ФормаЗапису") },      { ibT("FormRecord") } },
		{ { ibT("ListForm"),        ibT("ФормаСписка"),       ibT("ФормаСписку") },      { ibT("FormList") } },
		{ { ibT("ChoiceForm"),      ibT("ФормаВыбора"),       ibT("ФормаВибору") },      { ibT("FormSelect") } },
		{ { ibT("GroupForm"),       ibT("ФормаГруппы"),       ibT("ФормаГрупи") },       { ibT("FormFolder") } },
		{ { ibT("GroupChoiceForm"), ibT("ФормаВыбораГруппы"), ibT("ФормаВиборуГрупи") }, { ibT("FormGroupSelect") } },
	};
	return aliases;
}

// The third word of Kind.Name.Form.FormName — the same word that, last, names the object form.
const ibChar* const s_formWord[3] = { ibT("Form"), ibT("Форма"), ibT("Форма") };

bool IsOneOf(const ibString& word, const ibChar* const (&names)[3])
{
	for (const ibChar* name : names) {
		if (word.IsSameAs(ibString(name), false))
			return true;
	}
	return false;
}

const ibFormKind* FindKind(const ibString& word)
{
	for (const ibFormKind& kind : s_formKinds) {
		if (IsOneOf(word, kind.m_names))
			return &kind;
	}
	return nullptr;
}

const ibFormKind* FindKind(const ibClassID& clsid)
{
	for (const ibFormKind& kind : s_formKinds) {
		if (kind.m_clsid == clsid)
			return &kind;
	}
	return nullptr;
}

[[noreturn]] void RefuseSpelling(const ibString& fullName)
{
	ibBackendCoreException::Error(
		_("'%s' is not a form name: a form is named Kind.Name.Form.FormName, Kind.Name.ObjectForm (or ListForm, ChoiceForm, GroupForm, GroupChoiceForm, RecordForm, Form) or CommonForm.Name"),
		fullName);
}

} // namespace

ibFormName::ibParts ibFormName::Parse(const ibString& fullName)
{
	std::vector<ibString> words;
	ibString rest = fullName;
	while (true) {
		const int dot = rest.Find(ibT('.'));
		ibString word = (dot == wxNOT_FOUND ? rest : rest.Left((size_t)dot)).TrimAll();
		if (word.IsEmpty())
			RefuseSpelling(fullName);
		words.emplace_back(std::move(word));
		if (dot == wxNOT_FOUND)
			break;
		rest = rest.Mid((size_t)dot + 1);
	}

	if (words.size() < 2 || words.size() > 4)
		RefuseSpelling(fullName);

	const ibFormKind* const kind = FindKind(words[0]);
	if (kind == nullptr)
		ibBackendCoreException::Error(_("'%s' is not a form name: '%s' is not a kind of object that has forms"),
			fullName, words[0]);

	ibParts parts;
	parts.m_kind = kind->m_clsid;
	parts.m_objectName = words[1];

	if (kind->m_clsid == g_metaCommonFormCLSID) {
		if (words.size() != 2)
			RefuseSpelling(fullName);
		return parts;
	}

	if (words.size() == 4 && IsOneOf(words[2], s_formWord)) {   // Kind.Name.Form.FormName
		parts.m_formName = words[3];
		return parts;
	}

	if (words.size() == 3) {
		for (const ibFormAlias& alias : FormAliases()) {
			if (IsOneOf(words[2], alias.m_names)) {
				parts.m_formTypes = alias.m_formTypes;
				return parts;
			}
		}
	}

	RefuseSpelling(fullName);
}

ibFormName ibFormName::Resolve(const ibMetaData* metaData, const ibString& fullName)
{
	const ibParts parts = Parse(fullName);

	ibFormName name;

	if (parts.m_kind == g_metaCommonFormCLSID) {
		name.m_commonForm = metaData != nullptr
			? metaData->FindAnyObjectByFilter<ibValueMetaObjectCommonForm>(parts.m_objectName, g_metaCommonFormCLSID)
			: nullptr;
		if (name.m_commonForm == nullptr)
			ibBackendCoreException::Error(_("Form '%s' not found: there is no common form '%s'"),
				fullName, parts.m_objectName);
		return name;
	}

	const ibString kindName = FindKind(parts.m_kind)->m_names[0];

	name.m_owner = metaData != nullptr
		? metaData->FindAnyObjectByFilter<ibValueMetaObjectGenericData>(parts.m_objectName, parts.m_kind)
		: nullptr;
	if (name.m_owner == nullptr)
		ibBackendCoreException::Error(_("Form '%s' not found: there is no %s '%s'"),
			fullName, kindName, parts.m_objectName);

	const ibFormTypeList formTypes = name.m_owner->GetFormType();

	if (!parts.m_formName.IsEmpty()) {
		name.m_form = name.m_owner->FindFormObjectByFilter(parts.m_formName);
		if (name.m_form == nullptr || name.m_form->GetClassType() != g_metaFormCLSID)
			ibBackendCoreException::Error(_("Form '%s' not found: %s '%s' has no form '%s'"),
				fullName, kindName, parts.m_objectName, parts.m_formName);
		name.m_formType = name.m_form->GetTypeForm();
		for (unsigned int idx = 0; idx < formTypes.GetItemCount(); idx++) {
			if (formTypes.GetItemId(idx) == name.m_formType)
				name.m_formTypeName = formTypes.GetItemName(idx);
		}
		return name;
	}

	for (const ibString& wanted : parts.m_formTypes) {
		for (unsigned int idx = 0; idx < formTypes.GetItemCount(); idx++) {
			if (formTypes.GetItemName(idx) == wanted) {
				name.m_formType = formTypes.GetItemId(idx);
				name.m_formTypeName = wanted;
				return name;
			}
		}
	}

	ibBackendCoreException::Error(_("Form '%s' not found: %s '%s' has no form of this kind"),
		fullName, kindName, parts.m_objectName);
	return name;
}

bool ibFormName::IsObjectForm() const
{
	return m_formTypeName == ibT("FormObject") || m_formTypeName == ibT("FormFolder");
}
