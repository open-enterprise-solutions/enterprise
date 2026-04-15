/**
 * FormSessionPage — opens a server-side form session and renders
 * the live control tree from ibWebVisualHost.
 *
 * Route: /:section/:resource/:id/form  (or /form?metaType=...&metaName=...)
 *
 * This replaces OesSchemaForm for forms that have a designer layout
 * (binary blob from desktop configurator → ibWebVisualHost → JSON).
 *
 * After receiving the layout from open-form, we enrich each textbox node
 * with the proper control type (datepicker, numberinput, reffield, checkbox)
 * by cross-referencing the metadata tree attribute type information.
 */

import { useEffect, useCallback, useMemo } from "react"
import { useParams, useNavigate } from "react-router-dom"
import { useFormSession } from "@/hooks/useFormSession"
import { useMetadata, type MetaObjectRef } from "@/hooks/useMetadata"
import { FormRenderer } from "@/components/forms/FormRenderer"
import { FormSkeleton } from "@/components/forms/FormSkeleton"
import { Warning, X } from "@phosphor-icons/react"
import type { ControlNode } from "@/hooks/useFormSession"

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

// Map URL section names to metadata tree keys
const SECTION_TO_TREE_KEY: Record<string, keyof import("@/hooks/useMetadata").MetadataTree> = {
  catalogs: "catalogs",
  documents: "documents",
  dataProcessors: "dataProcessors",
  reports: "reports",
  informationRegisters: "informationRegisters",
  accumulationRegisters: "accumulationRegisters",
  accountingRegisters: "accountingRegisters",
  constants: "constants",
  enumerations: "enumerations",
}

// ---------------------------------------------------------------------------
// Attribute type helpers
// ---------------------------------------------------------------------------

interface MetaAttribute {
  name: string
  synonym?: string
  type?: { types?: string[] }
  precision?: number
  scale?: number
}

interface MetaSystemAttributes {
  code?: MetaAttribute
  description?: MetaAttribute
  number?: MetaAttribute
  date?: MetaAttribute
  [key: string]: MetaAttribute | undefined
}

/**
 * Build a name→attribute map from a metadata object's attributes and
 * systemAttributes. Keys are lowercase for case-insensitive matching.
 */
function buildAttrMap(metaItem: MetaObjectRef): Map<string, MetaAttribute> {
  const map = new Map<string, MetaAttribute>()

  // System attributes (code, description, number, date, ...)
  const sys = metaItem.systemAttributes as MetaSystemAttributes | undefined
  if (sys) {
    for (const [key, val] of Object.entries(sys)) {
      if (val && typeof val === "object" && "name" in val) {
        map.set((val as MetaAttribute).name?.toLowerCase() ?? key.toLowerCase(), val as MetaAttribute)
      } else if (val !== undefined) {
        // Some backends serialize systemAttributes as plain objects with the
        // key being the attribute name directly
        map.set(key.toLowerCase(), { name: key, ...(typeof val === "object" ? (val as object) : {}) } as MetaAttribute)
      }
    }
  }

  // User-defined attributes
  const attrs = (metaItem.attributes ?? []) as MetaAttribute[]
  for (const attr of attrs) {
    if (attr.name) map.set(attr.name.toLowerCase(), attr)
  }

  return map
}

/**
 * Given an attribute's types array, return the enriched control type
 * and extra props to merge into the node.
 */
function resolveControlType(
  attr: MetaAttribute,
): { type: string; extraProps: Record<string, string> } {
  const types = attr.type?.types ?? []

  if (types.includes("date")) {
    return { type: "datepicker", extraProps: {} }
  }
  if (types.includes("boolean")) {
    return { type: "checkbox", extraProps: {} }
  }
  if (types.includes("number")) {
    const extraProps: Record<string, string> = {}
    if (attr.precision !== undefined) extraProps.precision = String(attr.precision)
    if (attr.scale !== undefined) extraProps.scale = String(attr.scale)
    return { type: "numberinput", extraProps }
  }
  if (types.includes("reference") || types.some((t) => t.startsWith("catalog") || t.startsWith("document"))) {
    // refType is the first reference type found
    const refType = types.find((t) => t !== "undefined") ?? ""
    return { type: "reffield", extraProps: { refType } }
  }

  // string / unknown — keep as textbox
  return { type: "textbox", extraProps: {} }
}

/**
 * Walk the control tree and rewrite textbox nodes whose name matches a
 * known attribute with a non-string type.
 */
function enrichLayout(node: ControlNode, attrMap: Map<string, MetaAttribute>): ControlNode {
  const enrichedChildren = node.children?.map((child) => enrichLayout(child, attrMap))

  // Only rewrite textbox controls that have a name we can look up
  if (node.type === "textbox" && node.name) {
    const attr = attrMap.get(node.name.toLowerCase())
    if (attr) {
      const { type, extraProps } = resolveControlType(attr)
      if (type !== "textbox") {
        return {
          ...node,
          type,
          props: { ...node.props, ...extraProps },
          children: enrichedChildren,
        }
      }
    }
  }

  if (enrichedChildren === node.children) return node
  return { ...node, children: enrichedChildren }
}

// ---------------------------------------------------------------------------
// Page component
// ---------------------------------------------------------------------------

export function FormSessionPage() {
  const { section = "", resource = "", id } = useParams<{
    section: string
    resource: string
    id: string
  }>()
  const navigate = useNavigate()

  const { layout, loading, error, openForm, sendEvent, closeForm } = useFormSession()
  const { tree, load: loadMetadata } = useMetadata()

  const metaType = SECTION_TO_META[section] ?? section
  const metaName = resource

  // Ensure metadata is loaded so we can enrich the layout
  useEffect(() => {
    loadMetadata()
  }, [loadMetadata])

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

  // Build attribute map from the matching metadata object
  const attrMap = useMemo<Map<string, MetaAttribute>>(() => {
    if (!tree) return new Map()
    const treeKey = SECTION_TO_TREE_KEY[section]
    if (!treeKey) return new Map()
    const collection = (tree[treeKey] ?? []) as MetaObjectRef[]
    const metaItem = collection.find(
      (obj) => obj.name.toLowerCase() === resource.toLowerCase(),
    )
    if (!metaItem) return new Map()
    return buildAttrMap(metaItem)
  }, [tree, section, resource])

  // Enrich layout with proper control types from metadata
  const enrichedLayout = useMemo<ControlNode | null>(() => {
    if (!layout) return null
    if (attrMap.size === 0) return layout
    return enrichLayout(layout, attrMap)
  }, [layout, attrMap])

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

  if (!enrichedLayout) {
    return (
      <div className="flex items-center justify-center h-full text-[13px] text-[hsl(var(--muted-foreground))]">
        No form layout available
      </div>
    )
  }

  return <FormRenderer layout={enrichedLayout} onEvent={sendEvent} />
}
