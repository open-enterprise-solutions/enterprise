/**
 * Pagination — compact ERP-style pager.
 *
 * Features:
 *   - Previous / Next buttons (keyboard: Alt+Left, Alt+Right)
 *   - Page number buttons with ellipsis for large page counts
 *   - Page size selector (10 / 25 / 50 / 100)
 *   - "Showing X–Y of Z" summary
 *
 * Keyboard:
 *   Alt+Left  — previous page
 *   Alt+Right — next page
 *
 * Accessibility:
 *   - nav role with aria-label="Pagination"
 *   - aria-current="page" on active page button
 *   - aria-label on prev/next buttons
 */

import { useEffect, useCallback } from "react"
import { CaretLeft, CaretRight } from "@phosphor-icons/react"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface PaginationProps {
  /** Current 1-based page number */
  page: number
  /** Total number of pages */
  totalPages: number
  /** Total number of records (for "Showing X–Y of Z") */
  totalRecords: number
  /** Number of records per page */
  pageSize: number
  onPageChange: (page: number) => void
  onPageSizeChange: (size: number) => void
  pageSizeOptions?: number[]
  /** Hide when there is only one page */
  hideOnSinglePage?: boolean
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const DEFAULT_PAGE_SIZE_OPTIONS = [10, 25, 50, 100]

function cn(...parts: (string | false | null | undefined)[]): string {
  return parts.filter(Boolean).join(" ")
}

/**
 * Build the list of page items to render.
 * Returns numbers for actual pages and null for ellipsis.
 */
function buildPageItems(current: number, total: number): (number | null)[] {
  if (total <= 7) {
    return Array.from({ length: total }, (_, i) => i + 1)
  }

  const items: (number | null)[] = []
  const WING = 1 // pages shown around current page on each side

  items.push(1)

  if (current - WING > 2) {
    items.push(null) // left ellipsis
  }

  const rangeStart = Math.max(2, current - WING)
  const rangeEnd = Math.min(total - 1, current + WING)

  for (let p = rangeStart; p <= rangeEnd; p++) {
    items.push(p)
  }

  if (current + WING < total - 1) {
    items.push(null) // right ellipsis
  }

  items.push(total)

  return items
}

// ---------------------------------------------------------------------------
// PageButton
// ---------------------------------------------------------------------------

function PageButton({
  page,
  active,
  onClick,
}: {
  page: number
  active: boolean
  onClick: () => void
}) {
  return (
    <button
      type="button"
      aria-label={`Page ${page}`}
      aria-current={active ? "page" : undefined}
      onClick={onClick}
      className={cn(
        "h-7 min-w-[28px] px-1.5 rounded-[var(--radius)] text-[12px] tabular-nums transition-colors border",
        active
          ? "bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))] border-[hsl(var(--primary))] font-medium"
          : "bg-[hsl(var(--background))] text-[hsl(var(--foreground))] border-[hsl(var(--border))] hover:bg-[hsl(var(--secondary))]",
      )}
    >
      {page}
    </button>
  )
}

// ---------------------------------------------------------------------------
// Ellipsis
// ---------------------------------------------------------------------------

function Ellipsis() {
  return (
    <span
      aria-hidden="true"
      className="h-7 flex items-end pb-0.5 px-0.5 text-[12px] text-[hsl(var(--muted-foreground))] select-none"
    >
      &hellip;
    </span>
  )
}

// ---------------------------------------------------------------------------
// Pagination
// ---------------------------------------------------------------------------

export function Pagination({
  page,
  totalPages,
  totalRecords,
  pageSize,
  onPageChange,
  onPageSizeChange,
  pageSizeOptions = DEFAULT_PAGE_SIZE_OPTIONS,
  hideOnSinglePage = false,
}: PaginationProps) {
  if (hideOnSinglePage && totalPages <= 1) return null

  const from = totalRecords === 0 ? 0 : (page - 1) * pageSize + 1
  const to = Math.min(page * pageSize, totalRecords)

  const goTo = useCallback(
    (p: number) => {
      const clamped = Math.max(1, Math.min(p, totalPages))
      if (clamped !== page) onPageChange(clamped)
    },
    [page, totalPages, onPageChange],
  )

  // Keyboard shortcuts: Alt+Left / Alt+Right
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (e.altKey && e.key === "ArrowLeft") {
        e.preventDefault()
        goTo(page - 1)
      } else if (e.altKey && e.key === "ArrowRight") {
        e.preventDefault()
        goTo(page + 1)
      }
    }
    window.addEventListener("keydown", handler)
    return () => window.removeEventListener("keydown", handler)
  }, [page, goTo])

  const pageItems = buildPageItems(page, totalPages)

  return (
    <nav
      role="navigation"
      aria-label="Pagination"
      className="flex items-center gap-3 px-3 py-2 border-t border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.3)] shrink-0 flex-wrap"
    >
      {/* Record range summary */}
      <span className="text-[12px] text-[hsl(var(--muted-foreground))] tabular-nums whitespace-nowrap mr-auto">
        {totalRecords === 0
          ? "No records"
          : `Showing ${from}–${to} of ${totalRecords}`}
      </span>

      {/* Previous */}
      <button
        type="button"
        aria-label="Previous page"
        disabled={page <= 1}
        onClick={() => goTo(page - 1)}
        className={cn(
          "flex items-center justify-center h-7 w-7 rounded-[var(--radius)] border border-[hsl(var(--border))] text-[12px] transition-colors",
          page <= 1
            ? "opacity-40 cursor-not-allowed bg-[hsl(var(--background))]"
            : "bg-[hsl(var(--background))] hover:bg-[hsl(var(--secondary))] text-[hsl(var(--foreground))]",
        )}
      >
        <CaretLeft size={13} weight="bold" />
      </button>

      {/* Page buttons */}
      <div className="flex items-center gap-1" role="list">
        {pageItems.map((item, i) =>
          item === null ? (
            <Ellipsis key={`ell-${i}`} />
          ) : (
            <PageButton
              key={item}
              page={item}
              active={item === page}
              onClick={() => goTo(item)}
            />
          ),
        )}
      </div>

      {/* Next */}
      <button
        type="button"
        aria-label="Next page"
        disabled={page >= totalPages}
        onClick={() => goTo(page + 1)}
        className={cn(
          "flex items-center justify-center h-7 w-7 rounded-[var(--radius)] border border-[hsl(var(--border))] text-[12px] transition-colors",
          page >= totalPages
            ? "opacity-40 cursor-not-allowed bg-[hsl(var(--background))]"
            : "bg-[hsl(var(--background))] hover:bg-[hsl(var(--secondary))] text-[hsl(var(--foreground))]",
        )}
      >
        <CaretRight size={13} weight="bold" />
      </button>

      {/* Page size selector */}
      <select
        aria-label="Records per page"
        value={pageSize}
        onChange={(e) => {
          onPageSizeChange(Number(e.target.value))
          onPageChange(1) // reset to first page on size change
        }}
        className="h-7 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] px-1.5 text-[12px] text-[hsl(var(--foreground))] outline-none focus:border-[hsl(var(--ring))] focus:ring-1 focus:ring-[hsl(var(--ring))] cursor-pointer"
      >
        {pageSizeOptions.map((size) => (
          <option key={size} value={size}>
            {size} / page
          </option>
        ))}
      </select>
    </nav>
  )
}
