import { useEffect, useState, useCallback, useRef } from "react"
import { createForm } from "@formily/core"
import { FormProvider, createSchemaField } from "@formily/react"
import { useFormSchema } from "@/hooks/useFormSchema"
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
// Main component
// ---------------------------------------------------------------------------

export function OesSchemaForm({ resource, id, onSave, onCancel }: OesSchemaFormProps) {
  const isEditMode = Boolean(id)

  const { schema, isLoading: schemaLoading, error: schemaError } = useFormSchema(resource)

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
        const data = res.data
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

  // Derive metadata from schema extension fields
  const meta = schema?.["x-oes-meta"]
  const commands: OesCommand[] = schema?.["x-oes-commands"] ?? ["save", "close"]
  const isDocument = meta?.metaType === "document"

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

  if (!schema) {
    return (
      <div className="p-4">
        <ErrorBanner message="No schema returned for this resource." />
      </div>
    )
  }

  // ---------------------------------------------------------------------------
  // Main form render
  // ---------------------------------------------------------------------------

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

      {/* Form body */}
      <div className="flex-1 overflow-auto p-4">
        {/* Form-level error */}
        {formError && (
          <div className="mb-3">
            <ErrorBanner message={formError} />
          </div>
        )}

        {/* Object header */}
        <FormHeader
          title={meta?.name}
          synonym={meta?.synonym}
          status={isDocument ? docStatus : undefined}
        />

        {/* Formily form */}
        <FormProvider form={formRef.current}>
          {/* eslint-disable-next-line @typescript-eslint/no-explicit-any */}
          <OesSchemaField schema={schema as any} />
        </FormProvider>
      </div>

      {/* Status strip on the left edge (documents only) */}
      {isDocument && (
        <div
          className={`fixed left-0 top-0 h-full w-[3px] pointer-events-none ${
            docStatus === "posted"
              ? "bg-[hsl(var(--status-posted))]"
              : docStatus === "cancelled"
              ? "bg-[hsl(var(--status-deleted))]"
              : "bg-[hsl(var(--status-draft))]"
          }`}
        />
      )}
    </div>
  )
}

// Re-export for convenience
export { StatusBadge }
