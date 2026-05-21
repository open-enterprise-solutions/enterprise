/////////////////////////////////////////////////////////////////////////////
// prompts — 7 OES slash-command templates exposed via MCP prompts/list +
// prompts/get. See header for spec citations.
//
// Each prompt is opinionated about flow — Sigma-first for business-domain
// depth, then meta_create / write_module / sigma_check / save_config. The
// templates are concise (~200-400 words) and reference real oes-mcp +
// Pugi tool names so any MCP-compatible LLM gets a complete recipe.
/////////////////////////////////////////////////////////////////////////////

#include "prompts.h"
#include "jsonrpc.h"

#include <algorithm>
#include <mutex>

namespace mcpServer {
namespace {

// MCP: substitute `{key}` placeholders in `tpl` using `args`. Missing keys
// are left intact in the output — the renderer validates required keys
// before calling this, so a leftover `{foo}` is a logic bug, not user
// input. Cheap textual substitution; no escaping needed since templates
// produce plain LLM-facing text, not JSON or shell.
std::string Substitute(const std::string& tpl,
                       const std::map<std::string, std::string>& args)
{
	std::string out;
	out.reserve(tpl.size() + 64);
	std::size_t i = 0;
	while (i < tpl.size()) {
		const char c = tpl[i];
		if (c == '{') {
			const std::size_t close = tpl.find('}', i + 1);
			if (close != std::string::npos) {
				const std::string key = tpl.substr(i + 1, close - i - 1);
				const auto it = args.find(key);
				if (it != args.end()) {
					out.append(it->second);
					i = close + 1;
					continue;
				}
				// Unknown placeholder — leave it raw so test failures
				// surface immediately instead of silently swallowing.
				out.append(tpl, i, close - i + 1);
				i = close + 1;
				continue;
			}
		}
		out.push_back(c);
		++i;
	}
	return out;
}

// MCP: build the `prompts/get` result envelope from a single user-role
// message. All 7 prompts emit exactly one user message — the host then
// feeds it to the LLM as the next-turn prefix.
nlohmann::json BuildEnvelope(const std::string& description,
                             const std::string& text)
{
	nlohmann::json content;
	content["type"] = "text";
	content["text"] = text;

	nlohmann::json msg;
	msg["role"]    = "user";
	msg["content"] = std::move(content);

	nlohmann::json out;
	out["description"] = description;
	out["messages"]    = nlohmann::json::array({ std::move(msg) });
	return out;
}

// MCP: helper — first non-empty value among the supplied args. Used so
// optional `synonym` defaults to `name`, etc. without polluting the
// template with conditional logic.
std::string FirstNonEmpty(const std::map<std::string, std::string>& args,
                          std::initializer_list<const char*> keys)
{
	for (const char* k : keys) {
		const auto it = args.find(k);
		if (it != args.end() && !it->second.empty()) return it->second;
	}
	return std::string();
}

// =========================================================================
// Prompt templates — Russian where it matches the natural authoring flow,
// English for tool names and code-shaped fragments.
// =========================================================================

constexpr const char* kTplNewCatalog =
	"Создай Catalog.{name} с синонимом \"{synonym}\".\n"
	"\n"
	"Recommended flow:\n"
	"1. Spросi у Sigma типичные реквизиты для этого business-domain:\n"
	"   llm_query persona=sigma \"стандартные реквизиты Catalog.{name}\"\n"
	"   Sigma вернёт список (Code, Description + domain-specific) с типами и длинами.\n"
	"2. Создай объект:\n"
	"   meta_create kind=Catalog fullName=Catalog.{name} \\\n"
	"               properties={{\"synonym\":\"{synonym}\"}}\n"
	"3. Для каждого attribute из ответа Sigma:\n"
	"   meta_create kind=Attribute \\\n"
	"               fullName=Catalog.{name}.Attributes.<AttrName> \\\n"
	"               properties={{\"type\":\"...\",\"length\":...}}\n"
	"4. Если домен предполагает иерархию или предопределённые значения —\n"
	"   уточни у Sigma и добавь HierarchicalCatalog flag + predefined_values_set.\n"
	"5. Проверь схему: sigma_check metadata={{kind:\"Catalog\",name:\"{name}\"}}\n"
	"6. Сохрани конфигурацию: save_config\n"
	"\n"
	"Используй oes://sigma-rules как reference по invariants — Σ-unique\n"
	"особенно актуален для Code. Не плоди реквизитов сверх того, что Sigma\n"
	"назвала: 1С-стиль — минимальная анемичная схема + расширение по запросу.";

constexpr const char* kTplNewDocument =
	"Создай Document.{name} с синонимом \"{synonym}\".\n"
	"{registerLine}"
	"\n"
	"Recommended flow:\n"
	"1. Sproci у Sigma структуру документа:\n"
	"   llm_query persona=sigma \"стандартные реквизиты Document.{name}\"\n"
	"   Sigma вернёт header attrs + tabular sections (Goods, Services, ...).\n"
	"2. Создай документ:\n"
	"   meta_create kind=Document fullName=Document.{name} \\\n"
	"               properties={{\"synonym\":\"{synonym}\"}}\n"
	"3. Для каждого header attribute:\n"
	"   meta_create kind=Attribute fullName=Document.{name}.Attributes.<X>\n"
	"4. Для каждой табличной части:\n"
	"   meta_create kind=TabularSection fullName=Document.{name}.TabularSections.<TS>\n"
	"   + meta_create kind=Attribute по каждой колонке.\n"
	"{postingBlock}"
	"5. Проверь invariants: sigma_check metadata={{kind:\"Document\",name:\"{name}\"}}\n"
	"6. save_config\n"
	"\n"
	"Document.Posting() owns the side-effect surface — keep it idempotent\n"
	"(reposting must produce the same register state). Используй\n"
	"oes://sigma-rules § Σ-balance как guard для accounting movements.";

constexpr const char* kTplNewDocumentPostingBlock =
	"4b. Напиши процедуру проведения (поднимет {register}):\n"
	"    write_module fullName=Document.{name}.ObjectModule\n"
	"    Контент модуля: Procedure Posting() с регистрацией движений в\n"
	"    {register} — Sigma подскажет shape записи (period, dimensions,\n"
	"    resources).\n";

constexpr const char* kTplMigrateBas =
	"Импортируй конфигурацию 1С/BAS из {path}{objectFilterClause}.\n"
	"\n"
	"Recommended flow:\n"
	"1. Сначала preview — НЕ применяй ничего:\n"
	"   import_bas_xml configurationPath={path} preview=true{objectFilterArg}\n"
	"   Сервер вернёт summary: сколько Catalogs / Documents / Registers /\n"
	"   Forms / Modules найдено, какие БЛОКИРУЮТ импорт (unsupported types,\n"
	"   conflicts с existing OES objects).\n"
	"2. Покажи summary пользователю. Спроси, что делать с conflicts:\n"
	"   - rename (имя в OES будет <Original>_imported)\n"
	"   - skip (импортируется всё кроме конфликтных)\n"
	"   - merge (опасно — overwrites OES side; требуется явное согласие)\n"
	"3. Если ОК — apply:\n"
	"   import_bas_xml configurationPath={path} preview=false \\\n"
	"                  conflictMode=<rename|skip|merge>{objectFilterArg}\n"
	"4. Прогоняй sigma_check + compile_check по импортированным объектам —\n"
	"   1С/BAS BSL не 1:1 совпадает с CES/VES; модули могут требовать\n"
	"   ручной правки (доменные функции, отсутствующие в OES системной\n"
	"   библиотеке).\n"
	"5. save_config — only после того как compile_check прошёл.\n"
	"\n"
	"Безопасность: импорт additive, но conflictMode=merge перезаписывает.\n"
	"Не пускай его без явного user confirmation. Логи импорта смотри в\n"
	"oes://config/current — server emits notifications/resources/updated\n"
	"после успешного save_config.";

constexpr const char* kTplExplainObject =
	"Объясни metadata объект {fullName} — что это, зачем нужен, как с ним\n"
	"работать.\n"
	"\n"
	"Recommended flow:\n"
	"1. Прочитай structure:\n"
	"   meta_query fullName={fullName}\n"
	"   Получишь kind, attributes, tabular sections, forms, modules, comments.\n"
	"2. Прочитай business-context у Sigma:\n"
	"   llm_query persona=sigma \"объясни роль {fullName} в типичной\n"
	"   business-domain конфигурации\"\n"
	"   Sigma знает 1С-наследие — она ответит про типичные use-cases,\n"
	"   связанные регистры, отчёты, и edge cases.\n"
	"3. Если объект имеет модули — прочитай ObjectModule + ManagerModule:\n"
	"   read_module fullName={fullName}.ObjectModule\n"
	"   read_module fullName={fullName}.ManagerModule\n"
	"4. Если объект — Document, проверь чем он проводится:\n"
	"   meta_query на ObjectModule, найди Posting() body, перечисли\n"
	"   затрагиваемые регистры.\n"
	"5. Собери ответ в двух частях:\n"
	"   a) Structured — kind, ключевые attributes (типы + назначение),\n"
	"      связи (Owner, RegisterRecord, ChartOfAccounts, ...).\n"
	"   b) Narrative — на естественном языке: что хранит, как редактируется\n"
	"      пользователем, в каких отчётах появляется, типичные ошибки.\n"
	"\n"
	"Цель — чтобы новый разработчик через 2 минуты понимал что трогать а\n"
	"что нет. Если объект deprecated или подозрительно anaemic — отметь.";

constexpr const char* kTplAuditSecurity =
	"Проведи security audit конфигурации: найди over-permissive grants и\n"
	"предложи tightening.\n"
	"\n"
	"Recommended flow:\n"
	"1. Перечисли все роли:\n"
	"   role_list\n"
	"2. Для каждой роли прочитай ACL:\n"
	"   role_acl_read fullName=Role.<RoleName>\n"
	"   Получишь permissions[]: {object, kind, rights:[Read/Insert/Update/\n"
	"   Delete/Execute/...]}\n"
	"3. Найди over-permissive паттерны:\n"
	"   - Role с Delete на бизнес-критичных Catalogs (Контрагенты, Товары)\n"
	"     без явного operational оснований.\n"
	"   - Role с Execute на Reports/DataProcessors, которые могут менять\n"
	"     данные через Posting()/SetValue().\n"
	"   - Любая роль с правами на ВСЕ объекты (wildcard grant).\n"
	"   - Анонимная (системная) роль с любыми Write правами.\n"
	"4. Cross-check с предопределёнными пользователями (если есть):\n"
	"   meta_query fullName=Catalog.Users (или аналог) — кто к каким\n"
	"   ролям привязан.\n"
	"5. Собери отчёт:\n"
	"   - Каждый finding: severity (P0/P1/P2), role, объект, право, риск,\n"
	"     рекомендация (revoke / narrow scope / split role).\n"
	"   - Top-3 fixes ranked by impact.\n"
	"6. НЕ применяй role_acl_set автоматически — security changes требуют\n"
	"   explicit user confirmation. Surfaceей предложения, пусть customer\n"
	"   решит.\n"
	"\n"
	"Reference invariants: oes://sigma-rules. Дополнительно проверь, что\n"
	"критичные модули (Posting, ObjectModule с DB writes) защищены\n"
	"compile_check + sigma_check перед roll-out.";

constexpr const char* kTplWriteReport =
	"Напиши Report под описание: \"{description}\".\n"
	"\n"
	"Recommended flow:\n"
	"1. Спроси у Sigma структуру:\n"
	"   llm_query persona=sigma \"стандартная структура отчёта для\n"
	"   {description}\"\n"
	"   Sigma вернёт: какие исходные данные нужны (Catalogs, Registers),\n"
	"   query template (с группировками и суммами), layout suggestion\n"
	"   (column order, totals, filters).\n"
	"2. Создай объект отчёта:\n"
	"   meta_create kind=Report fullName=Report.<Name>\n"
	"3. Запиши модуль отчёта с query + ComposeData() body:\n"
	"   write_module fullName=Report.<Name>.ObjectModule\n"
	"   Body должен:\n"
	"   - построить запрос через QueryBuilder или прямой SELECT (через\n"
	"     ibPreparedStatement — НЕ строковая склейка),\n"
	"   - вернуть ValueTable / ResultSet с колонками из layout,\n"
	"   - корректно обрабатывать period filter (если отчёт за период).\n"
	"4. Проверь синтаксис:\n"
	"   compile_check fullName=Report.<Name>.ObjectModule\n"
	"5. Проверь business invariants:\n"
	"   sigma_check metadata={{kind:\"Report\",name:\"<Name>\"}}\n"
	"6. save_config\n"
	"\n"
	"Layout authoring (визуальная форма отчёта) сейчас DEFERRED — текущая\n"
	"версия oes-mcp не пишет form layout (см. form_layout_* tools, t1-002\n"
	"stub). Отчёт будет открыт через стандартный generated layout. Это\n"
	"приемлемо для MVP — кастомный layout добавляется в Designer вручную.";

constexpr const char* kTplWriteForm =
	"Напиши форму {formKind} для {objectFullName}.\n"
	"\n"
	"Recommended flow:\n"
	"1. Прочитай structure владельца:\n"
	"   meta_query fullName={objectFullName}\n"
	"   Получишь attributes + tabular sections — это базовый набор\n"
	"   контролов формы.\n"
	"2. Сproci у Sigma:\n"
	"   llm_query persona=sigma \"типичная форма {formKind} для\n"
	"   {objectFullName}\"\n"
	"   Sigma подскажет:\n"
	"   - какие attributes важные (выводить крупно), какие — в Other\n"
	"     группе,\n"
	"   - какие требуют choice форму (Refer типы),\n"
	"   - какие имеют типичный OnChange-handler (пересчёт суммы из\n"
	"     количества*цены и т.п.).\n"
	"3. Создай форму:\n"
	"   meta_create kind=Form fullName={objectFullName}.Forms.{formKind}\n"
	"4. Запиши form module с обработчиками:\n"
	"   write_module fullName={objectFullName}.Forms.{formKind}.Module\n"
	"   Минимум: Procedure OnOpen() — initial values, filters.\n"
	"   По запросу Sigma добавь OnChange-handlers для расчётных полей.\n"
	"5. compile_check на модуле формы.\n"
	"6. sigma_check + save_config.\n"
	"\n"
	"ВНИМАНИЕ: визуальный layout формы (расположение контролов, размеры,\n"
	"группы, табы) сейчас DEFERRED — form_layout_set возвращает isError\n"
	"со ссылкой на t1-002 stub. Текущая версия пишет ТОЛЬКО module-only\n"
	"форму; layout будет сгенерирован дефолтный (по attributes владельца).\n"
	"Customer сможет открыть форму в Designer и поправить визуал вручную.\n"
	"Документируй это ограничение в коммите.";

// =========================================================================
// Renderers — one per prompt. They validate required args, then call
// Substitute() + BuildEnvelope(). Missing required args throw a JSON
// error envelope which the dispatcher re-emits as JSON-RPC -32602.
// =========================================================================

void RequireArg(const std::map<std::string, std::string>& args,
                const std::string& key,
                const std::string& promptName)
{
	const auto it = args.find(key);
	if (it == args.end() || it->second.empty()) {
		nlohmann::json err;
		err["code"]    = errc::kInvalidParams;
		err["message"] = "prompts/get " + promptName + ": missing required "
		                  "argument '" + key + "'";
		throw err;
	}
}

nlohmann::json RenderNewCatalog(const std::map<std::string, std::string>& a)
{
	RequireArg(a, "name", "oes:new-catalog");
	std::map<std::string, std::string> bound = a;
	if (bound["synonym"].empty()) bound["synonym"] = bound["name"];
	const std::string text = Substitute(kTplNewCatalog, bound);
	return BuildEnvelope(
		"Создание Catalog с Sigma-driven attributes + sigma_check + save",
		text);
}

nlohmann::json RenderNewDocument(const std::map<std::string, std::string>& a)
{
	RequireArg(a, "name", "oes:new-document");
	std::map<std::string, std::string> bound = a;
	if (bound["synonym"].empty()) bound["synonym"] = bound["name"];
	const std::string reg = FirstNonEmpty(a, {"register"});
	bound["registerLine"]  = reg.empty()
		? std::string()
		: "С движениями в регистр " + reg + ".\n";
	if (!reg.empty()) {
		std::map<std::string, std::string> postingArgs = bound;
		postingArgs["register"] = reg;
		bound["postingBlock"] = Substitute(kTplNewDocumentPostingBlock,
		                                    postingArgs);
	} else {
		bound["postingBlock"] = std::string();
	}
	const std::string text = Substitute(kTplNewDocument, bound);
	return BuildEnvelope(
		"Создание Document (header + tabular sections + optional Posting)",
		text);
}

nlohmann::json RenderMigrateBas(const std::map<std::string, std::string>& a)
{
	RequireArg(a, "path", "oes:migrate-1c-xml");
	std::map<std::string, std::string> bound = a;
	const std::string filter = FirstNonEmpty(a, {"objectFilter"});
	bound["objectFilterClause"] = filter.empty()
		? std::string()
		: " с фильтром объектов '" + filter + "'";
	bound["objectFilterArg"] = filter.empty()
		? std::string()
		: " objectFilter=\"" + filter + "\"";
	const std::string text = Substitute(kTplMigrateBas, bound);
	return BuildEnvelope(
		"Импорт 1С/BAS конфигурации с preview-first flow и conflict resolution",
		text);
}

nlohmann::json RenderExplainObject(const std::map<std::string, std::string>& a)
{
	RequireArg(a, "fullName", "oes:explain-object");
	const std::string text = Substitute(kTplExplainObject, a);
	return BuildEnvelope(
		"Структурное + narrative объяснение metadata объекта",
		text);
}

nlohmann::json RenderAuditSecurity(const std::map<std::string, std::string>& a)
{
	(void)a;  // no arguments — operates on full config
	return BuildEnvelope(
		"Security audit: enumerate roles, find over-permissive grants, "
		"propose tightening",
		std::string(kTplAuditSecurity));
}

nlohmann::json RenderWriteReport(const std::map<std::string, std::string>& a)
{
	RequireArg(a, "description", "oes:write-report");
	const std::string text = Substitute(kTplWriteReport, a);
	return BuildEnvelope(
		"Создание Report: Sigma-driven query + module + compile + sigma",
		text);
}

nlohmann::json RenderWriteForm(const std::map<std::string, std::string>& a)
{
	RequireArg(a, "objectFullName", "oes:write-form");
	std::map<std::string, std::string> bound = a;
	if (bound["formKind"].empty()) bound["formKind"] = "ItemForm";
	const std::string text = Substitute(kTplWriteForm, bound);
	return BuildEnvelope(
		"Создание Form (module-only — layout DEFERRED per t1-002 stub)",
		text);
}

} // anonymous namespace

// =========================================================================
// Public API
// =========================================================================

void PromptRegistry::Register(PromptDescriptor desc)
{
	for (auto& e : m_entries) {
		if (e.name == desc.name) {
			e = std::move(desc);
			return;
		}
	}
	m_entries.push_back(std::move(desc));
}

std::vector<PromptDescriptor> PromptRegistry::List() const
{
	return m_entries;
}

const PromptDescriptor* PromptRegistry::Find(const std::string& name) const
{
	for (const auto& e : m_entries) {
		if (e.name == name) return &e;
	}
	return nullptr;
}

nlohmann::json PromptRegistry::Get(const std::string& name,
                                    const std::map<std::string, std::string>& args) const
{
	const PromptDescriptor* p = Find(name);
	if (p == nullptr) {
		nlohmann::json err;
		err["code"]    = errc::kInvalidParams;
		err["message"] = "prompts/get: unknown prompt '" + name + "'";
		throw err;
	}
	// Registry-level required-arg check before invoking the renderer so
	// the diagnostic mentions the prompt name + missing key uniformly.
	for (const auto& arg : p->arguments) {
		if (!arg.required) continue;
		const auto it = args.find(arg.name);
		if (it == args.end() || it->second.empty()) {
			nlohmann::json err;
			err["code"]    = errc::kInvalidParams;
			err["message"] = "prompts/get " + name + ": missing required "
			                  "argument '" + arg.name + "'";
			throw err;
		}
	}
	return p->render(args);
}

PromptRegistry& AllPrompts()
{
	static PromptRegistry reg;
	static std::once_flag init;
	std::call_once(init, [] {
		// 1) oes:new-catalog
		{
			PromptDescriptor d;
			d.name        = "oes:new-catalog";
			d.description = "Create a new Catalog with Sigma-recommended "
			                 "attributes, then sigma_check + save_config.";
			d.arguments   = {
				{ "name",    "Catalog name (e.g. 'Контрагенты' or 'Goods')", true  },
				{ "synonym", "User-facing label; defaults to name when omitted", false },
			};
			d.render      = &RenderNewCatalog;
			reg.Register(std::move(d));
		}
		// 2) oes:new-document
		{
			PromptDescriptor d;
			d.name        = "oes:new-document";
			d.description = "Create a new Document with header attrs + "
			                 "tabular sections + optional Posting() body.";
			d.arguments   = {
				{ "name",     "Document name (e.g. 'РеализацияТоваров')", true  },
				{ "synonym",  "User-facing label; defaults to name",      false },
				{ "register", "Register name for Posting() movements "
				              "(e.g. 'AccumulationRegister.Остатки'); "
				              "omit for non-posting documents",           false },
			};
			d.render      = &RenderNewDocument;
			reg.Register(std::move(d));
		}
		// 3) oes:migrate-1c-xml
		{
			PromptDescriptor d;
			d.name        = "oes:migrate-1c-xml";
			d.description = "Preview + apply a 1С/BAS Configuration.xml "
			                 "import with conflict resolution.";
			d.arguments   = {
				{ "path",         "Filesystem path to Configuration.xml", true  },
				{ "objectFilter", "Optional glob to scope import "
				                  "(e.g. 'Catalog.*' or 'Document.Реал*')", false },
			};
			d.render      = &RenderMigrateBas;
			reg.Register(std::move(d));
		}
		// 4) oes:explain-object
		{
			PromptDescriptor d;
			d.name        = "oes:explain-object";
			d.description = "Explain a metadata object's structure + "
			                 "business-domain role (structured + narrative).";
			d.arguments   = {
				{ "fullName", "Object full name (e.g. 'Catalog.Контрагенты')", true },
			};
			d.render      = &RenderExplainObject;
			reg.Register(std::move(d));
		}
		// 5) oes:audit-security
		{
			PromptDescriptor d;
			d.name        = "oes:audit-security";
			d.description = "Audit role ACLs for over-permissive grants; "
			                 "propose tightening (no auto-apply).";
			d.arguments   = {};  // operates on full config
			d.render      = &RenderAuditSecurity;
			reg.Register(std::move(d));
		}
		// 6) oes:write-report
		{
			PromptDescriptor d;
			d.name        = "oes:write-report";
			d.description = "Create a Report from a natural-language spec; "
			                 "Sigma-driven query + module + compile + sigma.";
			d.arguments   = {
				{ "description", "Natural-language spec of the report "
				                 "(e.g. 'продажи по контрагентам за месяц')", true },
			};
			d.render      = &RenderWriteReport;
			reg.Register(std::move(d));
		}
		// 7) oes:write-form
		{
			PromptDescriptor d;
			d.name        = "oes:write-form";
			d.description = "Create a Form for a Catalog/Document "
			                 "(module-only — layout authoring DEFERRED).";
			d.arguments   = {
				{ "objectFullName", "Owner full name "
				                    "(e.g. 'Catalog.Контрагенты')",      true  },
				{ "formKind",       "Form kind (ItemForm/ListForm/"
				                    "ChoiceForm/...); defaults to ItemForm", false },
			};
			d.render      = &RenderWriteForm;
			reg.Register(std::move(d));
		}
	});
	return reg;
}

} // namespace mcpServer
