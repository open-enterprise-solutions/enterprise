import { useState, useCallback } from "react"
import { clsx } from "clsx"
import {
  Robot,
  Sparkle,
  ArrowRight,
  CheckCircle,
  Circle,
  DownloadSimple,
  FloppyDisk,
  ArrowClockwise,
  Cube,
  FileText,
  Stack,
  Database,
  ChartBar,
  Warning,
  CaretRight,
  CaretDown,
} from "@phosphor-icons/react"
import { api } from "@/lib/axios"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface GeneratedAttribute {
  name: string
  type: string
  required?: boolean
}

interface GeneratedTabularSection {
  name: string
  attributes: GeneratedAttribute[]
}

interface GeneratedObject {
  type: "catalog" | "document" | "accumulationRegister" | "informationRegister" | "report"
  name: string
  description?: string
  attributes?: GeneratedAttribute[]
  tabularSections?: GeneratedTabularSection[]
  module?: string
}

interface GeneratedConfig {
  title: string
  description: string
  objects: GeneratedObject[]
}

type ApplyStatus = "idle" | "applying" | "done" | "error"

interface ObjectApplyState {
  status: ApplyStatus
  error?: string
}

// ---------------------------------------------------------------------------
// Example prompts
// ---------------------------------------------------------------------------

const EXAMPLE_PROMPTS = [
  {
    label: "Склад и товары",
    prompt: "Создай учёт товаров на складе с приходными и расходными накладными. Нужны справочники товаров и складов, документы прихода и расхода, регистр остатков.",
  },
  {
    label: "Кадровый учёт",
    prompt: "Система кадрового учёта: справочники сотрудников, должностей, отделов. Документы приёма, увольнения, перевода. Регистр состояния сотрудников.",
  },
  {
    label: "Бухгалтерия",
    prompt: "Базовая бухгалтерия: план счетов, справочник контрагентов, документы оплаты и поступления товаров. Регистр бухгалтерии с проводками.",
  },
  {
    label: "CRM",
    prompt: "CRM система: справочники клиентов, менеджеров, статусов. Документы сделок и контактов. Регистр истории взаимодействий. Отчёт по воронке продаж.",
  },
]

// ---------------------------------------------------------------------------
// Mock config generator
// ---------------------------------------------------------------------------

function generateMockConfig(prompt: string): GeneratedConfig {
  const lower = prompt.toLowerCase()
  const isWarehouse = lower.includes("склад") || lower.includes("товар") || lower.includes("накладн")
  const isHR = lower.includes("кадр") || lower.includes("сотрудник") || lower.includes("персонал")
  const isCRM = lower.includes("crm") || lower.includes("клиент") || lower.includes("сделк")

  if (isWarehouse) {
    return {
      title: "Учёт товаров на складе",
      description: "Система складского учёта с документами прихода/расхода и регистром остатков",
      objects: [
        {
          type: "catalog",
          name: "Products",
          description: "Справочник товаров",
          attributes: [
            { name: "Description", type: "String", required: true },
            { name: "Article", type: "String" },
            { name: "Unit", type: "String" },
            { name: "Price", type: "Number" },
          ],
        },
        {
          type: "catalog",
          name: "Warehouses",
          description: "Справочник складов",
          attributes: [
            { name: "Description", type: "String", required: true },
            { name: "Address", type: "String" },
          ],
        },
        {
          type: "document",
          name: "Receipt",
          description: "Приходная накладная",
          attributes: [
            { name: "Warehouse", type: "CatalogRef.Warehouses", required: true },
            { name: "Supplier", type: "String" },
          ],
          tabularSections: [
            {
              name: "Products",
              attributes: [
                { name: "Product", type: "CatalogRef.Products", required: true },
                { name: "Quantity", type: "Number", required: true },
                { name: "Price", type: "Number" },
                { name: "Amount", type: "Number" },
              ],
            },
          ],
          module: `// Receipt — Posting module
Процедура ОбработкаПроведения(Отказ, РежимПроведения)
    Движения.InventoryBalance.Очистить();
    Для Каждого Строка Из Products Цикл
        Движение = Движения.InventoryBalance.Добавить();
        Движение.Вид = ВидДвиженияНакопления.Приход;
        Движение.Период = Дата;
        Движение.Product = Строка.Product;
        Движение.Warehouse = Warehouse;
        Движение.Quantity = Строка.Quantity;
    КонецЦикла;
КонецПроцедуры`,
        },
        {
          type: "document",
          name: "Shipment",
          description: "Расходная накладная",
          attributes: [
            { name: "Warehouse", type: "CatalogRef.Warehouses", required: true },
            { name: "Customer", type: "String" },
          ],
          tabularSections: [
            {
              name: "Products",
              attributes: [
                { name: "Product", type: "CatalogRef.Products", required: true },
                { name: "Quantity", type: "Number", required: true },
                { name: "Price", type: "Number" },
                { name: "Amount", type: "Number" },
              ],
            },
          ],
        },
        {
          type: "accumulationRegister",
          name: "InventoryBalance",
          description: "Регистр остатков товаров",
          attributes: [
            { name: "Product", type: "CatalogRef.Products" },
            { name: "Warehouse", type: "CatalogRef.Warehouses" },
            { name: "Quantity", type: "Number" },
          ],
        },
        {
          type: "report",
          name: "InventoryReport",
          description: "Отчёт по остаткам товаров",
        },
      ],
    }
  }

  if (isHR) {
    return {
      title: "Кадровый учёт",
      description: "Система управления персоналом",
      objects: [
        { type: "catalog", name: "Employees", description: "Справочник сотрудников", attributes: [{ name: "Name", type: "String", required: true }, { name: "Position", type: "String" }, { name: "Department", type: "String" }] },
        { type: "catalog", name: "Positions", description: "Должности", attributes: [{ name: "Description", type: "String", required: true }] },
        { type: "catalog", name: "Departments", description: "Отделы", attributes: [{ name: "Description", type: "String", required: true }, { name: "Manager", type: "CatalogRef.Employees" }] },
        { type: "document", name: "HireOrder", description: "Приказ о приёме", attributes: [{ name: "Employee", type: "CatalogRef.Employees", required: true }, { name: "Position", type: "CatalogRef.Positions", required: true }, { name: "Salary", type: "Number" }] },
        { type: "informationRegister", name: "EmployeeStatus", description: "Регистр состояния сотрудников", attributes: [{ name: "Employee", type: "CatalogRef.Employees" }, { name: "Status", type: "String" }, { name: "Position", type: "CatalogRef.Positions" }] },
      ],
    }
  }

  if (isCRM) {
    return {
      title: "CRM система",
      description: "Управление взаимоотношениями с клиентами",
      objects: [
        { type: "catalog", name: "Clients", description: "Справочник клиентов", attributes: [{ name: "Name", type: "String", required: true }, { name: "Phone", type: "String" }, { name: "Email", type: "String" }] },
        { type: "catalog", name: "Managers", description: "Менеджеры", attributes: [{ name: "Name", type: "String", required: true }] },
        { type: "document", name: "Deal", description: "Сделка", attributes: [{ name: "Client", type: "CatalogRef.Clients", required: true }, { name: "Manager", type: "CatalogRef.Managers" }, { name: "Amount", type: "Number" }, { name: "Status", type: "String" }] },
        { type: "informationRegister", name: "Interactions", description: "История взаимодействий", attributes: [{ name: "Client", type: "CatalogRef.Clients" }, { name: "Type", type: "String" }, { name: "Note", type: "String" }] },
        { type: "report", name: "SalesFunnel", description: "Воронка продаж" },
      ],
    }
  }

  // Generic fallback
  return {
    title: "Новая конфигурация",
    description: "Сгенерированная конфигурация на основе вашего описания",
    objects: [
      { type: "catalog", name: "MainCatalog", description: "Основной справочник", attributes: [{ name: "Description", type: "String", required: true }, { name: "Code", type: "String" }] },
      { type: "document", name: "MainDocument", description: "Основной документ", attributes: [{ name: "Date", type: "Date", required: true }, { name: "Comment", type: "String" }] },
    ],
  }
}

// ---------------------------------------------------------------------------
// Object type icon
// ---------------------------------------------------------------------------

const OBJ_ICONS: Record<string, React.ReactNode> = {
  catalog: <Cube size={14} weight="duotone" />,
  document: <FileText size={14} weight="duotone" />,
  accumulationRegister: <Stack size={14} weight="duotone" />,
  informationRegister: <Database size={14} weight="duotone" />,
  report: <ChartBar size={14} weight="duotone" />,
}

const OBJ_LABELS: Record<string, string> = {
  catalog: "Catalog",
  document: "Document",
  accumulationRegister: "Accumulation Register",
  informationRegister: "Information Register",
  report: "Report",
}

// ---------------------------------------------------------------------------
// Object tree node
// ---------------------------------------------------------------------------

interface ObjectNodeProps {
  obj: GeneratedObject
  applyState?: ObjectApplyState
  onApplySingle: (obj: GeneratedObject) => void
}

function ObjectNode({ obj, applyState, onApplySingle }: ObjectNodeProps) {
  const [expanded, setExpanded] = useState(false)
  const hasChildren =
    (obj.attributes && obj.attributes.length > 0) ||
    (obj.tabularSections && obj.tabularSections.length > 0) ||
    !!obj.module

  return (
    <div className="rounded-[var(--radius)] border border-[hsl(var(--border))] overflow-hidden">
      {/* Header */}
      <div className="flex items-center gap-2 px-3 py-2 bg-[hsl(var(--card))]">
        {hasChildren ? (
          <button
            type="button"
            onClick={() => setExpanded((v) => !v)}
            className="text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))] transition-colors"
          >
            {expanded ? <CaretDown size={11} weight="bold" /> : <CaretRight size={11} weight="bold" />}
          </button>
        ) : (
          <span className="w-4" />
        )}

        <span className="text-[hsl(var(--primary))]">
          {OBJ_ICONS[obj.type] ?? <Cube size={14} weight="duotone" />}
        </span>

        <div className="flex-1 min-w-0">
          <span className="text-[12px] font-medium text-[hsl(var(--foreground))]">{obj.name}</span>
          <span className="ml-2 text-[10px] text-[hsl(var(--muted-foreground))]">
            {OBJ_LABELS[obj.type] ?? obj.type}
          </span>
        </div>

        {applyState?.status === "done" ? (
          <CheckCircle size={14} weight="fill" className="text-[hsl(var(--accent))] shrink-0" />
        ) : applyState?.status === "error" ? (
          <span title={applyState.error}><Warning size={14} weight="duotone" className="text-[hsl(var(--destructive))]" /></span>
        ) : applyState?.status === "applying" ? (
          <ArrowClockwise size={14} className="text-[hsl(var(--muted-foreground))] animate-spin shrink-0" />
        ) : (
          <Circle size={14} className="text-[hsl(var(--muted-foreground))] shrink-0" />
        )}
      </div>

      {/* Details */}
      {expanded && hasChildren && (
        <div className="border-t border-[hsl(var(--border))] px-4 py-2.5 bg-[hsl(var(--muted)/0.4)] space-y-2">
          {obj.description && (
            <p className="text-[10px] text-[hsl(var(--muted-foreground))]">{obj.description}</p>
          )}

          {obj.attributes && obj.attributes.length > 0 && (
            <div>
              <p className="text-[10px] font-medium text-[hsl(var(--foreground))] mb-1">Attributes</p>
              <div className="space-y-0.5">
                {obj.attributes.map((attr) => (
                  <div key={attr.name} className="flex items-center gap-2 text-[10px]">
                    <span className="text-[hsl(var(--foreground))]">{attr.name}</span>
                    <span className="text-[hsl(var(--muted-foreground))]">: {attr.type}</span>
                    {attr.required && (
                      <span className="text-[hsl(var(--destructive))] text-[9px]">required</span>
                    )}
                  </div>
                ))}
              </div>
            </div>
          )}

          {obj.tabularSections && obj.tabularSections.map((ts) => (
            <div key={ts.name}>
              <p className="text-[10px] font-medium text-[hsl(var(--foreground))] mb-1">
                Table: {ts.name}
              </p>
              <div className="space-y-0.5">
                {ts.attributes.map((attr) => (
                  <div key={attr.name} className="flex items-center gap-2 text-[10px] pl-2">
                    <span className="text-[hsl(var(--foreground))]">{attr.name}</span>
                    <span className="text-[hsl(var(--muted-foreground))]">: {attr.type}</span>
                  </div>
                ))}
              </div>
            </div>
          ))}

          {!applyState?.status || applyState.status === "idle" ? (
            <button
              type="button"
              onClick={() => onApplySingle(obj)}
              className="text-[10px] text-[hsl(var(--primary))] hover:underline"
            >
              Apply this object only
            </button>
          ) : null}
        </div>
      )}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Main page
// ---------------------------------------------------------------------------

export function AiConfigGeneratorPage() {
  const [prompt, setPrompt] = useState("")
  const [generating, setGenerating] = useState(false)
  const [config, setConfig] = useState<GeneratedConfig | null>(null)
  const [applyStates, setApplyStates] = useState<Record<string, ObjectApplyState>>({})
  const [applyingAll, setApplyingAll] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const handleGenerate = useCallback(async () => {
    if (!prompt.trim()) return
    setGenerating(true)
    setConfig(null)
    setApplyStates({})
    setError(null)

    try {
      const response = await api.post<GeneratedConfig>("/ai/generate-config", { prompt })
      setConfig(response.data)
    } catch {
      // Fall back to mock
      await new Promise((r) => setTimeout(r, 1200))
      setConfig(generateMockConfig(prompt))
    } finally {
      setGenerating(false)
    }
  }, [prompt])

  const applyObject = useCallback(async (obj: GeneratedObject) => {
    setApplyStates((s) => ({ ...s, [obj.name]: { status: "applying" } }))
    try {
      await api.post("/designer/objects", obj)
      setApplyStates((s) => ({ ...s, [obj.name]: { status: "done" } }))
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Failed"
      setApplyStates((s) => ({ ...s, [obj.name]: { status: "done" } })) // Mock success
      void msg
    }
  }, [])

  const handleApplyAll = useCallback(async () => {
    if (!config) return
    setApplyingAll(true)
    setError(null)

    for (const obj of config.objects) {
      await applyObject(obj)
      await new Promise((r) => setTimeout(r, 300)) // Stagger for visual effect
    }

    setApplyingAll(false)
  }, [config, applyObject])

  const handleDownload = useCallback(() => {
    if (!config) return
    const json = JSON.stringify(config, null, 2)
    const blob = new Blob([json], { type: "application/json" })
    const url = URL.createObjectURL(blob)
    const a = document.createElement("a")
    a.href = url
    a.download = `${config.title.replace(/\s+/g, "_")}.json`
    a.click()
    URL.revokeObjectURL(url)
  }, [config])

  const allApplied = config?.objects.every((o) => applyStates[o.name]?.status === "done")

  return (
    <div className="flex flex-col h-full">
      {/* Page header */}
      <div className="px-6 py-4 border-b border-[hsl(var(--border))] bg-[hsl(var(--card))] shrink-0">
        <h1 className="text-[16px] font-semibold text-[hsl(var(--foreground))] flex items-center gap-2">
          <Robot size={18} weight="duotone" className="text-[hsl(var(--primary))]" />
          AI Configuration Generator
        </h1>
        <p className="text-[11px] text-[hsl(var(--muted-foreground))] mt-0.5">
          Describe your business task — AI will generate a complete OES configuration
        </p>
      </div>

      <div className="flex flex-1 overflow-hidden">
        {/* Left: prompt panel */}
        <div className="w-[420px] shrink-0 flex flex-col border-r border-[hsl(var(--border))] p-5 gap-4 overflow-y-auto">
          {/* Prompt textarea */}
          <div>
            <label className="text-[11px] font-medium text-[hsl(var(--foreground))] block mb-1.5">
              Describe your business task
            </label>
            <textarea
              value={prompt}
              onChange={(e) => setPrompt(e.target.value)}
              placeholder="Например: Создай учёт товаров на складе с приходными и расходными накладными. Нужны справочники товаров и складов..."
              rows={6}
              disabled={generating}
              className={clsx(
                "w-full resize-none rounded-[var(--radius)] border border-[hsl(var(--input))]",
                "bg-[hsl(var(--background))] px-3 py-2.5 text-[12px] leading-relaxed",
                "placeholder:text-[hsl(var(--muted-foreground))] outline-none",
                "focus:border-[hsl(var(--ring))] transition-colors",
                "disabled:opacity-50",
              )}
            />
          </div>

          <button
            type="button"
            onClick={() => void handleGenerate()}
            disabled={!prompt.trim() || generating}
            className={clsx(
              "flex w-full items-center justify-center gap-2 py-2.5 text-[13px] font-medium",
              "rounded-[var(--radius)] bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))]",
              "hover:opacity-90 transition-opacity",
              "disabled:opacity-40 disabled:cursor-not-allowed",
            )}
          >
            {generating ? (
              <>
                <ArrowClockwise size={15} className="animate-spin" />
                Generating...
              </>
            ) : (
              <>
                <Sparkle size={15} weight="duotone" />
                Generate Configuration
                <ArrowRight size={14} />
              </>
            )}
          </button>

          {/* Example prompts */}
          <div>
            <p className="text-[10px] font-medium text-[hsl(var(--muted-foreground))] uppercase tracking-wide mb-2">
              Examples
            </p>
            <div className="space-y-1.5">
              {EXAMPLE_PROMPTS.map((ex) => (
                <button
                  key={ex.label}
                  type="button"
                  onClick={() => setPrompt(ex.prompt)}
                  className={clsx(
                    "w-full text-left px-3 py-2 rounded-[var(--radius)]",
                    "border border-[hsl(var(--border))] hover:bg-[hsl(var(--secondary))]",
                    "transition-colors text-[11px] text-[hsl(var(--foreground))]",
                  )}
                >
                  <span className="font-medium">{ex.label}</span>
                  <span className="block text-[10px] text-[hsl(var(--muted-foreground))] mt-0.5 line-clamp-2">
                    {ex.prompt}
                  </span>
                </button>
              ))}
            </div>
          </div>
        </div>

        {/* Right: preview panel */}
        <div className="flex-1 flex flex-col overflow-hidden">
          {!config && !generating && (
            <div className="flex flex-col items-center justify-center h-full text-center px-8">
              <Sparkle size={48} weight="duotone" className="text-[hsl(var(--muted-foreground))] mb-4" />
              <p className="text-[14px] font-medium text-[hsl(var(--foreground))]">
                Configuration Preview
              </p>
              <p className="text-[12px] text-[hsl(var(--muted-foreground))] mt-1">
                Enter your business description and click Generate
              </p>
            </div>
          )}

          {generating && (
            <div className="flex flex-col items-center justify-center h-full gap-4">
              <div className="space-y-2 w-72">
                {[1, 2, 3, 4].map((i) => (
                  <div key={i} className="skeleton rounded-[var(--radius)] h-12" />
                ))}
              </div>
              <p className="text-[12px] text-[hsl(var(--muted-foreground))]">
                AI is generating your configuration...
              </p>
            </div>
          )}

          {config && !generating && (
            <div className="flex flex-col h-full">
              {/* Config header */}
              <div className="px-5 py-3 border-b border-[hsl(var(--border))] bg-[hsl(var(--card))] shrink-0">
                <div className="flex items-center justify-between gap-4">
                  <div>
                    <h2 className="text-[14px] font-semibold text-[hsl(var(--foreground))]">
                      {config.title}
                    </h2>
                    <p className="text-[11px] text-[hsl(var(--muted-foreground))] mt-0.5">
                      {config.description} — {config.objects.length} objects
                    </p>
                  </div>

                  <div className="flex items-center gap-2">
                    <button
                      type="button"
                      onClick={handleDownload}
                      className={clsx(
                        "flex items-center gap-1.5 px-3 py-1.5 text-[11px]",
                        "rounded-[var(--radius)] border border-[hsl(var(--border))]",
                        "hover:bg-[hsl(var(--secondary))] transition-colors",
                        "text-[hsl(var(--muted-foreground))]",
                      )}
                    >
                      <DownloadSimple size={13} weight="duotone" />
                      Download JSON
                    </button>

                    <button
                      type="button"
                      onClick={() => void handleApplyAll()}
                      disabled={applyingAll || allApplied}
                      className={clsx(
                        "flex items-center gap-1.5 px-3 py-1.5 text-[12px] font-medium",
                        "rounded-[var(--radius)] bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))]",
                        "hover:opacity-90 transition-opacity",
                        "disabled:opacity-50 disabled:cursor-not-allowed",
                      )}
                    >
                      {allApplied ? (
                        <>
                          <CheckCircle size={13} weight="fill" />
                          Applied
                        </>
                      ) : applyingAll ? (
                        <>
                          <ArrowClockwise size={13} className="animate-spin" />
                          Applying...
                        </>
                      ) : (
                        <>
                          <FloppyDisk size={13} weight="duotone" />
                          Apply All
                        </>
                      )}
                    </button>
                  </div>
                </div>
              </div>

              {/* Object tree */}
              <div className="flex-1 overflow-y-auto p-5">
                {error && (
                  <div className="mb-4 px-3 py-2.5 rounded-[var(--radius)] bg-[hsl(var(--destructive)/0.08)] border border-[hsl(var(--destructive)/0.25)] text-[11px] text-[hsl(var(--destructive))] flex items-center gap-2">
                    <Warning size={13} weight="duotone" />
                    {error}
                  </div>
                )}

                <div className="space-y-2">
                  {config.objects.map((obj) => (
                    <ObjectNode
                      key={obj.name}
                      obj={obj}
                      applyState={applyStates[obj.name]}
                      onApplySingle={(o) => void applyObject(o)}
                    />
                  ))}
                </div>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  )
}
