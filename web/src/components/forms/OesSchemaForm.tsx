import { useEffect, useState, useCallback, useRef, useMemo } from "react"
import { createForm } from "@formily/core"
import { FormProvider, createSchemaField } from "@formily/react"
import { useMetadata, type MetaObjectRef } from "@/hooks/useMetadata"
import { oesFormilyComponents } from "./oes-formily-components"
import { CommandBar } from "./CommandBar"
import { FormSkeleton } from "./FormSkeleton"
import { FormHeader, StatusBadge } from "./oes-formily-components"
import { api } from "@/lib/axios"
import { Warning } from "@phosphor-icons/react"
import type { OesCommand } from "@/hooks/useFormSchema"

// Create the SchemaField renderer once, registering all OES components.
const OesSchemaField = createSchemaField({
  components: oesFormilyComponents,
})

// ---------------------------------------------------------------------------
// Schema builder — constructs Formily schema from metadata, no API call needed
// ---------------------------------------------------------------------------

interface MetaAttribute {
  name: string
  synonym?: string
  type?: { types?: string[] }
}

interface MetaSystemAttributes {
  code?: unknown
  description?: unknown
  number?: unknown
  date?: unknown
  [key: string]: unknown
}

function buildSchemaFromMetadata(
  metaItem: MetaObjectRef,
  metaType: string,
): Record<string, unknown> {
  const properties: Record<string, unknown> = {}
  const sys = metaItem.systemAttributes as MetaSystemAttributes | undefined
  const isDocument = metaType === "document"

  if (isDocument) {
    if (sys?.number !== undefined) {
      properties.number = {
        type: "string",
        title: "Number",
        "x-component": "Input",
        "x-decorator": "FormItem",
        "x-read-only": true,
      }
    }
    if (sys?.date !== undefined) {
      properties.date = {
        type: "string",
        title: "Date",
        "x-component": "DatePicker",
        "x-decorator": "FormItem",
      }
    }
  } else {
    if (sys?.code !== undefined) {
      properties.code = {
        type: "string",
        title: "Code",
        "x-component": "Input",
        "x-decorator": "FormItem",
      }
    }
    if (sys?.description !== undefined) {
      properties.description = {
        type: "string",
        title: "Description",
        "x-component": "Input",
        "x-decorator": "FormItem",
      }
    }
  }

  const attrs = (metaItem.attributes ?? []) as MetaAttribute[]
  for (const attr of attrs) {
    if (!attr.name) continue
    const rawType = attr.type?.types?.[0] ?? "string"
    const fieldKey = attr.name.toLowerCase()
    properties[fieldKey] = {
      type: rawType === "number" ? "number" : "string",
      title: attr.synonym ?? attr.name,
      "x-component":
        rawType === "boolean" ? "Checkbox" : rawType === "date" ? "DatePicker" : "Input",
      "x-decorator": "FormItem",
    }
  }

  return { type: "object", properties }
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface OesSchemaFormProps {
  /** Resource identifier, e.g. "catalog.products" or "document.sale" */
  resource: string
  /** UUID for edit mode. Undefined → create mode. */
  id?: string
  onSave?: (values: Record<string, unknown>) => void
  onCancel?: () => void
}

type DocStatus = "draft" | "posted" | "cancelled" | string

// ---------------------------------------------------------------------------
// Error banner
// ---------------------------------------------------------------------------

function ErrorBanner({ message }: { message: string }) {
  return (
    <div className="flex items-start gap-2 rounded-[var(--radius)] border border-[hsl(var(--destructive)/0.4)] bg-[hsl(var(--destructive)/0.06)] px-3 py-2 text-[13px] text-[hsl(var(--destructive))]">
      <Warning size={16} weight="duotone" className="mt-0.5 shrink-0" />
      <span>{message}</span>
    </div>
  )
}

// ---------------------------------------------------------------------------
// PropRow — used in the right-side properties panel
// ---------------------------------------------------------------------------

function PropRow({
  label,
  value,
  mono = false,
  valueClass,
}: {
  label: string
  value: string
  mono?: boolean
  valueClass?: string
}) {
  return (
    <div>
      <p className="text-[10px] text-[hsl(var(--muted-foreground))] mb-0.5">{label}</p>
      <p
        className={[
          "text-[11px] text-[hsl(var(--foreground))] break-all",
          mono ? "font-mono" : "",
          valueClass ?? "",
        ].join(" ")}
      >
        {value}
      </p>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Main component
// ---------------------------------------------------------------------------

export function OesSchemaForm({ resource, id, onSave, onCancel }: OesSchemaFormProps) {
  const isEditMode = Boolean(id)

  // Resolve metaType and object name from resource string (e.g. "catalog.products")
  const [metaType, metaName] = resource.split(".")

  const { tree, load } = useMetadata()
  useEffect(() => { load() }, [load])

  // Find the matching metadata item
  const metaItem = useMemo(() => {
    if (!tree || !metaName) return undefined
    const sectionKey = `${metaType}s` as keyof typeof tree
    const section = tree[sectionKey] as MetaObjectRef[] | undefined
    return section?.find((m) => m.name === metaName)
  }, [tree, metaType, metaName])

  // Build Formily schema from metadata — no API call needed
  const schema = useMemo(() => {
    if (!metaItem) return null
    return buildSchemaFromMetadata(metaItem, metaType)
  }, [metaItem, metaType])

  const schemaLoading = !tree
  const schemaError: string | null = null

  const [dataLoading, setDataLoading] = useState(isEditMode)
  const [dataError, setDataError] = useState<string | null>(null)
  const [docStatus, setDocStatus] = useState<DocStatus>("draft")

  const [isSaving, setIsSaving] = useState(false)
  const [isPosting, setIsPosting] = useState(false)
  const [formError, setFormError] = useState<string | null>(null)

  // Stable form instance — recreated when resource or id changes.
  const formRef = useRef(createForm({ validateFirst: true }))
  useEffect(() => {
    formRef.current = createForm({ validateFirst: true })
  }, [resource, id])

  // Load object data in edit mode
  useEffect(() => {
    if (!isEditMode || !id) {
      setDataLoading(false)
      return
    }
    setDataLoading(true)
    setDataError(null)
    api
      .get<Record<string, unknown>>(`/data/${resource}/${id}`)
      .then((res) => {
        const raw = res.data as Record<string, unknown>
        const data = (raw.data ?? raw) as Record<string, unknown>
        formRef.current.setValues(data)
        if (typeof data["status"] === "string") {
          setDocStatus(data["status"] as DocStatus)
        }
        setDataLoading(false)
      })
      .catch((err) => {
        setDataError(err instanceof Error ? err.message : "Failed to load record")
        setDataLoading(false)
      })
  }, [resource, id, isEditMode])

  const isDocument = metaType === "document"
  const commands: OesCommand[] = isDocument
    ? ["save", "post", "unpost", "delete", "copy", "close"]
    : ["save", "delete", "copy", "close"]

  // ---------------------------------------------------------------------------
  // Handlers
  // ---------------------------------------------------------------------------

  const handleSave = useCallback(async () => {
    if (isSaving) return
    setIsSaving(true)
    setFormError(null)
    try {
      const values = await formRef.current.submit<Record<string, unknown>>()
      if (isEditMode && id) {
        await api.put(`/data/${resource}/${id}`, values)
      } else {
        const res = await api.post<{ id: string }>(`/data/${resource}`, values)
        // Notify parent — pass back the new id so routing can update
        onSave?.({ ...values, id: res.data.id })
        return
      }
      onSave?.(values)
    } catch (err) {
      if (err instanceof Error) {
        setFormError(err.message)
      } else {
        setFormError("Validation failed. Check the highlighted fields.")
      }
    } finally {
      setIsSaving(false)
    }
  }, [isSaving, isEditMode, id, resource, onSave])

  const handlePost = useCallback(async () => {
    if (!id || isPosting) return
    setIsPosting(true)
    setFormError(null)
    try {
      // Save first to ensure server has latest values
      const values = await formRef.current.submit<Record<string, unknown>>()
      if (isEditMode) await api.put(`/data/${resource}/${id}`, values)
      await api.post(`/data/${resource}/${id}/post`)
      setDocStatus("posted")
    } catch (err) {
      setFormError(err instanceof Error ? err.message : "Posting failed")
    } finally {
      setIsPosting(false)
    }
  }, [id, isPosting, isEditMode, resource])

  const handleUnpost = useCallback(async () => {
    if (!id || isPosting) return
    setIsPosting(true)
    setFormError(null)
    try {
      await api.post(`/data/${resource}/${id}/unpost`)
      setDocStatus("draft")
    } catch (err) {
      setFormError(err instanceof Error ? err.message : "Unposting failed")
    } finally {
      setIsPosting(false)
    }
  }, [id, isPosting, resource])

  const handleDelete = useCallback(async () => {
    if (!id) return
    const confirmed = window.confirm("Delete this record? This action cannot be undone.")
    if (!confirmed) return
    setFormError(null)
    try {
      await api.delete(`/data/${resource}/${id}`)
      onCancel?.()
    } catch (err) {
      setFormError(err instanceof Error ? err.message : "Delete failed")
    }
  }, [id, resource, onCancel])

  const handleCopy = useCallback(async () => {
    if (!id) return
    try {
      const res = await api.post<{ id: string }>(`/data/${resource}/${id}/copy`)
      onSave?.({ _copied: true, id: res.data.id })
    } catch (err) {
      setFormError(err instanceof Error ? err.message : "Copy failed")
    }
  }, [id, resource, onSave])

  // ---------------------------------------------------------------------------
  // Render states
  // ---------------------------------------------------------------------------

  const isLoading = schemaLoading || dataLoading

  if (isLoading) {
    return <FormSkeleton />
  }

  if (schemaError) {
    return (
      <div className="p-4">
        <ErrorBanner message={`Schema error: ${schemaError}`} />
      </div>
    )
  }

  if (dataError) {
    return (
      <div className="p-4">
        <ErrorBanner message={`Data error: ${dataError}`} />
      </div>
    )
  }

  if (!schema && tree) {
    return (
      <div className="p-4">
        <ErrorBanner message={`Metadata not found for resource: ${resource}`} />
      </div>
    )
  }

  // ---------------------------------------------------------------------------
  // Main form render
  // ---------------------------------------------------------------------------

  const statusStripColor =
    docStatus === "posted"
      ? "hsl(var(--status-posted))"
      : docStatus === "cancelled"
      ? "hsl(var(--status-deleted))"
      : "hsl(var(--status-draft))"

  return (
    <div className="flex flex-col h-full bg-[hsl(var(--background))]">
      {/* Toolbar */}
      <CommandBar
        commands={commands}
        onSave={handleSave}
        onPost={isDocument ? handlePost : undefined}
        onUnpost={isDocument ? handleUnpost : undefined}
        onDelete={id ? handleDelete : undefined}
        onCopy={id ? handleCopy : undefined}
        onClose={onCancel}
        isSaving={isSaving}
        isPosting={isPosting}
        docStatus={docStatus}
      />

      {/* Form body — two-column layout */}
      <div className="flex flex-1 overflow-hidden">
        {/* Document status left strip */}
        {isDocument && (
          <div
            className="w-[3px] shrink-0 self-stretch"
            style={{ background: statusStripColor }}
          />
        )}

        {/* Main scrollable area */}
        <div className="flex flex-1 overflow-auto">
          {/* Form-level error */}
          {formError && (
            <div className="absolute z-10 top-2 left-1/2 -translate-x-1/2 w-[480px] max-w-[90vw]">
              <ErrorBanner message={formError} />
            </div>
          )}

          {/* Left: main form fields (70%) */}
          <div className="flex-1 min-w-0 p-4 pr-3">
            <FormHeader
              title={metaItem?.name}
              synonym={metaItem?.synonym}
              status={isDocument ? docStatus : undefined}
            />
            <div className="mt-3 border border-[hsl(var(--border))] bg-[hsl(var(--card))] p-4 shadow-card">
              <FormProvider form={formRef.current}>
                {/* eslint-disable-next-line @typescript-eslint/no-explicit-any */}
                <OesSchemaField schema={schema as any} />
              </FormProvider>
            </div>
          </div>

          {/* Right: system properties panel (30%) */}
          <aside className="w-[240px] shrink-0 border-l border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.2)] p-3 overflow-y-auto">
            <p className="text-[10px] font-semibold uppercase tracking-[0.08em] text-[hsl(var(--muted-foreground))] mb-3">
              Properties
            </p>
            <div className="space-y-3">
              {metaItem && (
                <>
                  <PropRow label="Object" value={metaItem.synonym ?? metaItem.name} />
                  <PropRow label="Type" value={metaType} />
                  {id && <PropRow label="ID" value={id} mono />}
                  {isDocument && (
                    <PropRow
                      label="Status"
                      value={docStatus}
                      valueClass={
                        docStatus === "posted"
                          ? "text-[hsl(142_60%_30%)]"
                          : docStatus === "cancelled"
                          ? "text-[hsl(0_72%_45%)]"
                          : "text-[hsl(215_14%_40%)]"
                      }
                    />
                  )}
                </>
              )}
            </div>
          </aside>
        </div>
      </div>
    </div>
  )
}

// Re-export for convenience
export { StatusBadge }
