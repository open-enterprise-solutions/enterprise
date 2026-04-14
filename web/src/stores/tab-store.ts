import { create } from "zustand"

export interface Tab {
  id: string
  title: string
  path: string
  icon?: string
  closable: boolean
}

interface TabStore {
  tabs: Tab[]
  activeTabId: string | null
  addTab: (tab: Tab) => void
  removeTab: (id: string) => void
  setActive: (id: string) => void
  updateTab: (id: string, partial: Partial<Tab>) => void
}

export const useTabStore = create<TabStore>((set, get) => ({
  tabs: [],
  activeTabId: null,

  addTab: (tab: Tab) => {
    const { tabs } = get()
    const existing = tabs.find((t) => t.id === tab.id)
    if (existing) {
      // Just activate the existing tab
      set({ activeTabId: tab.id })
      return
    }
    set({ tabs: [...tabs, tab], activeTabId: tab.id })
  },

  removeTab: (id: string) => {
    const { tabs, activeTabId } = get()
    const idx = tabs.findIndex((t) => t.id === id)
    if (idx === -1) return

    const next = tabs.filter((t) => t.id !== id)

    let nextActive = activeTabId
    if (activeTabId === id) {
      // Activate adjacent tab: prefer right, fallback left, fallback null
      const adjacent = next[idx] ?? next[idx - 1] ?? null
      nextActive = adjacent?.id ?? null
    }

    set({ tabs: next, activeTabId: nextActive })
  },

  setActive: (id: string) => set({ activeTabId: id }),

  updateTab: (id: string, partial: Partial<Tab>) =>
    set((s) => ({
      tabs: s.tabs.map((t) => (t.id === id ? { ...t, ...partial } : t)),
    })),
}))
