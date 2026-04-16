/**
 * SearchInput — debounced search field for DataView toolbars.
 *
 * Features:
 *   - MagnifyingGlass icon (Phosphor duotone)
 *   - Debounced onChange (default 300 ms)
 *   - Clear button (X) when field has content
 *   - Ctrl+F focuses this input from anywhere on the page
 *   - Escape clears + blurs
 *
 * Accessibility:
 *   - role="searchbox"
 *   - aria-label (configurable)
 *   - aria-busy while debounce is pending
 *
 * Design notes:
 *   - Matches the base input style from oes-formily-components (h-8, border,
 *     var(--radius), IBM Plex Sans 13px, compact density)
 *   - Spinner dot when pending (subtle — avoids layout shift)
 */

import { useState, useRef, useEffect, useCallback, type KeyboardEvent } from "react"
import { MagnifyingGlass, X } from "@phosphor-icons/react"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface SearchInputProps {
  /** Called with the debounced search string */
  onSearch: (value: string) => void
  /** Initial / controlled value (optional — component manages its own state) */
  value?: string
  placeholder?: string
  /** Debounce delay in milliseconds (default 300) */
  debounceMs?: number
  /** aria-label for the input */
  label?: string
  /** Whether the parent is still fetching */
  loading?: boolean
  className?: string
}

// ---------------------------------------------------------------------------
// SearchInput
// ---------------------------------------------------------------------------

export function SearchInput({
  onSearch,
  value: controlledValue,
  placeholder = "Search...",
  debounceMs = 300,
  label = "Search",
  loading = false,
  className,
}: SearchInputProps) {
  const [text, setText] = useState<string>(controlledValue ?? "")
  const [isPending, setIsPending] = useState(false)
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const inputRef = useRef<HTMLInputElement>(null)

  // Sync if controlled value changes externally
  useEffect(() => {
    if (controlledValue !== undefined) setText(controlledValue)
  }, [controlledValue])

  const commit = useCallback(
    (val: string) => {
      if (debounceRef.current) clearTimeout(debounceRef.current)
      setIsPending(true)
      debounceRef.current = setTimeout(() => {
        onSearch(val)
        setIsPending(false)
      }, debounceMs)
    },
    [onSearch, debounceMs],
  )

  const handleChange = (val: string) => {
    setText(val)
    commit(val)
  }

  const clear = useCallback(() => {
    if (debounceRef.current) clearTimeout(debounceRef.current)
    setText("")
    setIsPending(false)
    onSearch("")
    inputRef.current?.focus()
  }, [onSearch])

  const handleKeyDown = (e: KeyboardEvent<HTMLInputElement>) => {
    if (e.key === "Escape") {
      e.preventDefault()
      if (text) {
        clear()
      } else {
        inputRef.current?.blur()
      }
    }
  }

  // Ctrl+F (or Cmd+F on mac) focuses this input
  useEffect(() => {
    const handler = (e: globalThis.KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key === "f") {
        e.preventDefault()
        inputRef.current?.focus()
        inputRef.current?.select()
      }
    }
    window.addEventListener("keydown", handler)
    return () => window.removeEventListener("keydown", handler)
  }, [])

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      if (debounceRef.current) clearTimeout(debounceRef.current)
    }
  }, [])

  const showSpinner = isPending || loading
  const showClear = text.length > 0 && !showSpinner

  return (
    <div
      className={[
        "relative flex items-center rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))]",
        "focus-within:border-[hsl(var(--ring))] focus-within:ring-1 focus-within:ring-[hsl(var(--ring))]",
        "transition-colors",
        className,
      ]
        .filter(Boolean)
        .join(" ")}
    >
      {/* Leading icon */}
      <MagnifyingGlass
        size={15}
        weight="duotone"
        aria-hidden="true"
        className="shrink-0 ml-2 text-[hsl(var(--muted-foreground))]"
      />

      {/* Text input */}
      <input
        ref={inputRef}
        type="search"
        role="searchbox"
        aria-label={label}
        aria-busy={showSpinner}
        placeholder={placeholder}
        value={text}
        autoComplete="off"
        spellCheck={false}
        className="flex-1 h-8 min-w-0 bg-transparent px-2 text-[13px] text-[hsl(var(--foreground))] placeholder:text-[hsl(var(--muted-foreground))] outline-none [&::-webkit-search-cancel-button]:hidden"
        onChange={(e) => handleChange(e.target.value)}
        onKeyDown={handleKeyDown}
      />

      {/* Trailing: spinner, clear button, or Ctrl+F hint */}
      <div className="flex items-center pr-2 gap-1 shrink-0">
        {showSpinner && (
          <span
            aria-hidden="true"
            className="block h-3.5 w-3.5 rounded-full border-2 border-[hsl(var(--muted-foreground)/0.4)] border-t-[hsl(var(--primary))] animate-spin"
          />
        )}

        {showClear && (
          <button
            type="button"
            aria-label="Clear search"
            onClick={clear}
            className="flex items-center justify-center h-5 w-5 rounded-[var(--radius)] text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))] transition-colors"
          >
            <X size={12} weight="bold" />
          </button>
        )}

        {!showSpinner && !showClear && (
          <kbd
            aria-hidden="true"
            className="hidden sm:inline text-[10px] text-[hsl(var(--muted-foreground)/0.6)] leading-none border border-[hsl(var(--border))] rounded px-1 py-0.5 font-sans"
          >
            Ctrl+F
          </kbd>
        )}
      </div>
    </div>
  )
}
