import type { AuthProvider } from "@refinedev/core"
import axios from "axios"

const API_BASE = import.meta.env.VITE_API_BASE_URL || "/api"

// Dedicated axios instance for auth — no interceptor redirect loop
const authApi = axios.create({
  baseURL: API_BASE,
  timeout: 15000,
  headers: { "Content-Type": "application/json" },
})

interface OesUser {
  guid: string
  name: string
  fullName: string
  roles: string[]
}

// Decode JWT payload without a library.
// Returns null if the token is malformed or expired.
function decodeJwtPayload(token: string): Record<string, unknown> | null {
  try {
    const parts = token.split(".")
    if (parts.length !== 3) return null

    // Base64url → Base64 → JSON
    const base64 = parts[1].replace(/-/g, "+").replace(/_/g, "/")
    const json = decodeURIComponent(
      atob(base64)
        .split("")
        .map((c) => "%" + c.charCodeAt(0).toString(16).padStart(2, "0"))
        .join(""),
    )
    return JSON.parse(json) as Record<string, unknown>
  } catch {
    return null
  }
}

function isTokenExpired(token: string): boolean {
  const payload = decodeJwtPayload(token)
  if (!payload) return true
  const exp = payload.exp
  if (typeof exp !== "number") return false // no expiry claim → treat as valid
  return Date.now() >= exp * 1000
}

function getStoredToken(): string | null {
  return localStorage.getItem("oes_token")
}

function getStoredUser(): OesUser | null {
  try {
    const raw = localStorage.getItem("oes_user")
    return raw ? (JSON.parse(raw) as OesUser) : null
  } catch {
    return null
  }
}

function clearStorage(): void {
  localStorage.removeItem("oes_token")
  localStorage.removeItem("oes_user")
}

export const oesAuthProvider: AuthProvider = {
  login: async ({ username, password }: { username: string; password: string }) => {
    try {
      const response = await authApi.post<{ token: string; user: OesUser }>("/auth/login", {
        username,
        password,
      })

      const { token, user } = response.data

      if (!token) {
        return {
          success: false,
          error: { name: "Login Error", message: "Server returned no token" },
        }
      }

      localStorage.setItem("oes_token", token)
      localStorage.setItem("oes_user", JSON.stringify(user))

      return { success: true, redirectTo: "/" }
    } catch (err) {
      if (axios.isAxiosError(err)) {
        const status = err.response?.status
        const message =
          status === 401 || status === 403
            ? "Invalid username or password"
            : (err.response?.data as { message?: string })?.message ?? "Connection error"

        return {
          success: false,
          error: { name: "Login Error", message },
        }
      }

      return {
        success: false,
        error: { name: "Login Error", message: "Unexpected error, please try again" },
      }
    }
  },

  logout: async () => {
    clearStorage()
    return { success: true, redirectTo: "/login" }
  },

  check: async () => {
    const token = getStoredToken()

    if (!token) {
      return { authenticated: false, redirectTo: "/login" }
    }

    if (isTokenExpired(token)) {
      clearStorage()
      return { authenticated: false, redirectTo: "/login" }
    }

    return { authenticated: true }
  },

  onError: async (error: { statusCode?: number; status?: number }) => {
    const status = error?.statusCode ?? error?.status

    if (status === 401) {
      clearStorage()
      return { logout: true, redirectTo: "/login" }
    }

    return { error: error as Error }
  },

  getIdentity: async () => {
    const user = getStoredUser()
    if (!user) return null

    return {
      id: user.guid,
      name: user.fullName || user.name,
      avatar: undefined,
      roles: user.roles,
    }
  },
}
