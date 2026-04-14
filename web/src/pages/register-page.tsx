/**
 * RegisterPage — renders a register viewer for information, accumulation and accounting registers.
 *
 * Route: /:section/:resource  (when section is *Registers)
 * Loads register metadata from /api/metadata/:section/:id to get dimensions/resources.
 * Renders RegisterView with PeriodSelector and appropriate mode toggles.
 */

import { useEffect } from "react"
import { useParams } from "react-router-dom"
import { useOne } from "@refinedev/core"
import { Database, Warning } from "@phosphor-icons/react"
import { useTabStore } from "@/stores/tab-store"
import {
  RegisterView,
  type RegisterType,
  type RegisterDimension,
  type RegisterResource,
} from "@/components/reports/RegisterView"
import { PrintTemplate } from "@/components/reports/PrintTemplate"

// ---------------------------------------------------------------------------
// Types — expected shape of /api/metadata/:section/:id
// ---------------------------------------------------------------------------

interface RegisterMeta {
  id: string
  name: string
  registerType?: RegisterType // "information" | "accumulation" | "accounting"
  dimensions?: RegisterDimension[]
  resources?: RegisterResource[]
  attributes?: { key: string; label: string; type?: string }[]
}

// ---------------------------------------------------------------------------
// Determine RegisterType from section name
// ---------------------------------------------------------------------------

function sectionToRegisterType(section: string): RegisterType {
  if (section === "accumulationRegisters") return "accumulation"
  if (section === "accountingRegisters")   return "accounting"
  return "information"
}

// ---------------------------------------------------------------------------
// Skeleton
// ---------------------------------------------------------------------------

function RegisterPageSkeleton() {
  return (
    <div className="flex flex-col h-full">
      <div className="flex items-center gap-3 border-b border-[hsl(var(--border))] px-4 py-2.5">
        <div className="skeleton" style={{ height: 16, width: 16, borderRadius: "var(--radius)" }} />
        <div className="skeleton" style={{ height: 16, width: 180, borderRadius: "var(--radius)" }} />
      </div>
      <div className="flex items-center gap-3 border-b border-[hsl(var(--border))] px-4 py-2">
        {Array.from({ length: 4 }).map((_, i) => (
          <div key={i} className="skeleton" style={{ height: 28, width: 80 + i * 20, borderRadius: "var(--radius)" }} />
        ))}
      </div>
      <div className="flex-1 flex flex-col gap-0">
        {Array.from({ length: 10 }).map((_, i) => (
          <div
            key={i}
            className="flex items-center gap-3 border-b border-[hsl(var(--border))] px-4"
            style={{ height: 32 }}
          >
            {Array.from({ length: 4 }).map((_, j) => (
              <div
                key={j}
                className="skeleton"
                style={{ height: 13, width: `${30 + ((i * 5 + j * 11) % 50)}%`, borderRadius: "var(--radius)" }}
              />
            ))}
          </div>
        ))}
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// RegisterPage
// ---------------------------------------------------------------------------

export function RegisterPage() {
  const { section = "informationRegisters", resource = "" } = useParams<{
    section: string
    resource: string
  }>()

  const { addTab } = useTabStore()

  const registerType = sectionToRegisterType(section)

  // Fetch register metadata
  const metaResult = useOne<RegisterMeta>({
    resource: `metadata/${section}`,
    id: resource,
  })

  const isLoading = metaResult.query?.isLoading ?? false
  const isError = metaResult.query?.isError ?? false
  const meta = metaResult.result

  // Register tab
  useEffect(() => {
    const title = meta?.name ?? resource
    const tabId = `${section}/${resource}`
    addTab({
      id: tabId,
      title,
      path: `/${section}/${resource}`,
      icon: "database",
      closable: true,
    })
  }, [section, resource, meta, addTab])

  // ---------------------------------------------------------------------------

  if (isLoading) return <RegisterPageSkeleton />

  if (isError || !meta) {
    return (
      <div className="flex flex-col items-center justify-center h-full gap-3 text-[hsl(var(--muted-foreground))]">
        <Warning size={32} weight="duotone" className="text-[hsl(var(--destructive))]" />
        <p className="text-[13px]">
          Failed to load register metadata for <strong>{resource}</strong>.
        </p>
      </div>
    )
  }

  const dimensions: RegisterDimension[] = meta.dimensions ?? []
  const resources: RegisterResource[] = meta.resources ?? []
  const extraColumns = meta.attributes ?? []

  return (
    <PrintTemplate title={meta.name} className="flex flex-col h-full">
      {/* Page header */}
      <div
        className="flex shrink-0 items-center gap-2 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.3)] px-4 py-2.5"
        data-no-print=""
      >
        <Database size={16} weight="duotone" className="text-[hsl(var(--primary))]" />
        <h1 className="text-[14px] font-semibold text-[hsl(var(--foreground))]">
          {meta.name}
        </h1>
        <span className="ml-2 text-[11px] text-[hsl(var(--muted-foreground))] bg-[hsl(var(--secondary))] px-2 py-0.5 rounded-[var(--radius)]">
          {registerType === "information"  ? "Information Register"  :
           registerType === "accumulation" ? "Accumulation Register" :
                                            "Accounting Register"}
        </span>
      </div>

      <RegisterView
        resource={`${section}/${resource}`}
        registerType={meta.registerType ?? registerType}
        dimensions={dimensions}
        resources={resources}
        extraColumns={extraColumns}
      />
    </PrintTemplate>
  )
}
