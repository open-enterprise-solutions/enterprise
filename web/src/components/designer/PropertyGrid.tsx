import { useState } from "react"
import { TypeDescriptionEditor, emptyTypeDescription, type TypeDescription, type RefType } from "./TypeDescriptionEditor"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export type PropValueType = "text" | "number" | "boolean" | "select" | "type-description"

export interface PropField {
  key: string
  label: string
  type: PropValueType
  options?: string[]
  readOnly?: boolean
}

export interface PropGroup {
  label: string
  fields: PropField[]
}

export type PropValues = Record<string, string | number | boolean | TypeDescription>

interface PropertyGridProps {
  groups: PropGroup[]
  values: PropValues
  onChange: (key: string, value: string | number | boolean | TypeDescription) => void
  availableRefs?: RefType[]
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export function PropertyGrid({ groups, values, onChange, availableRefs = [] }: PropertyGridProps) {
  return (
    <div className="flex flex-col gap-0 overflow-y-auto h-full">
      {groups.map((group) => (
        <PropertyGroup
          key={group.label}
          group={group}
          values={values}
          onChange={onChange}
          availableRefs={availableRefs}
        />
      ))}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Group (collapsible)
// ---------------------------------------------------------------------------

function PropertyGroup({
  group,
  values,
  onChange,
  availableRefs,
}: {
  group: PropGroup
  values: PropValues
  onChange: (key: string, value: string | number | boolean | TypeDescription) => void
  availableRefs: RefType[]
}) {
  const [expanded, setExpanded] = useState(true)

  return (
    <div className="border-b border-[hsl(var(--border))]">
      <button
        type="button"
        onClick={() => setExpanded((v) => !v)}
        className="flex w-full items-center gap-2 px-3 py-1.5 text-[10px] font-semibold uppercase tracking-wide text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] transition-colors"
      >
        <span className={expanded ? "rotate-90" : ""} style={{ transition: "transform 0.1s" }}>
          ▶
        </span>
        {group.label}
      </button>

      {expanded && (
        <div>
          {group.fields.map((field) => (
            <PropertyRow
              key={field.key}
              field={field}
              value={values[field.key]}
              onChange={(v) => onChange(field.key, v)}
              availableRefs={availableRefs}
            />
          ))}
        </div>
      )}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Individual row
// ---------------------------------------------------------------------------

function PropertyRow({
  field,
  value,
  onChange,
  availableRefs,
}: {
  field: PropField
  value: string | number | boolean | TypeDescription | undefined
  onChange: (v: string | number | boolean | TypeDescription) => void
  availableRefs: RefType[]
}) {
  const [typeEditorOpen, setTypeEditorOpen] = useState(false)

  const baseCell = "px-3 py-1.5 text-[11px]"

  return (
    <>
      <div
        className={[
          "grid border-b border-[hsl(var(--border)/0.5)]",
          field.type === "type-description" ? "grid-cols-1" : "grid-cols-2",
        ].join(" ")}
      >
        {/* Label */}
        <div className={`${baseCell} text-[hsl(var(--muted-foreground))] border-r border-[hsl(var(--border)/0.5)] flex items-center`}>
          {field.label}
        </div>

        {/* Value cell */}
        {field.type !== "type-description" && (
          <div className={`${baseCell} flex items-center`}>
            {field.type === "text" && (
              <input
                type="text"
                value={String(value ?? "")}
                readOnly={field.readOnly}
                onChange={(e) => onChange(e.target.value)}
                className="w-full bg-transparent outline-none focus:bg-[hsl(var(--secondary))] rounded-[2px] px-1 -mx-1 read-only:opacity-60"
              />
            )}
            {field.type === "number" && (
              <input
                type="number"
                value={Number(value ?? 0)}
                readOnly={field.readOnly}
                onChange={(e) => onChange(Number(e.target.value))}
                className="w-full bg-transparent outline-none focus:bg-[hsl(var(--secondary))] rounded-[2px] px-1 -mx-1 tabular-nums read-only:opacity-60"
              />
            )}
            {field.type === "boolean" && (
              <input
                type="checkbox"
                checked={Boolean(value)}
                disabled={field.readOnly}
                onChange={(e) => onChange(e.target.checked)}
                className="accent-[hsl(var(--primary))] h-3.5 w-3.5"
              />
            )}
            {field.type === "select" && (
              <select
                value={String(value ?? "")}
                disabled={field.readOnly}
                onChange={(e) => onChange(e.target.value)}
                className="w-full bg-transparent outline-none focus:bg-[hsl(var(--secondary))] rounded-[2px] px-1 -mx-1 disabled:opacity-60"
              >
                {(field.options ?? []).map((opt) => (
                  <option key={opt} value={opt}>{opt}</option>
                ))}
              </select>
            )}
          </div>
        )}

        {/* Type description — full-width toggle button */}
        {field.type === "type-description" && (
          <div className={`${baseCell} flex items-center justify-between`}>
            <span className="text-[hsl(var(--muted-foreground))]">{field.label}</span>
            <button
              type="button"
              onClick={() => setTypeEditorOpen((v) => !v)}
              className="text-[10px] text-[hsl(var(--primary))] hover:underline"
            >
              {typeEditorOpen ? "Close" : "Edit type..."}
            </button>
          </div>
        )}
      </div>

      {/* Inline type description editor */}
      {field.type === "type-description" && typeEditorOpen && (
        <div className="border-b border-[hsl(var(--border)/0.5)] px-3 py-2 bg-[hsl(var(--secondary)/0.4)]">
          <TypeDescriptionEditor
            value={(value as TypeDescription) ?? emptyTypeDescription()}
            onChange={(v) => onChange(v)}
            availableRefs={availableRefs}
          />
        </div>
      )}
    </>
  )
}
