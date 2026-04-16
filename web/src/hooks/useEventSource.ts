////////////////////////////////////////////////////////////////////////////
// useEventSource — SSE hook for OES real-time notifications
//
// Connects to GET /api/events?token=<jwt>.
// Dispatches incoming events to the notification store and invalidates
// Refine query-cache entries on "data_changed" events.
//
// Auto-reconnects on drop with exponential backoff (1 s → 2 s → 4 s …
// capped at 30 s). Backoff resets after a successful 10-second window.
////////////////////////////////////////////////////////////////////////////

import { useEffect, useRef, useCallback, useState } from "react"
import { useInvalidate } from "@refinedev/core"
import { useNotificationStore } from "@/stores/notification-store"

// Token is always stored in localStorage by oes-auth-provider
function getToken(): string | null {
  return localStorage.getItem("oes_token")
}

const BASE_URL = (import.meta.env.VITE_API_BASE_URL as string | undefined) ?? "/api"
const SSE_ENDPOINT = `${BASE_URL}/events`

// Backoff config
const MIN_BACKOFF_MS  = 1_000
const MAX_BACKOFF_MS  = 30_000
const STABLE_WINDOW_MS = 10_000 // reset backoff after this long connected

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface SseDataChangedPayload {
  resource: string
  id: string
  action: "create" | "update" | "delete"
}

export interface SseNotificationPayload {
  message: string
  level: "info" | "success" | "warning" | "error"
}

export interface LastSseEvent {
  type: string
  payload: unknown
  at: number // Date.now()
}

// ---------------------------------------------------------------------------
// Hook
// ---------------------------------------------------------------------------

export function useEventSource() {
  const [isConnected, setIsConnected] = useState(false)
  const [lastEvent,   setLastEvent]   = useState<LastSseEvent | null>(null)

  const invalidate   = useInvalidate()
  const addNotification = useNotificationStore((s) => s.add)

  // Refs so callbacks never become stale closures
  const esRef        = useRef<EventSource | null>(null)
  const backoffMs    = useRef(MIN_BACKOFF_MS)
  const connectTimer = useRef<ReturnType<typeof setTimeout> | null>(null)
  const stableTimer  = useRef<ReturnType<typeof setTimeout> | null>(null)
  const unmounted    = useRef(false)

  const clearTimers = useCallback(() => {
    if (connectTimer.current !== null) {
      clearTimeout(connectTimer.current)
      connectTimer.current = null
    }
    if (stableTimer.current !== null) {
      clearTimeout(stableTimer.current)
      stableTimer.current = null
    }
  }, [])

  const disconnect = useCallback(() => {
    clearTimers()
    if (esRef.current !== null) {
      esRef.current.close()
      esRef.current = null
    }
    setIsConnected(false)
  }, [clearTimers])

  const connect = useCallback(() => {
    if (unmounted.current) return

    const token = getToken()
    if (!token) {
      // Not authenticated — do not connect
      return
    }

    // Close any stale connection first
    if (esRef.current !== null) {
      esRef.current.close()
      esRef.current = null
    }

    const url = `${SSE_ENDPOINT}?token=${encodeURIComponent(token)}`
    const es   = new EventSource(url)
    esRef.current = es

    // ---- Connection opened ------------------------------------------------
    es.onopen = () => {
      if (unmounted.current) { es.close(); return }
      setIsConnected(true)

      // Start stable window — if we stay connected this long, reset backoff
      stableTimer.current = setTimeout(() => {
        backoffMs.current = MIN_BACKOFF_MS
      }, STABLE_WINDOW_MS)
    }

    // ---- data_changed event -----------------------------------------------
    es.addEventListener("data_changed", (ev) => {
      if (unmounted.current) return

      let payload: SseDataChangedPayload | null = null
      try { payload = JSON.parse(ev.data) as SseDataChangedPayload }
      catch { return }

      setLastEvent({ type: "data_changed", payload, at: Date.now() })

      // Invalidate the matching Refine resource list + single record
      if (payload.resource) {
        // resource format from backend: "catalog.products" → Refine uses "catalogs/products"
        // Both are accepted by invalidate() via the name field
        invalidate({ resource: payload.resource, invalidates: ["list"] })
        if (payload.id) {
          invalidate({
            resource: payload.resource,
            id: payload.id,
            invalidates: ["detail"],
          })
        }
      }
    })

    // ---- notification event -----------------------------------------------
    es.addEventListener("notification", (ev) => {
      if (unmounted.current) return

      let payload: SseNotificationPayload | null = null
      try { payload = JSON.parse(ev.data) as SseNotificationPayload }
      catch { return }

      setLastEvent({ type: "notification", payload, at: Date.now() })
      addNotification({
        message: payload.message,
        level:   payload.level ?? "info",
      })
    })

    // ---- heartbeat event --------------------------------------------------
    es.addEventListener("heartbeat", () => {
      // No-op; connection is alive
    })

    // ---- Error / reconnect ------------------------------------------------
    es.onerror = () => {
      if (unmounted.current) return

      es.close()
      esRef.current = null
      setIsConnected(false)
      clearTimers()

      // Schedule reconnect with backoff
      const delay = backoffMs.current
      backoffMs.current = Math.min(backoffMs.current * 2, MAX_BACKOFF_MS)

      connectTimer.current = setTimeout(() => {
        connectTimer.current = null
        connect()
      }, delay)
    }
  }, [invalidate, addNotification, clearTimers])

  useEffect(() => {
    unmounted.current = false
    connect()

    return () => {
      unmounted.current = true
      disconnect()
    }
  }, [connect, disconnect])

  return { isConnected, lastEvent }
}
