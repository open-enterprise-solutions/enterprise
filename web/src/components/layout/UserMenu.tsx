import { useState, useRef, useEffect, useCallback } from "react"
import { useLogout, useGetIdentity } from "@refinedev/core"
import {
  User,
  SignOut,
  Moon,
  Sun,
  TextT,
  CaretDown,
} from "@phosphor-icons/react"
import { useUiStore } from "@/stores/ui-store"

interface Identity {
  id: string
  name?: string
  avatar?: string
}

export function UserMenu() {
  const [open, setOpen] = useState(false)
  const menuRef = useRef<HTMLDivElement>(null)
  const buttonRef = useRef<HTMLButtonElement>(null)

  const { darkMode, compactMode, toggleDark, toggleCompact } = useUiStore()
  const { mutate: logout } = useLogout()
  const { data: identity } = useGetIdentity<Identity>()

  const userName = identity?.name ?? "User"
  const initials = userName
    .split(" ")
    .map((w) => w[0])
    .slice(0, 2)
    .join("")
    .toUpperCase()

  const close = useCallback(() => setOpen(false), [])

  // Close on outside click or Escape
  useEffect(() => {
    if (!open) return

    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        close()
        buttonRef.current?.focus()
      }
    }
    const onOutside = (e: MouseEvent) => {
      if (menuRef.current && !menuRef.current.contains(e.target as Node)) {
        close()
      }
    }

    document.addEventListener("keydown", onKey)
    document.addEventListener("mousedown", onOutside)
    return () => {
      document.removeEventListener("keydown", onKey)
      document.removeEventListener("mousedown", onOutside)
    }
  }, [open, close])

  return (
    <div ref={menuRef} className="relative">
      <button
        ref={buttonRef}
        type="button"
        aria-haspopup="menu"
        aria-expanded={open}
        onClick={() => setOpen((v) => !v)}
        className="flex items-center gap-1.5 rounded-[var(--radius)] px-2 py-1 hover:bg-[hsl(var(--secondary))] transition-colors"
      >
        {/* Avatar circle */}
        <div className="flex h-7 w-7 items-center justify-center rounded-full bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))] text-[11px] font-semibold select-none">
          {initials || <User size={14} weight="duotone" />}
        </div>
        <span className="hidden sm:block text-[12px] font-medium text-[hsl(var(--foreground))] max-w-[100px] truncate">
          {userName}
        </span>
        <CaretDown
          size={11}
          weight="bold"
          className={`text-[hsl(var(--muted-foreground))] transition-transform ${open ? "rotate-180" : ""}`}
        />
      </button>

      {open && (
        <div
          role="menu"
          className={[
            "absolute right-0 top-full mt-1 z-50 min-w-[180px]",
            "border border-[hsl(var(--border))] bg-[hsl(var(--popover))]",
            "shadow-md py-1",
          ].join(" ")}
          style={{ borderRadius: "var(--radius)" }}
        >
          {/* User info header */}
          <div className="border-b border-[hsl(var(--border))] px-3 py-2.5">
            <p className="text-[12px] font-semibold text-[hsl(var(--foreground))] truncate">{userName}</p>
            <p className="text-[11px] text-[hsl(var(--muted-foreground))]">Signed in</p>
          </div>

          {/* Toggle: Dark mode */}
          <MenuButton
            icon={darkMode ? <Sun size={14} weight="duotone" /> : <Moon size={14} weight="duotone" />}
            label={darkMode ? "Light Mode" : "Dark Mode"}
            onClick={() => { toggleDark(); close() }}
            trailing={
              <span className={`text-[10px] px-1.5 py-0.5 rounded ${darkMode ? "bg-[hsl(var(--primary)/0.15)] text-[hsl(var(--primary))]" : "bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))]"}`}>
                {darkMode ? "ON" : "OFF"}
              </span>
            }
          />

          {/* Toggle: Density */}
          <MenuButton
            icon={<TextT size={14} weight="duotone" />}
            label={compactMode ? "Standard Mode" : "Compact Mode"}
            onClick={() => { toggleCompact(); close() }}
            trailing={
              <span className="text-[10px] px-1.5 py-0.5 rounded bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))]">
                {compactMode ? "COMPACT" : "STD"}
              </span>
            }
          />

          {/* Divider */}
          <div className="my-1 border-t border-[hsl(var(--border))]" />

          {/* Logout */}
          <MenuButton
            icon={<SignOut size={14} weight="duotone" />}
            label="Sign Out"
            onClick={() => { logout(); close() }}
            danger
          />
        </div>
      )}
    </div>
  )
}

interface MenuButtonProps {
  icon: React.ReactNode
  label: string
  onClick: () => void
  trailing?: React.ReactNode
  danger?: boolean
}

function MenuButton({ icon, label, onClick, trailing, danger }: MenuButtonProps) {
  return (
    <button
      type="button"
      role="menuitem"
      onClick={onClick}
      className={[
        "flex w-full items-center gap-2.5 px-3 py-2 text-[12px] transition-colors",
        danger
          ? "text-[hsl(var(--destructive))] hover:bg-[hsl(var(--destructive)/0.08)]"
          : "text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))]",
      ].join(" ")}
    >
      <span className="shrink-0">{icon}</span>
      <span className="flex-1 text-left">{label}</span>
      {trailing}
    </button>
  )
}
