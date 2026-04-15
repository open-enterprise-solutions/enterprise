/**
 * useFormSession — manages the lifecycle of a server-side form session.
 *
 * Flow:
 *   1. openForm() → POST /api/session/open-form → sessionId + layout JSON
 *   2. sendEvent() → POST /api/session/event   → property updates
 *   3. closeForm() → POST /api/session/close   → cleanup
 *
 * The layout is a recursive control tree matching ibWebControlProxy on the server.
 */

import { useState, useCallback, useRef, useEffect } from "react"
import { api } from "@/lib/axios"

// ---------------------------------------------------------------------------
// Types — must match C++ ibWebVisualHost JSON output
// ---------------------------------------------------------------------------

export interface ControlNode {
  id: number
  type: string
  name?: string
  props?: Record<string, string>
  children?: ControlNode[]
}

export interface ControlUpdate {
  id: number
  props: Record<string, string>
}

export interface FormSessionState {
  sessionId: string | null
  layout: ControlNode | null
  loading: boolean
  error: string | null
}

interface OpenFormResponse {
  sessionId: string
  layout?: ControlNode
  error?: { code: string; message: string }
}

interface EventResponse {
  handled: boolean
  updates?: ControlUpdate[]
  error?: string
}

// ---------------------------------------------------------------------------
// Hook
// ---------------------------------------------------------------------------

export function useFormSession() {
  const [state, setState] = useState<FormSessionState>({
    sessionId: null,
    layout: null,
    loading: false,
    error: null,
  })

  const sessionRef = useRef<string | null>(null)

  // Apply property updates to the layout tree in place
  const applyUpdates = useCallback((updates: ControlUpdate[]) => {
    setState((prev) => {
      if (!prev.layout) return prev

      const updateMap = new Map(updates.map((u) => [u.id, u.props]))
      const patched = patchTree(prev.layout, updateMap)
      return { ...prev, layout: patched }
    })
  }, [])

  // Open a form session
  const openForm = useCallback(
    async (metaType: string, metaName: string, objectId?: string) => {
      setState({ sessionId: null, layout: null, loading: true, error: null })

      try {
        const res = await api.post<OpenFormResponse>("/session/open-form", {
          metaType,
          metaName,
          objectId: objectId ?? "",
        })

        if (res.data.error) {
          setState({
            sessionId: null,
            layout: null,
            loading: false,
            error: res.data.error.message,
          })
          return
        }

        sessionRef.current = res.data.sessionId
        setState({
          sessionId: res.data.sessionId,
          layout: res.data.layout ?? null,
          loading: false,
          error: null,
        })
      } catch (err) {
        const msg = err instanceof Error ? err.message : "Failed to open form"
        setState({ sessionId: null, layout: null, loading: false, error: msg })
      }
    },
    [],
  )

  // Send an event to the server
  const sendEvent = useCallback(
    async (controlId: number, event: string, data?: Record<string, unknown>) => {
      const sid = sessionRef.current
      if (!sid) return

      try {
        const res = await api.post<EventResponse>("/session/event", {
          sessionId: sid,
          controlId,
          event,
          data: data ?? {},
        })

        if (res.data.updates && res.data.updates.length > 0) {
          applyUpdates(res.data.updates)
        }
      } catch {
        // Event errors are non-fatal — server logs them
      }
    },
    [applyUpdates],
  )

  // Close the session
  const closeForm = useCallback(async () => {
    const sid = sessionRef.current
    if (!sid) return

    sessionRef.current = null
    setState({ sessionId: null, layout: null, loading: false, error: null })

    try {
      await api.post("/session/close", { sessionId: sid })
    } catch {
      // Ignore close errors — server will clean up on timeout
    }
  }, [])

  // Clean up on unmount
  useEffect(() => {
    return () => {
      if (sessionRef.current) {
        api.post("/session/close", { sessionId: sessionRef.current }).catch(() => {})
      }
    }
  }, [])

  return {
    ...state,
    openForm,
    sendEvent,
    closeForm,
  }
}

// ---------------------------------------------------------------------------
// Tree patching — immutable update of changed nodes
// ---------------------------------------------------------------------------

function patchTree(
  node: ControlNode,
  updateMap: Map<number, Record<string, string>>,
): ControlNode {
  const newProps = updateMap.get(node.id)
  const patchedChildren = node.children?.map((c) => patchTree(c, updateMap))

  if (!newProps && patchedChildren === node.children) return node

  return {
    ...node,
    props: newProps ? { ...node.props, ...newProps } : node.props,
    children: patchedChildren,
  }
}
