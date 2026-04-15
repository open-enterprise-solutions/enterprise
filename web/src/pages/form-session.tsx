/**
 * FormSessionPage — opens a server-side form session and renders
 * the live control tree from ibWebVisualHost.
 *
 * Route: /:section/:resource/:id/form  (or /form?metaType=...&metaName=...)
 *
 * This replaces OesSchemaForm for forms that have a designer layout
 * (binary blob from desktop configurator → ibWebVisualHost → JSON).
 */

import { useEffect, useCallback } from "react"
import { useParams, useNavigate } from "react-router-dom"
import { useFormSession } from "@/hooks/useFormSession"
import { FormRenderer } from "@/components/forms/FormRenderer"
import { FormSkeleton } from "@/components/forms/FormSkeleton"
import { Warning, X } from "@phosphor-icons/react"

// Map URL section names to metadata type names
const SECTION_TO_META: Record<string, string> = {
  catalogs: "catalog",
  documents: "document",
  dataProcessors: "dataProcessor",
  reports: "report",
  informationRegisters: "informationRegister",
  accumulationRegisters: "accumulationRegister",
  accountingRegisters: "accountingRegister",
  constants: "constant",
  enumerations: "enumeration",
}

export function FormSessionPage() {
  const { section = "", resource = "", id } = useParams<{
    section: string
    resource: string
    id: string
  }>()
  const navigate = useNavigate()

  const { layout, loading, error, openForm, sendEvent, closeForm } = useFormSession()

  const metaType = SECTION_TO_META[section] ?? section
  const metaName = resource

  useEffect(() => {
    if (metaName) {
      openForm(metaType, metaName, id)
    }
    return () => {
      closeForm()
    }
  }, [metaType, metaName, id]) // eslint-disable-line react-hooks/exhaustive-deps

  const handleClose = useCallback(() => {
    closeForm()
    navigate(-1)
  }, [closeForm, navigate])

  if (loading) return <FormSkeleton />

  if (error) {
    return (
      <div className="p-4">
        <div className="flex items-start gap-2 rounded-[var(--radius)] border border-[hsl(var(--destructive)/0.4)] bg-[hsl(var(--destructive)/0.06)] px-3 py-2 text-[13px] text-[hsl(var(--destructive))]">
          <Warning size={16} weight="duotone" className="mt-0.5 shrink-0" />
          <span>Form error: {error}</span>
          <button
            type="button"
            onClick={handleClose}
            className="ml-auto text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]"
          >
            <X size={14} />
          </button>
        </div>
      </div>
    )
  }

  if (!layout) {
    return (
      <div className="flex items-center justify-center h-full text-[13px] text-[hsl(var(--muted-foreground))]">
        No form layout available
      </div>
    )
  }

  return <FormRenderer layout={layout} onEvent={sendEvent} />
}
