import { useState } from "react"
import { Plus, X } from "@phosphor-icons/react"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface NumberDescription {
  precision: number
  scale: number
  nonNegative: boolean
}

export interface StringDescription {
  length: number
  allowedLength: "Fixed" | "Variable"
}

export type DateFractions = "Date" | "DateTime" | "Time"

export interface RefType {
  metaType: string  // e.g. "catalogs", "documents", "enumerations"
  id: string
  name: string
}

export interface TypeDescription {
  useBoolean: boolean
  useNumber: boolean
  useString: boolean
  useDate: boolean
  number?: NumberDescription
  string?: StringDescription
  dateFractions?: DateFractions
  refTypes: RefType[]
}

export function emptyTypeDescription(): TypeDescription {
  return {
    useBoolean: false,
    useNumber: false,
    useString: false,
    useDate: false,
    refTypes: [],
  }
}

interface TypeDescriptionEditorProps {
  value: TypeDescription
  onChange: (v: TypeDescription) => void
  availableRefs?: RefType[]
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export function TypeDescriptionEditor({
  value,
  onChange,
  availableRefs = [],
}: TypeDescriptionEditorProps) {
  const [addRefOpen, setAddRefOpen] = useState(false)

  function set(patch: Partial<TypeDescription>) {
    onChange({ ...value, ...patch })
  }

  function toggleBoolean() { set({ useBoolean: !value.useBoolean }) }
  function toggleNumber()  {
    const next = !value.useNumber
    set({
      useNumber: next,
      number: next ? (value.number ?? { precision: 10, scale: 2, nonNegative: false }) : undefined,
    })
  }
  function toggleString()  {
    const next = !value.useString
    set({
      useString: next,
      string: next ? (value.string ?? { length: 100, allowedLength: "Variable" }) : undefined,
    })
  }
  function toggleDate()    {
    const next = !value.useDate
    set({
      useDate: next,
      dateFractions: next ? (value.dateFractions ?? "DateTime") : undefined,
    })
  }

  function removeRef(id: string) {
    set({ refTypes: value.refTypes.filter((r) => r.id !== id) })
  }

  function addRef(ref: RefType) {
    if (value.refTypes.some((r) => r.id === ref.id)) return
    set({ refTypes: [...value.refTypes, ref] })
    setAddRefOpen(false)
  }

  return (
    <div className="space-y-3 text-[11px]">
      {/* Primitive type checkboxes */}
      <div className="grid grid-cols-2 gap-x-4 gap-y-2">
        <CheckRow label="Boolean" checked={value.useBoolean} onChange={toggleBoolean} />
        <CheckRow label="Number"  checked={value.useNumber}  onChange={toggleNumber}  />
        <CheckRow label="String"  checked={value.useString}  onChange={toggleString}  />
        <CheckRow label="Date"    checked={value.useDate}    onChange={toggleDate}    />
      </div>

      {/* Number options */}
      {value.useNumber && value.number && (
        <fieldset className="rounded-[2px] border border-[hsl(var(--border))] p-2 space-y-2">
          <legend className="px-1 text-[10px] font-medium text-[hsl(var(--muted-foreground))] uppercase tracking-wide">
            Number
          </legend>
          <NumberRow
            label="Precision"
            value={value.number.precision}
            onChange={(v) => set({ number: { ...value.number!, precision: v } })}
          />
          <NumberRow
            label="Scale"
            value={value.number.scale}
            onChange={(v) => set({ number: { ...value.number!, scale: v } })}
          />
          <CheckRow
            label="Non-negative"
            checked={value.number.nonNegative}
            onChange={() => set({ number: { ...value.number!, nonNegative: !value.number!.nonNegative } })}
          />
        </fieldset>
      )}

      {/* String options */}
      {value.useString && value.string && (
        <fieldset className="rounded-[2px] border border-[hsl(var(--border))] p-2 space-y-2">
          <legend className="px-1 text-[10px] font-medium text-[hsl(var(--muted-foreground))] uppercase tracking-wide">
            String
          </legend>
          <NumberRow
            label="Length"
            value={value.string.length}
            onChange={(v) => set({ string: { ...value.string!, length: v } })}
          />
          <div className="flex items-center gap-2">
            <span className="w-24 text-[hsl(var(--muted-foreground))]">Allowed length</span>
            <select
              value={value.string.allowedLength}
              onChange={(e) =>
                set({ string: { ...value.string!, allowedLength: e.target.value as "Fixed" | "Variable" } })
              }
              className="h-6 rounded-[2px] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-1 text-[11px] outline-none focus:border-[hsl(var(--ring))]"
            >
              <option value="Fixed">Fixed</option>
              <option value="Variable">Variable</option>
            </select>
          </div>
        </fieldset>
      )}

      {/* Date fractions */}
      {value.useDate && (
        <fieldset className="rounded-[2px] border border-[hsl(var(--border))] p-2">
          <legend className="px-1 text-[10px] font-medium text-[hsl(var(--muted-foreground))] uppercase tracking-wide">
            Date
          </legend>
          <div className="flex gap-3">
            {(["Date", "DateTime", "Time"] as DateFractions[]).map((f) => (
              <label key={f} className="flex items-center gap-1.5 cursor-pointer">
                <input
                  type="radio"
                  name="dateFractions"
                  value={f}
                  checked={value.dateFractions === f}
                  onChange={() => set({ dateFractions: f })}
                  className="accent-[hsl(var(--primary))]"
                />
                {f}
              </label>
            ))}
          </div>
        </fieldset>
      )}

      {/* Reference types */}
      <div className="space-y-1">
        <div className="flex items-center justify-between">
          <span className="font-medium text-[hsl(var(--muted-foreground))]">Reference types</span>
          {availableRefs.length > 0 && (
            <button
              type="button"
              onClick={() => setAddRefOpen((v) => !v)}
              className="flex items-center gap-1 text-[10px] text-[hsl(var(--primary))] hover:underline"
            >
              <Plus size={10} weight="bold" /> Add
            </button>
          )}
        </div>

        {value.refTypes.map((ref) => (
          <div
            key={ref.id}
            className="flex items-center justify-between rounded-[2px] bg-[hsl(var(--secondary))] px-2 py-1"
          >
            <span className="text-[hsl(var(--foreground))]">{ref.name}</span>
            <span className="text-[hsl(var(--muted-foreground))] mr-2">{ref.metaType}</span>
            <button
              type="button"
              onClick={() => removeRef(ref.id)}
              className="text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--destructive))]"
            >
              <X size={11} weight="bold" />
            </button>
          </div>
        ))}

        {addRefOpen && (
          <div className="rounded-[2px] border border-[hsl(var(--border))] bg-[hsl(var(--background))] p-2 space-y-1 max-h-40 overflow-y-auto">
            {availableRefs.filter((r) => !value.refTypes.some((e) => e.id === r.id)).map((ref) => (
              <button
                key={ref.id}
                type="button"
                onClick={() => addRef(ref)}
                className="flex w-full items-center gap-2 rounded-[2px] px-2 py-1 text-left hover:bg-[hsl(var(--secondary))] transition-colors"
              >
                <span className="flex-1">{ref.name}</span>
                <span className="text-[hsl(var(--muted-foreground))]">{ref.metaType}</span>
              </button>
            ))}
          </div>
        )}
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Small field helpers
// ---------------------------------------------------------------------------

function CheckRow({
  label,
  checked,
  onChange,
}: { label: string; checked: boolean; onChange: () => void }) {
  return (
    <label className="flex items-center gap-2 cursor-pointer">
      <input
        type="checkbox"
        checked={checked}
        onChange={onChange}
        className="accent-[hsl(var(--primary))] h-3 w-3"
      />
      <span className="text-[hsl(var(--foreground))]">{label}</span>
    </label>
  )
}

function NumberRow({
  label,
  value,
  onChange,
}: { label: string; value: number; onChange: (v: number) => void }) {
  return (
    <div className="flex items-center gap-2">
      <span className="w-24 text-[hsl(var(--muted-foreground))]">{label}</span>
      <input
        type="number"
        value={value}
        min={0}
        onChange={(e) => onChange(Number(e.target.value))}
        className="h-6 w-20 rounded-[2px] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-1.5 text-[11px] outline-none focus:border-[hsl(var(--ring))] tabular-nums"
      />
    </div>
  )
}
