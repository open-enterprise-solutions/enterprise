////////////////////////////////////////////////////////////////////////////
// notification-store — in-memory notification queue fed by SSE events
////////////////////////////////////////////////////////////////////////////

import { create } from "zustand"

export interface OesNotification {
  id:      string
  message: string
  level:   "info" | "success" | "warning" | "error"
  at:      number // Date.now()
  read:    boolean
}

interface NotificationStore {
  items: OesNotification[]

  add:       (n: Pick<OesNotification, "message" | "level">) => void
  markRead:  (id: string) => void
  markAllRead: () => void
  dismiss:   (id: string) => void
  clear:     () => void

  unreadCount: () => number
}

let counter = 0

export const useNotificationStore = create<NotificationStore>((set, get) => ({
  items: [],

  add: ({ message, level }) => {
    const item: OesNotification = {
      id:      `notif-${++counter}`,
      message,
      level,
      at:      Date.now(),
      read:    false,
    }
    set((s) => ({
      // Keep latest 100 notifications
      items: [item, ...s.items].slice(0, 100),
    }))
  },

  markRead: (id) =>
    set((s) => ({
      items: s.items.map((n) => (n.id === id ? { ...n, read: true } : n)),
    })),

  markAllRead: () =>
    set((s) => ({
      items: s.items.map((n) => ({ ...n, read: true })),
    })),

  dismiss: (id) =>
    set((s) => ({ items: s.items.filter((n) => n.id !== id) })),

  clear: () => set({ items: [] }),

  unreadCount: () => get().items.filter((n) => !n.read).length,
}))
