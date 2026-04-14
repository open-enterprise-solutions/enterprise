import { create } from "zustand"
import { persist } from "zustand/middleware"

interface UiStore {
  sidebarCollapsed: boolean
  darkMode: boolean
  compactMode: boolean
  toggleSidebar: () => void
  toggleDark: () => void
  toggleCompact: () => void
  setSidebarCollapsed: (v: boolean) => void
}

function applyDark(dark: boolean) {
  if (dark) {
    document.documentElement.classList.add("dark")
  } else {
    document.documentElement.classList.remove("dark")
  }
}

function applyCompact(compact: boolean) {
  if (compact) {
    document.documentElement.classList.add("density-compact")
    document.documentElement.classList.remove("density-standard")
  } else {
    document.documentElement.classList.remove("density-compact")
    document.documentElement.classList.add("density-standard")
  }
}

export const useUiStore = create<UiStore>()(
  persist(
    (set, get) => ({
      sidebarCollapsed: false,
      darkMode: false,
      compactMode: true,

      toggleSidebar: () =>
        set((s) => ({ sidebarCollapsed: !s.sidebarCollapsed })),

      toggleDark: () => {
        const next = !get().darkMode
        applyDark(next)
        set({ darkMode: next })
      },

      toggleCompact: () => {
        const next = !get().compactMode
        applyCompact(next)
        set({ compactMode: next })
      },

      setSidebarCollapsed: (v: boolean) => set({ sidebarCollapsed: v }),
    }),
    {
      name: "oes-ui",
      onRehydrateStorage: () => (state) => {
        if (state) {
          applyDark(state.darkMode)
          applyCompact(state.compactMode)
        }
      },
    },
  ),
)
