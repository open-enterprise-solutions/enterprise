/**
 * ReportPage — renders a metadata-driven report for /:section/:resource/report
 *
 * Loads report metadata (parameters, columns) from /api/metadata/:type/:id
 * and delegates rendering to ReportViewer.
 * Registers a tab in the tab store.
 */

import { useEffect, useCallback } from "react"
import { useParams } from "react-router-dom"
import { ChartBar, Warning } from "@phosphor-icons/react"
import { useTabStore } from "@/stores/tab-store"
import { useMetadata } from "@/hooks/useMetadata"
import { api } from "@/lib/axios"
import { toApiResource } from "@/lib/resource-utils"
import { ReportViewer, type ReportParameter, type ReportColumn } from "@/components/reports/ReportViewer"
import { PrintTemplate } from "@/components/reports/PrintTemplate"

// ---------------------------------------------------------------------------
// Types — expected shape of /api/metadata/reports/:id
// ---------------------------------------------------------------------------

interface ReportMeta {
  id: string
  name: string
  parameters?: ReportParameter[]
  columns?: ReportColumn[]
}

// ---------------------------------------------------------------------------
// Skeleton
// ---------------------------------------------------------------------------

function ReportPageSkeleton() {
  return (
    <div className="flex flex-col h-full">
      {/* Toolbar skeleton */}
      <div className="flex items-center gap-3 border-b border-[hsl(var(--border))] px-4 py-3">
        {Array.from({ length: 3 }).map((_, i) => (
          <div
            key={i}
            className="skeleton"
            style={{ height: 28, width: 90 + i * 30, borderRadius: "var(--radius)" }}
          />
        ))}
      </div>
      {/* Body */}
      <div className="flex flex-col items-center justify-center flex-1 gap-4">
        <div className="skeleton" style={{ height: 32, width: 200, borderRadius: "var(--radius)" }} />
        <div className="skeleton" style={{ height: 14, width: 300, borderRadius: "var(--radius)" }} />
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// ReportPage
// ---------------------------------------------------------------------------

export function ReportPage() {
  const { section = "reports", resource = "" } = useParams<{
    section: string
    resource: string
  }>()

  const { addTab } = useTabStore()

  // Resolve report metadata from the already-loaded metadata tree
  const { tree, loading: metaLoading, error: metaError, load } = useMetadata()
  useEffect(() => { load() }, [load])

  const isLoading = metaLoading
  const isError = !!metaError

  const metaItem = tree?.reports?.find(
    (r) => r.name === resource || r.id === resource,
  )

  // Shape it into the ReportMeta interface
  const meta: ReportMeta | undefined = metaItem
    ? {
        id: String(metaItem.id),
        name: metaItem.synonym ?? metaItem.name,
        parameters: (metaItem as unknown as { parameters?: ReportParameter[] }).parameters,
        columns: (metaItem as unknown as { columns?: ReportColumn[] }).columns,
      }
    : undefined

  // Register tab
  useEffect(() => {
    const title = meta?.name ?? resource
    const tabId = `${section}/${resource}/report`
    addTab({
      id: tabId,
      title,
      path: `/${section}/${resource}/report`,
      icon: "chart-bar",
      closable: true,
    })
  }, [section, resource, meta, addTab])

  // Report generate handler — calls /api/data/:resource with params as filters
  const handleGenerate = useCallback(
    async (params: Record<string, unknown>) => {
      const qs = new URLSearchParams()
      for (const [key, val] of Object.entries(params)) {
        if (val != null && val !== "") {
          qs.set(key, String(val))
        }
      }
      const response = await api.get<{ data: Record<string, unknown>[] }>(
        `/data/${toApiResource(section, resource)}?${qs.toString()}`,
      )
      return response.data.data ?? []
    },
    [section, resource],
  )

  // ---------------------------------------------------------------------------

  if (isLoading) return <ReportPageSkeleton />

  if (isError || !meta) {
    return (
      <div className="flex flex-col items-center justify-center h-full gap-3 text-[hsl(var(--muted-foreground))]">
        <Warning size={32} weight="duotone" className="text-[hsl(var(--destructive))]" />
        <p className="text-[13px]">Failed to load report metadata for <strong>{resource}</strong>.</p>
      </div>
    )
  }

  // Derive columns from metadata or fall back to basic set
  const columns: ReportColumn[] = meta.columns?.length
    ? meta.columns
    : [
        { key: "name",   title: "Name",   type: "string" },
        { key: "amount", title: "Amount", type: "number" },
        { key: "date",   title: "Date",   type: "date"   },
      ]

  const parameters: ReportParameter[] = meta.parameters ?? []

  return (
    <PrintTemplate title={meta.name} className="flex flex-col h-full">
      {/* Page header */}
      <div className="flex shrink-0 items-center gap-2 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.3)] px-4 py-2.5" data-no-print="">
        <ChartBar size={16} weight="duotone" className="text-[hsl(var(--primary))]" />
        <h1 className="text-[14px] font-semibold text-[hsl(var(--foreground))]">
          {meta.name}
        </h1>
      </div>

      <ReportViewer
        title={meta.name}
        parameters={parameters}
        columns={columns}
        onGenerate={handleGenerate}
      />
    </PrintTemplate>
  )
}
