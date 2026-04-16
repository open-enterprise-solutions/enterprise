import { create } from "zustand"
import { CheckCircle, XCircle, Info, X } from "@phosphor-icons/react"

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

export type ToastType = "success" | "error" | "info"

export interface ToastItem {
  id: string
  type: ToastType
  message: string
}

interface ToastStore {
  toasts: ToastItem[]
  push: (type: ToastType, message: string) => void
  remove: (id: string) => void
}

export const useToastStore = create<ToastStore>((set, get) => ({
  toasts: [],

  push: (type, message) => {
    const id = `${Date.now()}-${Math.random().toString(36).slice(2)}`
    const toasts = get().toasts.slice(-2)
    set({ toasts: [...toasts, { id, type, message }] })

    setTimeout(() => {
      set((s) => ({ toasts: s.toasts.filter((t) => t.id !== id) }))
    }, 3000)
  },

  remove: (id) =>
    set((s) => ({ toasts: s.toasts.filter((t) => t.id !== id) })),
}))

// Convenience helpers
export const toast = {
  success: (msg: string) => useToastStore.getState().push("success", msg),
  error:   (msg: string) => useToastStore.getState().push("error", msg),
  info:    (msg: string) => useToastStore.getState().push("info", msg),
}

// ---------------------------------------------------------------------------
// Single toast item
// ---------------------------------------------------------------------------

function ToastEntry({ item }: { item: ToastItem }) {
  const remove = useToastStore((s) => s.remove)

  const iconMap = {
    success: <CheckCircle size={16} weight="duotone" className="text-[hsl(var(--status-posted))] shrink-0 mt-0.5" />,
    error:   <XCircle     size={16} weight="duotone" className="text-[hsl(var(--destructive))]   shrink-0 mt-0.5" />,
    info:    <Info        size={16} weight="duotone" className="text-[hsl(var(--primary))]       shrink-0 mt-0.5" />,
  }

  const borderMap = {
    success: "border-l-[hsl(var(--status-posted))]",
    error:   "border-l-[hsl(var(--destructive))]",
    info:    "border-l-[hsl(var(--primary))]",
  }

  return (
    <div
      className={[
        "flex items-start gap-2.5 px-3 py-2.5",
        "bg-[hsl(var(--card))] border border-[hsl(var(--border))] border-l-2",
        borderMap[item.type],
        "shadow-[0_2px_8px_rgba(0,0,0,0.12)]",
        "min-w-[240px] max-w-[340px]",
        "animate-[toast-in_150ms_ease-out]",
      ].join(" ")}
    >
      {iconMap[item.type]}
      <span className="flex-1 text-[12px] text-[hsl(var(--foreground))] leading-[1.4]">
        {item.message}
      </span>
      <button
        type="button"
        onClick={() => remove(item.id)}
        className="shrink-0 text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))] transition-colors"
        aria-label="Dismiss"
      >
        <X size={12} weight="bold" />
      </button>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Toast container — render at app root
// ---------------------------------------------------------------------------

export function ToastContainer() {
  const toasts = useToastStore((s) => s.toasts)

  if (toasts.length === 0) return null

  return (
    <div
      className="fixed bottom-6 right-4 z-50 flex flex-col gap-2 pointer-events-none"
      aria-live="polite"
      aria-label="Notifications"
    >
      {toasts.map((t) => (
        <div key={t.id} className="pointer-events-auto">
          <ToastEntry item={t} />
        </div>
      ))}
    </div>
  )
}
