/**
 * DataView — unified data display component with three rendering modes.
 *
 * Modes:
 *   table  — dense sticky-header table with sort, zebra rows, status strip,
 *            column resize via CSS, full keyboard navigation
 *   list   — compact single-column list, icon per meta type, key fields inline
 *   cards  — 2–3 column grid of summary cards with hover effect
 *
 * Keyboard navigation (table mode):
 *   Arrow Up/Down  — move between rows
 *   Enter          — activate row (onRowClick)
 *   Tab            — moves to mode toggle bar (standard browser flow)
 *
 * Accessibility:
 *   - role="grid" with aria-rowcount / aria-colcount on the table
 *   - aria-sort on sortable column headers
 *   - aria-busy on loading state
 *   - aria-label on icon buttons
 */

import { useRef, useState, useCallback, type KeyboardEvent } from "react"
import {
  Table,
  List,
  GridFour,
  ArrowUp,
  ArrowDown,
  ArrowsDownUp,
  Package,
  FileText,
  ChartBar,
  Funnel,
} from "@phosphor-icons/react"
import { StatusBadge, type DocumentStatus } from "./StatusBadge"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface DataColumn {
  key: string
  title: string
  type?: "string" | "number" | "date" | "boolean" | "ref"
  width?: number
}

export type ViewMode = "table" | "list" | "cards"
export type MetaType = "catalog" | "document" | "register"

export interface DataViewProps {
  data: Record<string, unknown>[]
  columns: DataColumn[]
  mode: ViewMode
  onModeChange: (mode: ViewMode) => void
  onRowClick?: (row: Record<string, unknown>) => void
  onSort?: (field: string, order: "asc" | "desc") => void
  sortField?: string
  sortOrder?: "asc" | "desc"
  loading?: boolean
  metaType?: MetaType
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function cn(...parts: (string | false | null | undefined)[]): string {
  return parts.filter(Boolean).join(" ")
}

function formatCellValue(value: unknown, type?: string): string {
  if (value == null || value === "") return "—"
  if (type === "date" && typeof value === "string") {
    try {
      return new Intl.DateTimeFormat(undefined, {
        year: "numeric",
        month: "2-digit",
        day: "2-digit",
      }).format(new Date(value))
    } catch {
      return String(value)
    }
  }
  if (type === "number" && typeof value === "number") {
    return new Intl.NumberFormat(undefined, { maximumFractionDigits: 4 }).format(value)
  }
  if (type === "boolean") {
    return value ? "Yes" : "No"
  }
  if (type === "ref" && typeof value === "object" && value !== null) {
    return (value as Record<string, unknown>).presentation as string ?? String(value)
  }
  return String(value)
}

function getStatusStripClass(status: unknown): string {
  if (status === "posted") return "status-strip status-strip--posted"
  if (status === "deleted" || status === "cancelled") return "status-strip status-strip--deleted"
  if (status === "inProgress" || status === "in_progress") return "status-strip status-strip--progress"
  return "status-strip status-strip--draft"
}

function getMetaIcon(metaType?: MetaType) {
  if (metaType === "document") return <FileText size={16} weight="duotone" />
  if (metaType === "register") return <ChartBar size={16} weight="duotone" />
  return <Package size={16} weight="duotone" />
}

// ---------------------------------------------------------------------------
// Skeleton rows
// ---------------------------------------------------------------------------

function TableSkeleton({ columnCount }: { columnCount: number }) {
  return (
    <>
      {Array.from({ length: 8 }).map((_, i) => (
        <tr key={i} aria-hidden="true">
          {Array.from({ length: columnCount }).map((_, j) => (
            <td key={j} className="px-2 py-1.5 border-b border-[hsl(var(--border))]">
              <div
                className="skeleton rounded-[var(--radius)]"
                style={{ height: 14, width: j === 0 ? 80 : "100%" }}
              />
            </td>
          ))}
        </tr>
      ))}
    </>
  )
}

function ListSkeleton() {
  return (
    <>
      {Array.from({ length: 8 }).map((_, i) => (
        <div
          key={i}
          aria-hidden="true"
          className="flex items-center gap-3 px-3 py-2 border-b border-[hsl(var(--border))]"
        >
          <div className="skeleton rounded-[var(--radius)] shrink-0" style={{ width: 28, height: 28 }} />
          <div className="flex flex-col gap-1 flex-1">
            <div className="skeleton rounded-[var(--radius)]" style={{ height: 13, width: "40%" }} />
            <div className="skeleton rounded-[var(--radius)]" style={{ height: 11, width: "60%" }} />
          </div>
          <div className="skeleton rounded-[var(--radius)]" style={{ height: 18, width: 52 }} />
        </div>
      ))}
    </>
  )
}

function CardSkeleton() {
  return (
    <>
      {Array.from({ length: 6 }).map((_, i) => (
        <div
          key={i}
          aria-hidden="true"
          className="rounded-[var(--radius)] border border-[hsl(var(--border))] p-3 flex flex-col gap-2"
        >
          <div className="skeleton rounded-[var(--radius)]" style={{ height: 13, width: "60%" }} />
          <div className="skeleton rounded-[var(--radius)]" style={{ height: 11, width: "80%" }} />
          <div className="skeleton rounded-[var(--radius)]" style={{ height: 11, width: "50%" }} />
        </div>
      ))}
    </>
  )
}

// ---------------------------------------------------------------------------
// Mode toggle
// ---------------------------------------------------------------------------

const MODE_OPTIONS: { mode: ViewMode; icon: React.ReactNode; label: string }[] = [
  { mode: "table", icon: <Table size={15} weight="duotone" />, label: "Table view" },
  { mode: "list", icon: <List size={15} weight="duotone" />, label: "List view" },
  { mode: "cards", icon: <GridFour size={15} weight="duotone" />, label: "Cards view" },
]

function ModeToggle({
  mode,
  onModeChange,
}: {
  mode: ViewMode
  onModeChange: (m: ViewMode) => void
}) {
  return (
    <div
      role="group"
      aria-label="View mode"
      className="flex items-center rounded-[var(--radius)] border border-[hsl(var(--border))] overflow-hidden"
    >
      {MODE_OPTIONS.map(({ mode: m, icon, label }) => (
        <button
          key={m}
          type="button"
          aria-label={label}
          aria-pressed={mode === m}
          onClick={() => onModeChange(m)}
          className={cn(
            "flex items-center justify-center h-7 w-8 transition-colors border-r border-[hsl(var(--border))] last:border-r-0",
            mode === m
              ? "bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))]"
              : "bg-[hsl(var(--background))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))]",
          )}
        >
          {icon}
        </button>
      ))}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Sort icon
// ---------------------------------------------------------------------------

function SortIcon({ field, sortField, sortOrder }: {
  field: string
  sortField?: string
  sortOrder?: "asc" | "desc"
}) {
  if (field !== sortField) {
    return (
      <ArrowsDownUp
        size={12}
        weight="bold"
        className="opacity-30 group-hover:opacity-70 transition-opacity"
      />
    )
  }
  return sortOrder === "asc"
    ? <ArrowUp size={12} weight="bold" className="text-[hsl(var(--primary))]" />
    : <ArrowDown size={12} weight="bold" className="text-[hsl(var(--primary))]" />
}

// ---------------------------------------------------------------------------
// Table mode
// ---------------------------------------------------------------------------

function TableView({
  data,
  columns,
  onRowClick,
  onSort,
  sortField,
  sortOrder,
  loading,
  metaType,
}: Omit<DataViewProps, "mode" | "onModeChange">) {
  const [focusedRow, setFocusedRow] = useState<number>(-1)
  const tbodyRef = useRef<HTMLTableSectionElement>(null)
  const isDocument = metaType === "document"

  const handleSort = useCallback(
    (key: string) => {
      if (!onSort) return
      const nextOrder = sortField === key && sortOrder === "asc" ? "desc" : "asc"
      onSort(key, nextOrder)
    },
    [onSort, sortField, sortOrder],
  )

  const handleRowKeyDown = useCallback(
    (e: KeyboardEvent<HTMLTableRowElement>, idx: number, row: Record<string, unknown>) => {
      if (e.key === "ArrowDown") {
        e.preventDefault()
        const next = Math.min(idx + 1, data.length - 1)
        setFocusedRow(next)
        const rows = tbodyRef.current?.querySelectorAll("tr")
        ;(rows?.[next] as HTMLElement | undefined)?.focus()
      } else if (e.key === "ArrowUp") {
        e.preventDefault()
        const prev = Math.max(idx - 1, 0)
        setFocusedRow(prev)
        const rows = tbodyRef.current?.querySelectorAll("tr")
        ;(rows?.[prev] as HTMLElement | undefined)?.focus()
      } else if (e.key === "Enter") {
        e.preventDefault()
        onRowClick?.(row)
      }
    },
    [data.length, onRowClick],
  )

  const ariaSortAttr = (key: string): "ascending" | "descending" | "none" => {
    if (key !== sortField) return "none"
    return sortOrder === "asc" ? "ascending" : "descending"
  }

  return (
    <div className="flex-1 overflow-auto relative" style={{ minHeight: 0 }}>
      <table
        role="grid"
        aria-busy={loading}
        aria-rowcount={loading ? undefined : data.length}
        aria-colcount={columns.length}
        className="w-full border-collapse text-[13px]"
        style={{ tableLayout: "fixed" }}
      >
        {/* Sticky header */}
        <thead className="sticky top-0 z-10 bg-[hsl(var(--secondary))]">
          <tr>
            {/* Status strip column for documents */}
            {isDocument && (
              <th
                aria-label="Status"
                style={{ width: 4, padding: 0, borderBottom: "1px solid hsl(var(--border))" }}
              />
            )}
            {columns.map((col) => (
              <th
                key={col.key}
                aria-sort={ariaSortAttr(col.key)}
                scope="col"
                style={{
                  width: col.width ?? undefined,
                  borderBottom: "1px solid hsl(var(--border))",
                  borderRight: "1px solid hsl(var(--border))",
                  padding: "6px 8px",
                  textAlign: col.type === "number" ? "right" : "left",
                  fontWeight: 500,
                  fontSize: 11,
                  color: "hsl(var(--muted-foreground))",
                  whiteSpace: "nowrap",
                  userSelect: "none",
                  cursor: onSort ? "pointer" : "default",
                  position: "relative",
                }}
                className="group last:border-r-0"
                onClick={() => handleSort(col.key)}
                tabIndex={onSort ? 0 : undefined}
                onKeyDown={(e) => {
                  if (onSort && (e.key === "Enter" || e.key === " ")) {
                    e.preventDefault()
                    handleSort(col.key)
                  }
                }}
              >
                <span className="flex items-center gap-1">
                  {col.type === "number" ? (
                    <>
                      <span className="flex-1" />
                      <span className="truncate">{col.title}</span>
                      {onSort && (
                        <SortIcon field={col.key} sortField={sortField} sortOrder={sortOrder} />
                      )}
                    </>
                  ) : (
                    <>
                      <span className="truncate flex-1">{col.title}</span>
                      {onSort && (
                        <SortIcon field={col.key} sortField={sortField} sortOrder={sortOrder} />
                      )}
                    </>
                  )}
                </span>
              </th>
            ))}
          </tr>
        </thead>

        <tbody ref={tbodyRef}>
          {loading ? (
            <TableSkeleton columnCount={columns.length + (isDocument ? 1 : 0)} />
          ) : data.length === 0 ? (
            <tr>
              <td
                colSpan={columns.length + (isDocument ? 1 : 0)}
                className="px-4 py-8 text-center text-[12px] text-[hsl(var(--muted-foreground))]"
              >
                <Funnel size={24} weight="duotone" className="mx-auto mb-2 opacity-40" />
                No records found
              </td>
            </tr>
          ) : (
            data.map((row, idx) => {
              const isEven = idx % 2 === 0
              const status = row["status"] as string | undefined
              const isActive = focusedRow === idx

              return (
                <tr
                  key={row["uuid"] as string ?? idx}
                  role="row"
                  aria-rowindex={idx + 1}
                  tabIndex={0}
                  className={cn(
                    "transition-colors outline-none",
                    isEven
                      ? "bg-[hsl(var(--background))]"
                      : "bg-[hsl(var(--secondary)/0.4)]",
                    (onRowClick || isActive) && "hover:bg-[hsl(var(--primary)/0.06)] cursor-pointer",
                    isActive && "ring-1 ring-inset ring-[hsl(var(--ring)/0.4)]",
                  )}
                  onClick={() => onRowClick?.(row)}
                  onFocus={() => setFocusedRow(idx)}
                  onKeyDown={(e) => handleRowKeyDown(e, idx, row)}
                >
                  {/* Status strip cell */}
                  {isDocument && (
                    <td
                      aria-hidden="true"
                      style={{ padding: 0, width: 4 }}
                      className={getStatusStripClass(status)}
                    />
                  )}

                  {columns.map((col) => {
                    const rawValue = row[col.key]
                    const formatted = formatCellValue(rawValue, col.type)
                    const isStatus = col.key === "status" && typeof rawValue === "string"

                    return (
                      <td
                        key={col.key}
                        role="gridcell"
                        style={{
                          padding: "4px 8px",
                          borderBottom: "1px solid hsl(var(--border))",
                          borderRight: "1px solid hsl(var(--border))",
                          overflow: "hidden",
                          textOverflow: "ellipsis",
                          whiteSpace: "nowrap",
                          textAlign: col.type === "number" ? "right" : "left",
                          fontVariantNumeric: col.type === "number" ? "tabular-nums" : undefined,
                          color: col.type === "number"
                            ? "hsl(var(--foreground))"
                            : "hsl(var(--foreground))",
                        }}
                        className="last:border-r-0"
                        title={formatted === "—" ? undefined : formatted}
                      >
                        {isStatus ? (
                          <StatusBadge
                            status={rawValue as DocumentStatus}
                            size="sm"
                          />
                        ) : (
                          formatted
                        )}
                      </td>
                    )
                  })}
                </tr>
              )
            })
          )}
        </tbody>
      </table>
    </div>
  )
}

// ---------------------------------------------------------------------------
// List mode
// ---------------------------------------------------------------------------

function ListView({
  data,
  columns,
  onRowClick,
  loading,
  metaType,
}: Omit<DataViewProps, "mode" | "onModeChange" | "onSort" | "sortField" | "sortOrder">) {
  // Primary field: first column; secondary: next two columns
  const [primary, ...secondaries] = columns
  const metaIcon = getMetaIcon(metaType)

  if (loading) {
    return (
      <div className="flex-1 overflow-auto" style={{ minHeight: 0 }} aria-busy="true">
        <ListSkeleton />
      </div>
    )
  }

  if (data.length === 0) {
    return (
      <div className="flex flex-col items-center justify-center flex-1 py-12 text-[12px] text-[hsl(var(--muted-foreground))]">
        <Funnel size={24} weight="duotone" className="mb-2 opacity-40" />
        No records found
      </div>
    )
  }

  return (
    <div
      role="list"
      aria-label="Records list"
      className="flex-1 overflow-auto divide-y divide-[hsl(var(--border))]"
      style={{ minHeight: 0 }}
    >
      {data.map((row, idx) => {
        const primaryValue = primary ? formatCellValue(row[primary.key], primary.type) : "—"
        const secondaryParts = secondaries
          .slice(0, 2)
          .map((col) => formatCellValue(row[col.key], col.type))
          .filter((v) => v !== "—")
        const status = row["status"] as DocumentStatus | undefined
        const isDocument = metaType === "document"

        return (
          <div
            key={row["uuid"] as string ?? idx}
            role="listitem"
            tabIndex={0}
            className={cn(
              "flex items-center gap-3 px-3 py-2 transition-colors outline-none",
              isDocument && getStatusStripClass(status),
              onRowClick && "hover:bg-[hsl(var(--secondary)/0.6)] cursor-pointer",
              "focus:bg-[hsl(var(--primary)/0.06)] focus:ring-1 focus:ring-inset focus:ring-[hsl(var(--ring)/0.4)]",
            )}
            onClick={() => onRowClick?.(row)}
            onKeyDown={(e) => {
              if (e.key === "Enter") onRowClick?.(row)
            }}
          >
            {/* Meta type icon badge */}
            <span
              aria-hidden="true"
              className="shrink-0 flex items-center justify-center w-7 h-7 rounded-[var(--radius)] bg-[hsl(var(--secondary))] text-[hsl(var(--muted-foreground))]"
            >
              {metaIcon}
            </span>

            {/* Text block */}
            <div className="flex-1 min-w-0">
              <div className="truncate text-[13px] font-medium text-[hsl(var(--foreground))]">
                {primaryValue}
              </div>
              {secondaryParts.length > 0 && (
                <div className="truncate text-[11px] text-[hsl(var(--muted-foreground))]">
                  {secondaryParts.join(" · ")}
                </div>
              )}
            </div>

            {/* Status badge (documents) */}
            {isDocument && status && (
              <StatusBadge status={status} size="sm" />
            )}
          </div>
        )
      })}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Cards mode
// ---------------------------------------------------------------------------

function CardsView({
  data,
  columns,
  onRowClick,
  loading,
  metaType,
}: Omit<DataViewProps, "mode" | "onModeChange" | "onSort" | "sortField" | "sortOrder">) {
  const [primary, ...rest] = columns
  const detailColumns = rest.slice(0, 3)
  const metaIcon = getMetaIcon(metaType)
  const isDocument = metaType === "document"

  if (loading) {
    return (
      <div
        className="flex-1 overflow-auto p-3"
        style={{ minHeight: 0 }}
        aria-busy="true"
      >
        <div className="grid gap-3" style={{ gridTemplateColumns: "repeat(auto-fill, minmax(220px, 1fr))" }}>
          <CardSkeleton />
        </div>
      </div>
    )
  }

  if (data.length === 0) {
    return (
      <div className="flex flex-col items-center justify-center flex-1 py-12 text-[12px] text-[hsl(var(--muted-foreground))]">
        <Funnel size={24} weight="duotone" className="mb-2 opacity-40" />
        No records found
      </div>
    )
  }

  return (
    <div
      role="list"
      aria-label="Records cards"
      className="flex-1 overflow-auto p-3"
      style={{ minHeight: 0 }}
    >
      <div
        className="grid gap-3"
        style={{ gridTemplateColumns: "repeat(auto-fill, minmax(220px, 1fr))" }}
      >
        {data.map((row, idx) => {
          const primaryValue = primary ? formatCellValue(row[primary.key], primary.type) : "—"
          const status = row["status"] as DocumentStatus | undefined
          const code = row["code"] != null ? String(row["code"]) : undefined

          return (
            <div
              key={row["uuid"] as string ?? idx}
              role="listitem"
              tabIndex={0}
              className={cn(
                "rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--card))] p-3",
                "flex flex-col gap-2 transition-all outline-none",
                "hover:border-[hsl(var(--primary)/0.4)] hover:shadow-sm",
                onRowClick && "cursor-pointer",
                "focus:ring-1 focus:ring-[hsl(var(--ring)/0.5)]",
              )}
              onClick={() => onRowClick?.(row)}
              onKeyDown={(e) => {
                if (e.key === "Enter") onRowClick?.(row)
              }}
            >
              {/* Card header: icon + code + status */}
              <div className="flex items-center gap-2">
                <span
                  aria-hidden="true"
                  className="shrink-0 flex items-center justify-center w-6 h-6 rounded-[var(--radius)] bg-[hsl(var(--secondary))] text-[hsl(var(--muted-foreground))]"
                >
                  {metaIcon}
                </span>
                {code && (
                  <span className="text-[11px] font-medium text-[hsl(var(--muted-foreground))] tabular-nums">
                    #{code}
                  </span>
                )}
                {isDocument && status && (
                  <span className="ml-auto">
                    <StatusBadge status={status} size="sm" />
                  </span>
                )}
              </div>

              {/* Primary field (title) */}
              <div
                className="text-[13px] font-medium text-[hsl(var(--foreground))] leading-snug"
                style={{
                  overflow: "hidden",
                  display: "-webkit-box",
                  WebkitLineClamp: 2,
                  WebkitBoxOrient: "vertical",
                }}
              >
                {primaryValue}
              </div>

              {/* Detail fields */}
              {detailColumns.length > 0 && (
                <dl className="flex flex-col gap-0.5">
                  {detailColumns.map((col) => {
                    const val = formatCellValue(row[col.key], col.type)
                    if (val === "—") return null
                    return (
                      <div key={col.key} className="flex items-baseline gap-1 min-w-0">
                        <dt className="shrink-0 text-[11px] text-[hsl(var(--muted-foreground))]">
                          {col.title}:
                        </dt>
                        <dd
                          className="text-[12px] text-[hsl(var(--foreground))] truncate"
                          style={{ fontVariantNumeric: col.type === "number" ? "tabular-nums" : undefined }}
                        >
                          {val}
                        </dd>
                      </div>
                    )
                  })}
                </dl>
              )}
            </div>
          )
        })}
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// DataView — main export
// ---------------------------------------------------------------------------

export function DataView({
  data,
  columns,
  mode,
  onModeChange,
  onRowClick,
  onSort,
  sortField,
  sortOrder,
  loading = false,
  metaType,
}: DataViewProps) {
  return (
    <div
      className="flex flex-col bg-[hsl(var(--background))] border border-[hsl(var(--border))] rounded-[var(--radius)] overflow-hidden"
      style={{ minHeight: 0, flex: 1 }}
    >
      {/* Toolbar: mode toggle + record count */}
      <div className="flex items-center justify-between px-3 py-2 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.4)] shrink-0">
        <ModeToggle mode={mode} onModeChange={onModeChange} />

        <span className="text-[11px] text-[hsl(var(--muted-foreground))] tabular-nums">
          {loading ? (
            <span className="skeleton inline-block rounded-[var(--radius)]" style={{ width: 60, height: 12 }} />
          ) : (
            `${data.length} record${data.length !== 1 ? "s" : ""}`
          )}
        </span>
      </div>

      {/* View body */}
      {mode === "table" && (
        <TableView
          data={data}
          columns={columns}
          onRowClick={onRowClick}
          onSort={onSort}
          sortField={sortField}
          sortOrder={sortOrder}
          loading={loading}
          metaType={metaType}
        />
      )}

      {mode === "list" && (
        <ListView
          data={data}
          columns={columns}
          onRowClick={onRowClick}
          loading={loading}
          metaType={metaType}
        />
      )}

      {mode === "cards" && (
        <CardsView
          data={data}
          columns={columns}
          onRowClick={onRowClick}
          loading={loading}
          metaType={metaType}
        />
      )}
    </div>
  )
}
