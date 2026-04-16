////////////////////////////////////////////////////////////////////////////
// EmbedLayout — minimal shell for iframe / embedded contexts
//
// Activated when:
//   - Route starts with /embed/...
//   - OR ?embed=true is present on any URL
//
// Omits: sidebar, tab bar, top toolbar, user menu.
// Keeps:  content area only (scrollable, full viewport).
////////////////////////////////////////////////////////////////////////////

import type { ReactNode } from "react"

interface EmbedLayoutProps {
  children?: ReactNode
}

export function EmbedLayout({ children }: EmbedLayoutProps) {
  return (
    <div className="flex h-screen w-full overflow-hidden bg-[hsl(var(--background))]">
      {/* Minimal command bar — just a thin border at top */}
      <div className="flex flex-col flex-1 overflow-hidden">
        <div className="flex h-8 shrink-0 items-center border-b border-[hsl(var(--border))] px-3 gap-2">
          <span className="text-[11px] font-semibold text-[hsl(var(--muted-foreground))] select-none tracking-wide">
            OES
          </span>
          {/* Embed badge */}
          <span className="rounded-[var(--radius)] bg-[hsl(var(--secondary))] px-1.5 py-0.5 text-[10px] text-[hsl(var(--muted-foreground))]">
            embed
          </span>
        </div>

        {/* Scrollable content */}
        <main className="flex-1 overflow-auto">
          {children}
        </main>
      </div>
    </div>
  )
}
