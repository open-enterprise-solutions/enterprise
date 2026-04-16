/**
 * FormRenderer — renders a server-side form from JSON control tree.
 *
 * The server (ibWebVisualHost) sends a recursive tree of controls:
 *   { id, type, name?, props?, children? }
 *
 * This component maps each control type to a React component and
 * renders the full tree. Events are sent back to the server via
 * the sendEvent callback.
 *
 * 21 control types from ibWebVisualHost::MapControlType().
 */

import { memo, useCallback, type ReactNode } from "react"
import { DotsThree } from "@phosphor-icons/react"
import type { ControlNode } from "@/hooks/useFormSession"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface FormRendererProps {
  layout: ControlNode
  onEvent: (controlId: number, event: string, data?: Record<string, unknown>) => void
}

interface ControlProps {
  node: ControlNode
  onEvent: FormRendererProps["onEvent"]
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

function px(v: string | undefined): string | undefined {
  if (!v) return undefined
  return /^\d+$/.test(v) ? `${v}px` : v
}

function bool(v: string | undefined): boolean {
  return v === "1" || v === "true"
}

function renderChildren(node: ControlNode, onEvent: FormRendererProps["onEvent"]): ReactNode {
  if (!node.children?.length) return null
  return node.children.map((child) => (
    <ControlRenderer key={child.id} node={child} onEvent={onEvent} />
  ))
}

// ---------------------------------------------------------------------------
// Control components — one per server control type
// ---------------------------------------------------------------------------

function FormControl({ node, onEvent }: ControlProps) {
  // Form root renders as transparent container — page provides the title
  return (
    <div className="flex flex-col gap-3" data-oes-form={node.name}>
      {renderChildren(node, onEvent)}
    </div>
  )
}

function BoxSizerControl({ node, onEvent }: ControlProps) {
  const vertical = (node.props?.orientation ?? "vertical") !== "horizontal"
  return (
    <div
      className={`flex ${vertical ? "flex-col" : "flex-row"} gap-2`}
      style={{ width: px(node.props?.width), height: px(node.props?.height) }}
      data-oes-sizer={node.name}
    >
      {renderChildren(node, onEvent)}
    </div>
  )
}

function GridSizerControl({ node, onEvent }: ControlProps) {
  const cols = Number(node.props?.cols ?? 2)
  return (
    <div
      className="grid gap-2"
      style={{
        gridTemplateColumns: `repeat(${cols}, 1fr)`,
        width: px(node.props?.width),
      }}
      data-oes-grid={node.name}
    >
      {renderChildren(node, onEvent)}
    </div>
  )
}

function NotebookControl({ node, onEvent }: ControlProps) {
  // Render as tabbed container — pages are children with type "notebook" or custom
  return (
    <div className="border border-[hsl(var(--border))] rounded-[var(--radius)]" data-oes-notebook={node.name}>
      {node.children && node.children.length > 0 && (
        <>
          <div className="flex border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.3)]">
            {node.children.map((page) => (
              <button
                key={page.id}
                type="button"
                className="px-4 py-2 text-[13px] text-[hsl(var(--foreground))] border-b-2 border-transparent first:border-b-[hsl(var(--ring))] hover:bg-[hsl(var(--secondary)/0.5)] transition-colors"
              >
                {page.props?.caption ?? page.name ?? `Page ${page.id}`}
              </button>
            ))}
          </div>
          <div className="p-3">
            {/* Render first page content */}
            {node.children[0] && renderChildren(node.children[0], onEvent)}
          </div>
        </>
      )}
    </div>
  )
}

function GridBoxControl({ node, onEvent }: ControlProps) {
  return (
    <div
      className="border border-[hsl(var(--border))] rounded-[var(--radius)] p-3"
      data-oes-gridbox={node.name}
    >
      {node.props?.caption && (
        <div className="text-[11px] font-medium text-[hsl(var(--muted-foreground))] mb-2">
          {node.props.caption}
        </div>
      )}
      {renderChildren(node, onEvent)}
    </div>
  )
}

function StaticTextControl({ node }: ControlProps) {
  return (
    <span
      className="text-[13px] text-[hsl(var(--foreground))] py-1"
      data-oes-label={node.name}
    >
      {node.props?.label ?? node.props?.caption ?? ""}
    </span>
  )
}

function StaticLineControl() {
  return <hr className="border-[hsl(var(--border))] my-2" />
}

function TextBoxControl({ node, onEvent }: ControlProps) {
  const handleChange = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      onEvent(node.id, "OnChange", { value: e.target.value })
    },
    [node.id, onEvent],
  )

  return (
    <div className="flex flex-col gap-1" data-oes-textbox={node.name}>
      {node.props?.label && (
        <label className="text-[11px] font-medium text-[hsl(var(--muted-foreground))]">
          {node.props.label}
        </label>
      )}
      <input
        type="text"
        className="h-8 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] px-3 text-[13px] text-[hsl(var(--foreground))] focus:outline-none focus:ring-1 focus:ring-[hsl(var(--ring))] transition-colors"
        defaultValue={node.props?.value ?? ""}
        readOnly={bool(node.props?.read_only)}
        placeholder={node.props?.placeholder}
        onChange={handleChange}
      />
    </div>
  )
}

function ButtonControl({ node, onEvent }: ControlProps) {
  const handleClick = useCallback(() => {
    onEvent(node.id, "OnClick")
  }, [node.id, onEvent])

  return (
    <button
      type="button"
      className="h-8 px-4 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.5)] text-[13px] font-medium text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))] focus:outline-none focus:ring-1 focus:ring-[hsl(var(--ring))] transition-colors disabled:opacity-50"
      onClick={handleClick}
      disabled={!bool(node.props?.enabled ?? "1")}
      data-oes-button={node.name}
    >
      {node.props?.caption ?? node.name ?? "Button"}
    </button>
  )
}

function CheckboxControl({ node, onEvent }: ControlProps) {
  const handleChange = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      onEvent(node.id, "OnChange", { value: e.target.checked ? "1" : "0" })
    },
    [node.id, onEvent],
  )

  return (
    <label
      className="flex items-center gap-2 text-[13px] text-[hsl(var(--foreground))] cursor-pointer py-1"
      data-oes-checkbox={node.name}
    >
      <input
        type="checkbox"
        className="h-4 w-4 rounded-[2px] border border-[hsl(var(--border))] accent-[hsl(var(--ring))]"
        defaultChecked={bool(node.props?.value)}
        onChange={handleChange}
      />
      {node.props?.label ?? node.props?.caption ?? ""}
    </label>
  )
}

function RadioButtonControl({ node, onEvent }: ControlProps) {
  const handleChange = useCallback(() => {
    onEvent(node.id, "OnChange", { value: "1" })
  }, [node.id, onEvent])

  return (
    <label
      className="flex items-center gap-2 text-[13px] text-[hsl(var(--foreground))] cursor-pointer py-1"
      data-oes-radio={node.name}
    >
      <input
        type="radio"
        name={node.props?.group ?? node.name}
        className="h-4 w-4 accent-[hsl(var(--ring))]"
        defaultChecked={bool(node.props?.value)}
        onChange={handleChange}
      />
      {node.props?.label ?? node.props?.caption ?? ""}
    </label>
  )
}

function ChoiceControl({ node, onEvent }: ControlProps) {
  const handleChange = useCallback(
    (e: React.ChangeEvent<HTMLSelectElement>) => {
      onEvent(node.id, "OnChange", { value: e.target.value })
    },
    [node.id, onEvent],
  )

  // Items come as pipe-delimited string from server: "Item1|Item2|Item3"
  const items = (node.props?.items ?? "").split("|").filter(Boolean)

  return (
    <div className="flex flex-col gap-1" data-oes-choice={node.name}>
      {node.props?.label && (
        <label className="text-[11px] font-medium text-[hsl(var(--muted-foreground))]">
          {node.props.label}
        </label>
      )}
      <select
        className="h-8 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] px-3 text-[13px] text-[hsl(var(--foreground))] focus:outline-none focus:ring-1 focus:ring-[hsl(var(--ring))]"
        defaultValue={node.props?.value}
        onChange={handleChange}
      >
        {items.map((item) => (
          <option key={item} value={item}>{item}</option>
        ))}
      </select>
    </div>
  )
}

function ComboBoxControl({ node, onEvent }: ControlProps) {
  // Combobox = editable dropdown — render as input with datalist
  const items = (node.props?.items ?? "").split("|").filter(Boolean)
  const listId = `oes-combo-${node.id}`

  const handleChange = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      onEvent(node.id, "OnChange", { value: e.target.value })
    },
    [node.id, onEvent],
  )

  return (
    <div className="flex flex-col gap-1" data-oes-combobox={node.name}>
      {node.props?.label && (
        <label className="text-[11px] font-medium text-[hsl(var(--muted-foreground))]">
          {node.props.label}
        </label>
      )}
      <input
        type="text"
        list={listId}
        className="h-8 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] px-3 text-[13px] text-[hsl(var(--foreground))] focus:outline-none focus:ring-1 focus:ring-[hsl(var(--ring))]"
        defaultValue={node.props?.value ?? ""}
        onChange={handleChange}
      />
      <datalist id={listId}>
        {items.map((item) => (
          <option key={item} value={item} />
        ))}
      </datalist>
    </div>
  )
}

function ListBoxControl({ node, onEvent }: ControlProps) {
  const items = (node.props?.items ?? "").split("|").filter(Boolean)

  const handleChange = useCallback(
    (e: React.ChangeEvent<HTMLSelectElement>) => {
      onEvent(node.id, "OnChange", { value: e.target.value })
    },
    [node.id, onEvent],
  )

  return (
    <div className="flex flex-col gap-1" data-oes-listbox={node.name}>
      {node.props?.label && (
        <label className="text-[11px] font-medium text-[hsl(var(--muted-foreground))]">
          {node.props.label}
        </label>
      )}
      <select
        multiple
        size={Number(node.props?.rows ?? 5)}
        className="rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] px-3 py-1 text-[13px] text-[hsl(var(--foreground))] focus:outline-none focus:ring-1 focus:ring-[hsl(var(--ring))]"
        onChange={handleChange}
      >
        {items.map((item) => (
          <option key={item} value={item}>{item}</option>
        ))}
      </select>
    </div>
  )
}

function SliderControl({ node, onEvent }: ControlProps) {
  const handleChange = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      onEvent(node.id, "OnChange", { value: e.target.value })
    },
    [node.id, onEvent],
  )

  return (
    <div className="flex flex-col gap-1" data-oes-slider={node.name}>
      {node.props?.label && (
        <label className="text-[11px] font-medium text-[hsl(var(--muted-foreground))]">
          {node.props.label}
        </label>
      )}
      <input
        type="range"
        min={node.props?.min ?? "0"}
        max={node.props?.max ?? "100"}
        defaultValue={node.props?.value ?? "50"}
        className="h-6 accent-[hsl(var(--ring))]"
        onChange={handleChange}
      />
    </div>
  )
}

function GaugeControl({ node }: ControlProps) {
  const value = Number(node.props?.value ?? 0)
  const max = Number(node.props?.max ?? 100)
  const pct = max > 0 ? (value / max) * 100 : 0

  return (
    <div className="flex flex-col gap-1" data-oes-gauge={node.name}>
      {node.props?.label && (
        <label className="text-[11px] font-medium text-[hsl(var(--muted-foreground))]">
          {node.props.label}
        </label>
      )}
      <div className="h-4 rounded-full bg-[hsl(var(--secondary))] overflow-hidden">
        <div
          className="h-full bg-[hsl(var(--ring))] rounded-full transition-[width] duration-300"
          style={{ width: `${pct}%` }}
        />
      </div>
    </div>
  )
}

function TableBoxControl({ node }: ControlProps) {
  // Columns are children with type info in their props
  const columns = node.children ?? []

  return (
    <div className="border border-[hsl(var(--border))] rounded-[var(--radius)] overflow-hidden" data-oes-table={node.name}>
      {node.props?.caption && (
        <div className="px-3 py-1 bg-[hsl(var(--secondary)/0.3)] text-[11px] font-medium text-[hsl(var(--muted-foreground))] border-b border-[hsl(var(--border))]">
          {node.props.caption}
        </div>
      )}
      <div className="overflow-auto">
        <table className="w-full text-[13px]">
          <thead>
            <tr className="bg-[hsl(var(--secondary)/0.4)]">
              {columns.map((col) => (
                <th
                  key={col.id}
                  className="px-3 py-2 text-left text-[11px] font-medium text-[hsl(var(--muted-foreground))] border-b border-[hsl(var(--border))]"
                  style={{ width: px(col.props?.width) }}
                >
                  {col.props?.caption ?? col.name ?? ""}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {/* Data rows will be populated via server updates */}
            <tr>
              <td
                colSpan={columns.length || 1}
                className="px-3 py-4 text-center text-[11px] text-[hsl(var(--muted-foreground))]"
              >
                No data
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>
  )
}

function ToolbarControl({ node, onEvent }: ControlProps) {
  return (
    <div
      className="flex items-center gap-1 px-2 py-1 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.3)]"
      data-oes-toolbar={node.name}
    >
      {renderChildren(node, onEvent)}
    </div>
  )
}

function ToolControl({ node, onEvent }: ControlProps) {
  const handleClick = useCallback(() => {
    onEvent(node.id, "OnClick")
  }, [node.id, onEvent])

  return (
    <button
      type="button"
      className="h-7 px-3 rounded-[var(--radius)] text-[12px] font-medium text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))] transition-colors disabled:opacity-50"
      onClick={handleClick}
      disabled={!bool(node.props?.enabled ?? "1")}
      title={node.props?.tooltip ?? node.props?.caption}
      data-oes-tool={node.name}
    >
      {node.props?.caption ?? node.name ?? ""}
    </button>
  )
}

function HtmlBoxControl({ node }: ControlProps) {
  const html = node.props?.html ?? node.props?.value ?? ""
  return (
    <div
      className="border border-[hsl(var(--border))] rounded-[var(--radius)] p-3 text-[13px] overflow-auto"
      data-oes-html={node.name}
      dangerouslySetInnerHTML={{ __html: html }}
    />
  )
}

function ChartControl({ node }: ControlProps) {
  return (
    <div
      className="border border-[hsl(var(--border))] rounded-[var(--radius)] p-3 flex items-center justify-center text-[11px] text-[hsl(var(--muted-foreground))]"
      style={{ minHeight: px(node.props?.height) ?? "200px" }}
      data-oes-chart={node.name}
    >
      Chart: {node.props?.caption ?? node.name ?? ""}
    </div>
  )
}

function DatePickerControl({ node, onEvent }: ControlProps) {
  const handleChange = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      onEvent(node.id, "OnChange", { value: e.target.value })
    },
    [node.id, onEvent],
  )

  return (
    <div className="flex flex-col gap-1" data-oes-datepicker={node.name}>
      {node.props?.label && (
        <label className="text-[11px] font-medium text-[hsl(var(--muted-foreground))]">
          {node.props.label}
        </label>
      )}
      <input
        type="date"
        className="h-8 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] px-3 text-[13px] text-[hsl(var(--foreground))] focus:outline-none focus:ring-1 focus:ring-[hsl(var(--ring))] transition-colors"
        defaultValue={node.props?.value ?? ""}
        readOnly={bool(node.props?.read_only)}
        onChange={handleChange}
      />
    </div>
  )
}

function NumberInputControl({ node, onEvent }: ControlProps) {
  const scale = Number(node.props?.scale ?? 0)
  const step = scale > 0 ? (1 / Math.pow(10, scale)).toFixed(scale) : "1"

  const handleChange = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      onEvent(node.id, "OnChange", { value: e.target.value })
    },
    [node.id, onEvent],
  )

  return (
    <div className="flex flex-col gap-1" data-oes-numberinput={node.name}>
      {node.props?.label && (
        <label className="text-[11px] font-medium text-[hsl(var(--muted-foreground))]">
          {node.props.label}
        </label>
      )}
      <input
        type="number"
        step={step}
        min={node.props?.min}
        max={node.props?.max}
        className="h-8 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] px-3 text-[13px] text-[hsl(var(--foreground))] focus:outline-none focus:ring-1 focus:ring-[hsl(var(--ring))] transition-colors"
        defaultValue={node.props?.value ?? ""}
        readOnly={bool(node.props?.read_only)}
        onChange={handleChange}
      />
    </div>
  )
}

function RefFieldControl({ node, onEvent }: ControlProps) {
  const handleChange = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      onEvent(node.id, "OnChange", { value: e.target.value })
    },
    [node.id, onEvent],
  )

  const handleBrowse = useCallback(() => {
    onEvent(node.id, "OnChoose", { refType: node.props?.refType ?? "" })
  }, [node.id, onEvent, node.props?.refType])

  return (
    <div className="flex flex-col gap-1" data-oes-reffield={node.name}>
      {node.props?.label && (
        <label className="text-[11px] font-medium text-[hsl(var(--muted-foreground))]">
          {node.props.label}
        </label>
      )}
      <div className="flex gap-1">
        <input
          type="text"
          className="h-8 flex-1 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] px-3 text-[13px] text-[hsl(var(--foreground))] focus:outline-none focus:ring-1 focus:ring-[hsl(var(--ring))] transition-colors"
          defaultValue={node.props?.value ?? ""}
          readOnly={bool(node.props?.read_only)}
          onChange={handleChange}
          title={node.props?.refType ? `Reference: ${node.props.refType}` : undefined}
        />
        <button
          type="button"
          className="h-8 w-8 flex items-center justify-center rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.5)] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))] focus:outline-none focus:ring-1 focus:ring-[hsl(var(--ring))] transition-colors shrink-0"
          onClick={handleBrowse}
          disabled={bool(node.props?.read_only)}
          title="Select..."
        >
          <DotsThree size={14} weight="bold" />
        </button>
      </div>
    </div>
  )
}

function UnknownControl({ node, onEvent }: ControlProps) {
  return (
    <div
      className="border border-dashed border-[hsl(var(--destructive)/0.3)] rounded-[var(--radius)] p-2 text-[11px] text-[hsl(var(--muted-foreground))]"
      data-oes-unknown={node.type}
    >
      <span className="opacity-60">[{node.type}] {node.name}</span>
      {renderChildren(node, onEvent)}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Control type registry
// ---------------------------------------------------------------------------

const CONTROL_MAP: Record<string, React.ComponentType<ControlProps>> = {
  form: FormControl,
  boxsizer: BoxSizerControl,
  gridsizer: GridSizerControl,
  notebook: NotebookControl,
  gridbox: GridBoxControl,
  statictext: StaticTextControl,
  staticline: StaticLineControl,
  textbox: TextBoxControl,
  button: ButtonControl,
  checkbox: CheckboxControl,
  radiobutton: RadioButtonControl,
  choice: ChoiceControl,
  combobox: ComboBoxControl,
  listbox: ListBoxControl,
  slider: SliderControl,
  gauge: GaugeControl,
  tablebox: TableBoxControl,
  toolbar: ToolbarControl,
  tool: ToolControl,
  htmlbox: HtmlBoxControl,
  chart: ChartControl,
  datepicker: DatePickerControl,
  numberinput: NumberInputControl,
  reffield: RefFieldControl,
}

// ---------------------------------------------------------------------------
// Recursive renderer
// ---------------------------------------------------------------------------

const ControlRenderer = memo(function ControlRenderer({ node, onEvent }: ControlProps) {
  const Component = CONTROL_MAP[node.type] ?? UnknownControl
  return <Component node={node} onEvent={onEvent} />
})

// ---------------------------------------------------------------------------
// FormRenderer — top-level component
// ---------------------------------------------------------------------------

export const FormRenderer = memo(function FormRenderer({ layout, onEvent }: FormRendererProps) {
  return (
    <div className="h-full bg-[hsl(var(--background))]">
      <ControlRenderer node={layout} onEvent={onEvent} />
    </div>
  )
})
