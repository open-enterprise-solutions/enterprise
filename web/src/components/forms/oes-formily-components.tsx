/**
 * OES Formily component registry.
 *
 * Every key here maps to an x-component value in the backend JSON Schema.
 * Import this map and pass it to SchemaField as the `components` prop.
 */

import { connect, mapProps, mapReadPretty } from "@formily/react"
import type { Field, ArrayField as ArrayFieldModel, IFormFeedback } from "@formily/core"
import { useField, useFieldSchema, RecursionField } from "@formily/react"
import type { ReactNode } from "react"
import { useState, useRef, useEffect, useCallback } from "react"
import {
  MagnifyingGlass,
  DotsThree,
  Plus,
  Trash,
  Warning,
} from "@phosphor-icons/react"
import { api } from "@/lib/axios"

// ---------------------------------------------------------------------------
// Primitive helpers
// ---------------------------------------------------------------------------

function cn(...classes: (string | undefined | false | null)[]): string {
  return classes.filter(Boolean).join(" ")
}

const baseInput =
  "h-8 w-full rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-2 text-[13px] text-[hsl(var(--foreground))] outline-none transition-colors placeholder:text-[hsl(var(--muted-foreground))] focus:border-[hsl(var(--ring))] focus:ring-1 focus:ring-[hsl(var(--ring))] disabled:cursor-not-allowed disabled:opacity-50"

// ---------------------------------------------------------------------------
// FormItem — label + validation message wrapper
// ---------------------------------------------------------------------------

interface FormItemProps {
  label?: ReactNode
  required?: boolean
  feedbackText?: ReactNode
  feedbackStatus?: "error" | "warning" | "success" | ""
  children?: ReactNode
  extra?: ReactNode
  colon?: boolean
  labelWidth?: number | string
}

export function FormItem({
  label,
  required,
  feedbackText,
  feedbackStatus,
  children,
  extra,
  colon = false,
  labelWidth = 140,
}: FormItemProps) {
  const hasError = feedbackStatus === "error"
  const hasWarning = feedbackStatus === "warning"

  return (
    <div className="flex flex-col gap-0.5 mb-3">
      {label && (
        <label
          className="flex items-center gap-1 text-[12px] font-medium text-[hsl(var(--muted-foreground))] leading-none mb-1"
          style={{ minWidth: labelWidth }}
        >
          {required && (
            <span className="text-[hsl(var(--destructive))] mr-0.5">*</span>
          )}
          {label}
          {colon && ":"}
        </label>
      )}
      <div className="relative">{children}</div>
      {extra && (
        <p className="text-[11px] text-[hsl(var(--muted-foreground))]">{extra}</p>
      )}
      {feedbackText && (
        <p
          className={cn(
            "flex items-center gap-1 text-[11px] leading-none",
            hasError && "text-[hsl(var(--destructive))]",
            hasWarning && "text-[hsl(var(--chart-3))]",
          )}
        >
          {(hasError || hasWarning) && <Warning size={11} weight="fill" />}
          {feedbackText}
        </p>
      )}
    </div>
  )
}

// Formily-connected FormItem — reads field metadata automatically.
export const FormilyFormItem = connect(
  FormItem,
  mapProps(
    { title: "label", required: "required" },
    (props, field) => {
      const f = field as Field | undefined
      const errors: IFormFeedback[] = f?.errors ?? []
      const warnings: IFormFeedback[] = f?.warnings ?? []
      const toMessages = (items: IFormFeedback[]) =>
        items.flatMap((fb) => fb.messages ?? []).join(", ")
      const feedbackStatus: FormItemProps["feedbackStatus"] =
        errors.length > 0 ? "error" : warnings.length > 0 ? "warning" : ""
      return {
        ...props,
        feedbackStatus,
        feedbackText:
          errors.length > 0
            ? toMessages(errors)
            : warnings.length > 0
            ? toMessages(warnings)
            : undefined,
        extra: (f?.description as string) || undefined,
      }
    },
  ),
)

// ---------------------------------------------------------------------------
// Input — plain text field
// ---------------------------------------------------------------------------

interface InputProps {
  value?: string
  onChange?: (v: string) => void
  onBlur?: () => void
  placeholder?: string
  maxLength?: number
  disabled?: boolean
  readOnly?: boolean
}

function InputBase({ value, onChange, onBlur, placeholder, maxLength, disabled, readOnly }: InputProps) {
  return (
    <input
      type="text"
      className={baseInput}
      value={value ?? ""}
      maxLength={maxLength}
      disabled={disabled}
      readOnly={readOnly}
      placeholder={placeholder}
      onChange={(e) => onChange?.(e.target.value)}
      onBlur={onBlur}
    />
  )
}

export const Input = connect(
  InputBase,
  mapProps({ value: "value", onInput: "onChange" }),
  mapReadPretty((props: InputProps) => (
    <span className="text-[13px] text-[hsl(var(--foreground))]">{props.value ?? "—"}</span>
  )),
)

// ---------------------------------------------------------------------------
// NumberInput — numeric field with precision / scale
// ---------------------------------------------------------------------------

interface NumberInputProps {
  value?: number | string
  onChange?: (v: number | null) => void
  onBlur?: () => void
  precision?: number
  scale?: number
  min?: number
  max?: number
  disabled?: boolean
  placeholder?: string
}

function NumberInputBase({ value, onChange, onBlur, scale = 2, min, max, disabled, placeholder }: NumberInputProps) {
  const [text, setText] = useState<string>(value != null ? String(value) : "")

  useEffect(() => {
    setText(value != null ? String(value) : "")
  }, [value])

  const commit = () => {
    const num = parseFloat(text)
    if (isNaN(num)) {
      onChange?.(null)
      setText("")
    } else {
      const clamped = min != null && num < min ? min : max != null && num > max ? max : num
      const rounded = parseFloat(clamped.toFixed(scale))
      onChange?.(rounded)
      setText(String(rounded))
    }
    onBlur?.()
  }

  return (
    <input
      type="text"
      inputMode="decimal"
      className={cn(baseInput, "text-right tabular-nums")}
      value={text}
      disabled={disabled}
      placeholder={placeholder ?? (scale > 0 ? `0.${"0".repeat(scale)}` : "0")}
      onChange={(e) => setText(e.target.value)}
      onBlur={commit}
    />
  )
}

export const NumberInput = connect(
  NumberInputBase,
  mapProps({ value: "value" }),
  mapReadPretty((props: NumberInputProps) => (
    <span className="text-[13px] tabular-nums text-right">{props.value != null ? String(props.value) : "—"}</span>
  )),
)

// ---------------------------------------------------------------------------
// DatePicker — date or datetime text input (native input[type=date])
// ---------------------------------------------------------------------------

interface DatePickerProps {
  value?: string
  onChange?: (v: string) => void
  onBlur?: () => void
  showTime?: boolean
  disabled?: boolean
}

function DatePickerBase({ value, onChange, onBlur, showTime, disabled }: DatePickerProps) {
  return (
    <input
      type={showTime ? "datetime-local" : "date"}
      className={baseInput}
      value={value ?? ""}
      disabled={disabled}
      onChange={(e) => onChange?.(e.target.value)}
      onBlur={onBlur}
    />
  )
}

export const DatePicker = connect(
  DatePickerBase,
  mapProps({ value: "value", onInput: "onChange" }),
  mapReadPretty((props: DatePickerProps) => (
    <span className="text-[13px] tabular-nums">{props.value ?? "—"}</span>
  )),
)

// ---------------------------------------------------------------------------
// Checkbox
// ---------------------------------------------------------------------------

interface CheckboxProps {
  value?: boolean
  onChange?: (v: boolean) => void
  disabled?: boolean
  children?: ReactNode
}

function CheckboxBase({ value, onChange, disabled, children }: CheckboxProps) {
  return (
    <label className="flex items-center gap-2 cursor-pointer select-none">
      <input
        type="checkbox"
        className="h-4 w-4 rounded-[var(--radius)] border border-[hsl(var(--input))] accent-[hsl(var(--primary))]"
        checked={value ?? false}
        disabled={disabled}
        onChange={(e) => onChange?.(e.target.checked)}
      />
      {children && <span className="text-[13px]">{children}</span>}
    </label>
  )
}

export const Checkbox = connect(
  CheckboxBase,
  mapProps({ value: "value" }),
)

// ---------------------------------------------------------------------------
// Select — enum dropdown
// ---------------------------------------------------------------------------

interface SelectProps {
  value?: unknown
  onChange?: (v: unknown) => void
  onBlur?: () => void
  disabled?: boolean
  placeholder?: string
  dataSource?: Array<{ label: string; value: unknown }>
}

function SelectBase({ value, onChange, onBlur, disabled, placeholder, dataSource = [] }: SelectProps) {
  return (
    <select
      className={cn(baseInput, "cursor-pointer")}
      value={value != null ? String(value) : ""}
      disabled={disabled}
      onChange={(e) => {
        const opt = dataSource.find((o) => String(o.value) === e.target.value)
        onChange?.(opt ? opt.value : e.target.value)
      }}
      onBlur={onBlur}
    >
      {placeholder && (
        <option value="" disabled>
          {placeholder}
        </option>
      )}
      {dataSource.map((opt) => (
        <option key={String(opt.value)} value={String(opt.value)}>
          {opt.label}
        </option>
      ))}
    </select>
  )
}

export const Select = connect(
  SelectBase,
  mapProps({ value: "value", dataSource: "dataSource" }),
  mapReadPretty((props: SelectProps) => {
    const opt = props.dataSource?.find((o) => String(o.value) === String(props.value))
    return <span className="text-[13px]">{opt?.label ?? String(props.value ?? "—")}</span>
  }),
)

// ---------------------------------------------------------------------------
// RefField — reference to another OES object (typeahead + modal picker)
// ---------------------------------------------------------------------------

export interface RefFieldValue {
  id: string
  presentation?: string
}

interface RefFieldProps {
  value?: RefFieldValue | null
  onChange?: (v: RefFieldValue | null) => void
  onBlur?: () => void
  refResource?: string
  disabled?: boolean
  placeholder?: string
}

function RefFieldBase({ value, onChange, onBlur, refResource, disabled, placeholder }: RefFieldProps) {
  const [query, setQuery] = useState<string>(value?.presentation ?? "")
  const [suggestions, setSuggestions] = useState<RefFieldValue[]>([])
  const [open, setOpen] = useState(false)
  const [loading, setLoading] = useState(false)
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const wrapperRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    setQuery(value?.presentation ?? "")
  }, [value])

  // Close dropdown on outside click
  useEffect(() => {
    const handler = (e: MouseEvent) => {
      if (wrapperRef.current && !wrapperRef.current.contains(e.target as Node)) {
        setOpen(false)
      }
    }
    document.addEventListener("mousedown", handler)
    return () => document.removeEventListener("mousedown", handler)
  }, [])

  const search = useCallback(
    (q: string) => {
      if (!refResource || q.trim().length < 1) {
        setSuggestions([])
        setOpen(false)
        return
      }
      if (debounceRef.current) clearTimeout(debounceRef.current)
      debounceRef.current = setTimeout(async () => {
        setLoading(true)
        try {
          const res = await api.get<{ data: RefFieldValue[] }>(`/data/${refResource}`, {
            params: { filter: q, pageSize: 10 },
          })
          setSuggestions(res.data.data ?? [])
          setOpen(true)
        } catch {
          setSuggestions([])
        } finally {
          setLoading(false)
        }
      }, 250)
    },
    [refResource],
  )

  const select = (item: RefFieldValue) => {
    onChange?.(item)
    setQuery(item.presentation ?? item.id)
    setOpen(false)
    setSuggestions([])
  }

  const clear = () => {
    onChange?.(null)
    setQuery("")
    setSuggestions([])
    setOpen(false)
  }

  return (
    <div ref={wrapperRef} className="relative">
      <div className="flex items-center gap-1">
        <div className="relative flex-1">
          <input
            type="text"
            className={cn(baseInput, "pr-7")}
            value={query}
            disabled={disabled}
            placeholder={placeholder ?? "Select..."}
            onChange={(e) => {
              setQuery(e.target.value)
              search(e.target.value)
            }}
            onBlur={() => {
              // If typed text doesn't match current value, clear the ref
              if (query === "" && value) clear()
              onBlur?.()
            }}
            onKeyDown={(e) => {
              if (e.key === "Escape") setOpen(false)
            }}
          />
          {loading && (
            <span className="absolute right-2 top-1/2 -translate-y-1/2">
              <span className="block h-3 w-3 animate-spin rounded-full border-2 border-[hsl(var(--muted-foreground))] border-t-transparent" />
            </span>
          )}
          {!loading && query && (
            <MagnifyingGlass
              size={14}
              className="absolute right-2 top-1/2 -translate-y-1/2 text-[hsl(var(--muted-foreground))]"
            />
          )}
        </div>
        {/* Modal picker trigger */}
        {refResource && (
          <button
            type="button"
            disabled={disabled}
            className="flex h-8 w-8 items-center justify-center rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--secondary))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--muted))] transition-colors disabled:opacity-50"
            onClick={() => {
              // Placeholder — modal picker to be implemented in Phase 4
              search("")
            }}
            title="Choose from list"
          >
            <DotsThree size={16} weight="bold" />
          </button>
        )}
      </div>

      {/* Suggestions dropdown */}
      {open && suggestions.length > 0 && (
        <ul className="absolute z-50 mt-0.5 w-full rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--popover))] shadow-md py-0.5 max-h-48 overflow-auto">
          {suggestions.map((item) => (
            <li
              key={item.id}
              className="flex items-center px-2 py-1.5 text-[13px] cursor-pointer hover:bg-[hsl(var(--secondary))] transition-colors"
              onMouseDown={(e) => {
                e.preventDefault()
                select(item)
              }}
            >
              {item.presentation ?? item.id}
            </li>
          ))}
        </ul>
      )}
    </div>
  )
}

export const RefField = connect(
  RefFieldBase,
  mapProps({ value: "value" }),
  mapReadPretty((props: RefFieldProps) => (
    <span className="text-[13px] text-[hsl(var(--primary))] underline-offset-2 hover:underline cursor-pointer">
      {props.value?.presentation ?? props.value?.id ?? "—"}
    </span>
  )),
)

// ---------------------------------------------------------------------------
// FormHeader — title bar inside the form
// ---------------------------------------------------------------------------

interface FormHeaderProps {
  title?: string
  synonym?: string
  status?: "draft" | "posted" | "cancelled" | string
}

const statusLabel: Record<string, string> = {
  draft: "Draft",
  posted: "Posted",
  cancelled: "Cancelled",
}

export function FormHeader({ title, synonym, status }: FormHeaderProps) {
  return (
    <div className="flex items-center gap-3 mb-4 pb-3 border-b border-[hsl(var(--border))]">
      <div className="flex-1 min-w-0">
        <h1 className="text-[15px] font-semibold text-[hsl(var(--foreground))] truncate">
          {synonym ?? title ?? "Form"}
        </h1>
      </div>
      {status && <StatusBadge status={status} />}
    </div>
  )
}

// ---------------------------------------------------------------------------
// StatusBadge — document status pill
// ---------------------------------------------------------------------------

interface StatusBadgeProps {
  status: string
}

const statusStyles: Record<string, string> = {
  posted:
    "bg-[hsl(var(--status-posted)/0.12)] text-[hsl(var(--status-posted))] border-[hsl(var(--status-posted)/0.3)]",
  draft:
    "bg-[hsl(var(--status-draft)/0.12)] text-[hsl(var(--status-draft))] border-[hsl(var(--status-draft)/0.3)]",
  cancelled:
    "bg-[hsl(var(--status-deleted)/0.12)] text-[hsl(var(--status-deleted))] border-[hsl(var(--status-deleted)/0.3)]",
  progress:
    "bg-[hsl(var(--status-progress)/0.12)] text-[hsl(var(--status-progress))] border-[hsl(var(--status-progress)/0.3)]",
}

export function StatusBadge({ status }: StatusBadgeProps) {
  const style = statusStyles[status] ?? statusStyles.draft
  return (
    <span
      className={cn(
        "inline-flex items-center rounded-[var(--radius)] border px-2 py-0.5 text-[11px] font-medium",
        style,
      )}
    >
      {statusLabel[status] ?? status}
    </span>
  )
}

// ---------------------------------------------------------------------------
// EditableTable — tabular section (array field)
// ---------------------------------------------------------------------------

interface EditableTableColumn {
  name: string
  title?: string
  component?: string
  width?: number | string
}

interface EditableTableProps {
  value?: Record<string, unknown>[]
  onChange?: (rows: Record<string, unknown>[]) => void
  disabled?: boolean
}

function EditableTableBase({ value = [], onChange, disabled }: EditableTableProps) {
  const field = useField<ArrayFieldModel>()
  const schema = useFieldSchema()

  // Derive columns from items.properties in the schema
  const itemProps = (schema.items as { properties?: Record<string, { title?: string; "x-component-props"?: { width?: number | string } }> } | undefined)?.properties ?? {}
  const columns: EditableTableColumn[] = Object.entries(itemProps).map(([name, prop]) => ({
    name,
    title: prop.title ?? name,
    width: prop["x-component-props"]?.width,
  }))

  const addRow = () => {
    if (disabled) return
    const empty: Record<string, unknown> = {}
    columns.forEach((col) => (empty[col.name] = undefined))
    const next = [...value, empty]
    onChange?.(next)
    field.push(empty)
  }

  const removeRow = (idx: number) => {
    if (disabled) return
    const next = value.filter((_, i) => i !== idx)
    onChange?.(next)
    field.remove(idx)
  }

  const updateCell = (rowIdx: number, colName: string, cellValue: unknown) => {
    const next = value.map((row, i) =>
      i === rowIdx ? { ...row, [colName]: cellValue } : row,
    )
    onChange?.(next)
  }

  if (columns.length === 0) {
    return (
      <div className="rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--muted)/0.3)] p-4 text-center text-[12px] text-[hsl(var(--muted-foreground))]">
        No columns defined in schema
      </div>
    )
  }

  return (
    <div className="rounded-[var(--radius)] border border-[hsl(var(--border))] overflow-hidden">
      {/* Column headers */}
      <div
        className="grid text-[11px] font-medium text-[hsl(var(--muted-foreground))] bg-[hsl(var(--secondary))] border-b border-[hsl(var(--border))]"
        style={{ gridTemplateColumns: `${columns.map((c) => (c.width ? `${c.width}px` : "1fr")).join(" ")} 32px` }}
      >
        {columns.map((col) => (
          <div key={col.name} className="px-2 py-1.5 truncate">
            {col.title}
          </div>
        ))}
        <div />
      </div>

      {/* Rows */}
      {value.map((row, rowIdx) => (
        <div
          key={rowIdx}
          className="grid border-b border-[hsl(var(--border))] last:border-b-0 hover:bg-[hsl(var(--secondary)/0.4)] transition-colors"
          style={{ gridTemplateColumns: `${columns.map((c) => (c.width ? `${c.width}px` : "1fr")).join(" ")} 32px` }}
        >
          {columns.map((col) => (
            <div key={col.name} className="border-r border-[hsl(var(--border))] last-of-type:border-r-0">
              <RecursionField
                basePath={field.address}
                name={`${rowIdx}.${col.name}`}
                schema={(schema.items as { properties?: Record<string, unknown> })?.properties?.[col.name] as never ?? {}}
              />
              {/* Fallback plain input for unregistered components */}
              {!(schema.items as { properties?: Record<string, unknown> })?.properties?.[col.name] && (
                <input
                  className="h-8 w-full px-2 text-[13px] bg-transparent outline-none focus:bg-[hsl(var(--secondary)/0.6)]"
                  value={row[col.name] != null ? String(row[col.name]) : ""}
                  disabled={disabled}
                  onChange={(e) => updateCell(rowIdx, col.name, e.target.value)}
                />
              )}
            </div>
          ))}
          <div className="flex items-center justify-center">
            <button
              type="button"
              disabled={disabled}
              className="flex items-center justify-center h-6 w-6 rounded-[var(--radius)] text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--destructive))] hover:bg-[hsl(var(--destructive)/0.08)] transition-colors disabled:opacity-40"
              onClick={() => removeRow(rowIdx)}
              title="Delete row"
            >
              <Trash size={13} weight="bold" />
            </button>
          </div>
        </div>
      ))}

      {/* Footer — add row */}
      <div className="flex items-center px-2 py-1 bg-[hsl(var(--secondary)/0.3)]">
        <button
          type="button"
          disabled={disabled}
          className="flex items-center gap-1 text-[12px] text-[hsl(var(--primary))] hover:text-[hsl(var(--primary)/0.8)] transition-colors disabled:opacity-40"
          onClick={addRow}
        >
          <Plus size={13} weight="bold" />
          Add row
        </button>
        <span className="ml-auto text-[11px] text-[hsl(var(--muted-foreground))]">
          {value.length} {value.length === 1 ? "row" : "rows"}
        </span>
      </div>
    </div>
  )
}

export const EditableTable = connect(EditableTableBase, mapProps({ value: "value" }))

// ---------------------------------------------------------------------------
// Component map — pass this to SchemaField's `components` prop
// ---------------------------------------------------------------------------

export const oesFormilyComponents = {
  // Field components
  Input,
  NumberInput,
  DatePicker,
  Checkbox,
  Select,
  RefField,
  EditableTable,

  // Decorators / layout
  FormItem: FormilyFormItem,
  FormHeader,
  StatusBadge,
}
