import { useState, useEffect, useCallback, useRef } from "react"
import { useNavigate, useParams } from "react-router-dom"
import { useList } from "@refinedev/core"
import {
  Plus,
  MagnifyingGlass,
  Table,
  Rows,
  SquaresFour,
  ArrowUp,
  ArrowDown,
  ArrowsDownUp,
  CaretLeft,
  CaretRight,
} from "@phosphor-icons/react"
import { useTabStore } from "@/stores/tab-store"
import { useMetadata, type MetaObjectRef } from "@/hooks/useMetadata"
import { toApiResource } from "@/lib/resource-utils"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

type ViewMode = "table" | "list" | "cards"
type SortOrder = "asc" | "desc" | null

interface ColumnDef {
  key: string
  label: string
}

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

// Derive columns from metadata attributes
function deriveColumns(item: MetaObjectRef | undefined, isDocument: boolean): ColumnDef[] {
  if (!item) {
    return isDocument
      ? [{ key: "number", label: "Number" }, { key: "date", label: "Date" }]
      : [{ key: "code", label: "Code" }, { key: "description", label: "Description" }]
  }

  const cols: ColumnDef[] = []
  const sys = item.systemAttributes as MetaSystemAttributes | undefined

  if (isDocument) {
    if (sys?.number !== undefined) cols.push({ key: "number", label: "Number" })
    if (sys?.date !== undefined) cols.push({ key: "date", label: "Date" })
  } else {
    if (sys?.code !== undefined) cols.push({ key: "code", label: "Code" })
    if (sys?.description !== undefined) cols.push({ key: "description", label: "Description" })
  }

  // User-defined attributes
  const attrs = (item.attributes ?? []) as MetaAttribute[]
  for (const attr of attrs) {
    if (!attr.name) continue
    cols.push({ key: attr.name.toLowerCase(), label: attr.synonym ?? attr.name })
  }

  // Fallback: ensure at least one visible column
  if (cols.length === 0) {
    cols.push({ key: "description", label: "Description" })
  }

  return cols
}

// ---------------------------------------------------------------------------
// Status badge for document rows
// ---------------------------------------------------------------------------

function StatusBadge({ status }: { status?: string }) {
  if (!status) return null

  const map: Record<string, { label: string; cls: string }> = {
    posted:    { label: "Posted",    cls: "bg-[hsl(var(--status-posted)/0.15)] text-[hsl(var(--status-posted))]" },
    draft:     { label: "Draft",     cls: "bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))]" },
    cancelled: { label: "Cancelled", cls: "bg-[hsl(var(--destructive)/0.12)] text-[hsl(var(--destructive))]" },
  }
  const cfg = map[status] ?? { label: status, cls: "bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))]" }

  return (
    <span className={`inline-flex items-center rounded-[var(--radius)] px-1.5 py-0.5 text-[10px] font-medium ${cfg.cls}`}>
      {cfg.label}
    </span>
  )
}

// ---------------------------------------------------------------------------
// Skeleton rows
// ---------------------------------------------------------------------------

function SkeletonRows({ count = 8, cols = 3 }: { count?: number; cols?: number }) {
  return (
    <>
      {Array.from({ length: count }).map((_, ri) => (
        <tr key={ri}>
          {Array.from({ length: cols }).map((_, ci) => (
            <td key={ci} className="px-3 py-2">
              <div
                className="skeleton h-4"
                style={{ width: `${50 + ((ri * 3 + ci * 7) % 40)}%` }}
              />
            </td>
          ))}
        </tr>
      ))}
    </>
  )
}

// ---------------------------------------------------------------------------
// Card view item
// ---------------------------------------------------------------------------

function RecordCard({
  record,
  onClick,
}: {
  record: Record<string, unknown>
  onClick: () => void
}) {
  const name = String(record.name ?? record.description ?? record.id ?? "—")
  const status = record.status as string | undefined
  const code = record.code as string | undefined

  return (
    <button
      type="button"
      onClick={onClick}
      className="group flex flex-col gap-1.5 border border-[hsl(var(--border))] bg-[hsl(var(--card))] p-3 text-left transition-colors hover:border-[hsl(var(--ring))] hover:bg-[hsl(var(--secondary)/0.3)]"
      style={{ borderRadius: "var(--radius)" }}
    >
      <div className="flex items-start justify-between gap-2">
        <span className="text-[13px] font-medium text-[hsl(var(--foreground))] line-clamp-2">
          {name}
        </span>
        {status && <StatusBadge status={status} />}
      </div>
      {code && (
        <span className="text-[11px] text-[hsl(var(--muted-foreground))]">#{code}</span>
      )}
    </button>
  )
}

// ---------------------------------------------------------------------------
// List row item (compact)
// ---------------------------------------------------------------------------

function RecordListItem({
  record,
  onClick,
  isDocument,
}: {
  record: Record<string, unknown>
  onClick: () => void
  isDocument: boolean
}) {
  const name = String(record.name ?? record.description ?? record.id ?? "—")
  const status = record.status as string | undefined
  const code = record.code as string | undefined

  return (
    <button
      type="button"
      onClick={onClick}
      className={[
        "group flex w-full items-center gap-3 border-b border-[hsl(var(--border))] px-4 py-2 text-left text-[12px]",
        "hover:bg-[hsl(var(--secondary)/0.5)] transition-colors",
        isDocument && status === "posted" ? "status-strip status-strip--posted" : "",
        isDocument && status === "draft" ? "status-strip status-strip--draft" : "",
        isDocument && status === "cancelled" ? "status-strip status-strip--deleted" : "",
      ].join(" ")}
    >
      {code && (
        <span className="shrink-0 text-[11px] text-[hsl(var(--muted-foreground))] w-16 truncate">
          {code}
        </span>
      )}
      <span className="flex-1 font-medium text-[hsl(var(--foreground))] truncate">{name}</span>
      {status && <StatusBadge status={status} />}
    </button>
  )
}

// ---------------------------------------------------------------------------
// ResourceList page
// ---------------------------------------------------------------------------

export function ResourceList() {
  const { section = "", resource = "" } = useParams<{ section: string; resource: string }>()
  const navigate = useNavigate()
  const { addTab, updateTab } = useTabStore()
  const { tree, load } = useMetadata()

  useEffect(() => { load() }, [load])

  // Resolve metadata object info — resource param is now item.name
  const metaItems = tree
    ? (tree[section as keyof typeof tree] as MetaObjectRef[] | undefined) ?? []
    : []
  const metaItem = metaItems.find((m) => m.name === resource)

  // Register tab
  useEffect(() => {
    const title = metaItem?.synonym ?? metaItem?.name ?? resource
    const tabId = `${section}/${resource}`
    addTab({ id: tabId, title, path: `/${section}/${resource}`, icon: "list", closable: true })
    updateTab(tabId, { title })
  }, [section, resource, metaItem, addTab, updateTab])

  // UI state
  const [viewMode, setViewMode] = useState<ViewMode>("table")
  const [search, setSearch] = useState("")
  const [debouncedSearch, setDebouncedSearch] = useState("")
  const [page, setPage] = useState(1)
  const [pageSize, setPageSize] = useState(25)
  const [sortField, setSortField] = useState<string | null>(null)
  const [sortOrder, setSortOrder] = useState<SortOrder>(null)
  const searchRef = useRef<HTMLInputElement>(null)

  const isDocument = section === "documents"

  // Debounce search
  useEffect(() => {
    const t = setTimeout(() => setDebouncedSearch(search), 300)
    return () => clearTimeout(t)
  }, [search])

  // Reset page when search changes
  useEffect(() => { setPage(1) }, [debouncedSearch])

  const columns = deriveColumns(metaItem, isDocument)

  // Build Refine filters
  const filters = debouncedSearch
    ? [{ field: "name", operator: "contains" as const, value: debouncedSearch }]
    : []

  const sorters =
    sortField && sortOrder
      ? [{ field: sortField, order: sortOrder }]
      : []

  const fullResource = toApiResource(section, resource)

  const listResult = useList({
    resource: fullResource,
    pagination: { currentPage: page, pageSize },
    sorters,
    filters,
  })

  const isLoading = listResult.query?.isLoading ?? false
  const isError = listResult.query?.isError ?? false
  const records = (listResult.result?.data ?? []) as Record<string, unknown>[]
  const total = listResult.result?.total ?? 0
  const pageCount = Math.max(1, Math.ceil(total / pageSize))

  const handleRowClick = useCallback(
    (record: Record<string, unknown>) => {
      const id = String(record.uuid ?? record.id ?? record.guid ?? "")
      if (!id) return
      const path = `/${section}/${resource}/${id}`
      const title = String(record.description ?? record.name ?? record.number ?? id)
      addTab({ id: `${section}/${resource}/${id}`, title, path, icon: "file-text", closable: true })
      navigate(path)
    },
    [section, resource, navigate, addTab],
  )

  const handleCreate = useCallback(() => {
    const path = `/${section}/${resource}/create`
    addTab({
      id: `${section}/${resource}/create`,
      title: `New ${metaItem?.name ?? resource}`,
      path,
      icon: "file-text",
      closable: true,
    })
    navigate(path)
  }, [section, resource, metaItem, navigate, addTab])

  const handleSort = (key: string) => {
    if (sortField !== key) {
      setSortField(key)
      setSortOrder("asc")
    } else if (sortOrder === "asc") {
      setSortOrder("desc")
    } else {
      setSortField(null)
      setSortOrder(null)
    }
  }

  function SortIcon({ field }: { field: string }) {
    if (sortField !== field) return <ArrowsDownUp size={11} className="opacity-30" />
    return sortOrder === "asc"
      ? <ArrowUp size={11} className="text-[hsl(var(--primary))]" />
      : <ArrowDown size={11} className="text-[hsl(var(--primary))]" />
  }

  // ---------------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------------

  return (
    <div className="flex flex-col h-full bg-[hsl(var(--background))]">
      {/* ------------------------------------------------------------------ */}
      {/* Toolbar                                                              */}
      {/* ------------------------------------------------------------------ */}
      <div className="flex shrink-0 items-center gap-2 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.4)] px-4 py-2">
        {/* Create button */}
        <button
          type="button"
          onClick={handleCreate}
          className="flex items-center gap-1.5 h-8 px-3 rounded-[var(--radius)] border border-[hsl(var(--primary))] bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))] text-[12px] font-medium hover:opacity-90 transition-opacity"
        >
          <Plus size={14} weight="bold" />
          <span>Create</span>
        </button>

        {/* Search */}
        <div className="flex items-center gap-2 rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-3 py-1.5 text-[12px] flex-1 max-w-[320px]">
          <MagnifyingGlass size={13} className="shrink-0 text-[hsl(var(--muted-foreground))]" />
          <input
            ref={searchRef}
            type="text"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            placeholder={`Search ${metaItem?.name ?? resource}...`}
            className="flex-1 bg-transparent outline-none placeholder:text-[hsl(var(--muted-foreground))]"
          />
          {search && (
            <button
              type="button"
              onClick={() => setSearch("")}
              className="text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))] text-[11px]"
            >
              ×
            </button>
          )}
        </div>

        <div className="flex-1" />

        {/* Page size */}
        <select
          value={pageSize}
          onChange={(e) => { setPageSize(Number(e.target.value)); setPage(1) }}
          className="h-8 rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-2 text-[12px] text-[hsl(var(--foreground))] outline-none"
        >
          {[25, 50, 100].map((n) => (
            <option key={n} value={n}>{n} / page</option>
          ))}
        </select>

        {/* View mode toggle */}
        <div className="flex items-center rounded-[var(--radius)] border border-[hsl(var(--border))] overflow-hidden">
          {(["table", "list", "cards"] as ViewMode[]).map((mode) => {
            const Icon =
              mode === "table" ? Table :
              mode === "list"  ? Rows  :
                                 SquaresFour
            return (
              <button
                key={mode}
                type="button"
                title={mode.charAt(0).toUpperCase() + mode.slice(1) + " view"}
                onClick={() => setViewMode(mode)}
                className={[
                  "flex h-8 w-8 items-center justify-center transition-colors",
                  viewMode === mode
                    ? "bg-[hsl(var(--secondary))] text-[hsl(var(--foreground))]"
                    : "bg-[hsl(var(--background))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary)/0.5)]",
                ].join(" ")}
              >
                <Icon size={14} weight="duotone" />
              </button>
            )
          })}
        </div>
      </div>

      {/* ------------------------------------------------------------------ */}
      {/* Content                                                             */}
      {/* ------------------------------------------------------------------ */}
      <div className="flex-1 overflow-auto">
        {isError && (
          <div className="flex items-center justify-center h-full text-[hsl(var(--muted-foreground))] text-[13px]">
            Failed to load data. Check your connection.
          </div>
        )}

        {/* TABLE VIEW */}
        {viewMode === "table" && !isError && (
          <table className="w-full border-collapse text-[12px]">
            <thead className="sticky top-0 z-10 bg-[hsl(var(--secondary)/0.8)] backdrop-blur-sm">
              <tr>
                {columns.map((col) => (
                  <th
                    key={col.key}
                    onClick={() => handleSort(col.key)}
                    className="cursor-pointer select-none border-b border-[hsl(var(--border))] px-3 py-2 text-left font-semibold text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))] transition-colors"
                  >
                    <div className="flex items-center gap-1">
                      {col.label}
                      <SortIcon field={col.key} />
                    </div>
                  </th>
                ))}
                {isDocument && (
                  <th className="border-b border-[hsl(var(--border))] px-3 py-2 text-left font-semibold text-[hsl(var(--foreground))]">
                    Status
                  </th>
                )}
              </tr>
            </thead>
            <tbody>
              {isLoading ? (
                <SkeletonRows count={pageSize > 25 ? 12 : 8} cols={columns.length + (isDocument ? 1 : 0)} />
              ) : records.length === 0 ? (
                <tr>
                  <td
                    colSpan={columns.length + (isDocument ? 1 : 0)}
                    className="px-3 py-10 text-center text-[hsl(var(--muted-foreground))]"
                  >
                    {debouncedSearch ? "No results match your search." : "No records yet."}
                  </td>
                </tr>
              ) : (
                records.map((record, idx) => {
                  const status = record.status as string | undefined
                  const rowKey = String(record.id ?? record.guid ?? idx)
                  return (
                    <tr
                      key={rowKey}
                      onClick={() => handleRowClick(record)}
                      className={[
                        "cursor-pointer border-b border-[hsl(var(--border))] transition-colors",
                        "hover:bg-[hsl(var(--secondary)/0.5)]",
                        isDocument && status === "posted"    ? "status-strip status-strip--posted" : "",
                        isDocument && status === "draft"     ? "status-strip status-strip--draft" : "",
                        isDocument && status === "cancelled" ? "status-strip status-strip--deleted" : "",
                      ].join(" ")}
                    >
                      {columns.map((col) => (
                        <td key={col.key} className="px-3 py-2 text-[hsl(var(--foreground))]">
                          {String(record[col.key] ?? "")}
                        </td>
                      ))}
                      {isDocument && (
                        <td className="px-3 py-2">
                          <StatusBadge status={status} />
                        </td>
                      )}
                    </tr>
                  )
                })
              )}
            </tbody>
          </table>
        )}

        {/* LIST VIEW */}
        {viewMode === "list" && !isError && (
          <div className="divide-y divide-[hsl(var(--border))]">
            {isLoading ? (
              Array.from({ length: 8 }).map((_, i) => (
                <div key={i} className="flex items-center gap-3 px-4 py-2">
                  <div className="skeleton h-4 flex-1" />
                </div>
              ))
            ) : records.length === 0 ? (
              <div className="flex items-center justify-center py-16 text-[hsl(var(--muted-foreground))] text-[12px]">
                {debouncedSearch ? "No results match your search." : "No records yet."}
              </div>
            ) : (
              records.map((record, idx) => (
                <RecordListItem
                  key={String(record.id ?? idx)}
                  record={record}
                  onClick={() => handleRowClick(record)}
                  isDocument={isDocument}
                />
              ))
            )}
          </div>
        )}

        {/* CARDS VIEW */}
        {viewMode === "cards" && !isError && (
          <div className="p-4 grid grid-cols-2 gap-3 sm:grid-cols-3 lg:grid-cols-4 xl:grid-cols-5">
            {isLoading ? (
              Array.from({ length: 10 }).map((_, i) => (
                <div key={i} className="skeleton h-20" style={{ borderRadius: "var(--radius)" }} />
              ))
            ) : records.length === 0 ? (
              <div className="col-span-full flex items-center justify-center py-16 text-[hsl(var(--muted-foreground))] text-[12px]">
                {debouncedSearch ? "No results match your search." : "No records yet."}
              </div>
            ) : (
              records.map((record, idx) => (
                <RecordCard
                  key={String(record.id ?? idx)}
                  record={record}
                  onClick={() => handleRowClick(record)}
                />
              ))
            )}
          </div>
        )}
      </div>

      {/* ------------------------------------------------------------------ */}
      {/* Pagination footer                                                    */}
      {/* ------------------------------------------------------------------ */}
      <div className="flex shrink-0 items-center gap-3 border-t border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.3)] px-4 py-2 text-[11px] text-[hsl(var(--muted-foreground))]">
        <span>
          {total > 0
            ? `${(page - 1) * pageSize + 1}–${Math.min(page * pageSize, total)} of ${total}`
            : "0 records"}
        </span>
        <div className="flex-1" />
        <div className="flex items-center gap-1">
          <button
            type="button"
            disabled={page <= 1}
            onClick={() => setPage(page - 1)}
            className="flex h-7 w-7 items-center justify-center rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] hover:bg-[hsl(var(--secondary))] disabled:opacity-40 disabled:cursor-not-allowed transition-colors"
          >
            <CaretLeft size={12} weight="bold" />
          </button>

          {/* Page number pills — show up to 5 */}
          {Array.from({ length: Math.min(pageCount, 5) }, (_, i) => {
            const p = Math.max(1, Math.min(page - 2, pageCount - 4)) + i
            if (p < 1 || p > pageCount) return null
            return (
              <button
                key={p}
                type="button"
                onClick={() => setPage(p)}
                className={[
                  "flex h-7 w-7 items-center justify-center rounded-[var(--radius)] border text-[11px] transition-colors",
                  p === page
                    ? "border-[hsl(var(--primary))] bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))] font-medium"
                    : "border-[hsl(var(--border))] bg-[hsl(var(--background))] hover:bg-[hsl(var(--secondary))]",
                ].join(" ")}
              >
                {p}
              </button>
            )
          })}

          <button
            type="button"
            disabled={page >= pageCount}
            onClick={() => setPage(page + 1)}
            className="flex h-7 w-7 items-center justify-center rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] hover:bg-[hsl(var(--secondary))] disabled:opacity-40 disabled:cursor-not-allowed transition-colors"
          >
            <CaretRight size={12} weight="bold" />
          </button>
        </div>
      </div>
    </div>
  )
}
