import type { AuthProvider } from "@refinedev/core"
import axios from "axios"

const api = axios.create({
  baseURL: import.meta.env.VITE_API_BASE_URL || "/api",
})

export const oesAuthProvider: AuthProvider = {
  login: async ({ username, password }) => {
    try {
      const response = await api.post("/auth/login", { username, password })
      localStorage.setItem("oes_token", response.data.token)
      localStorage.setItem("oes_user", JSON.stringify(response.data.user))
      return { success: true, redirectTo: "/" }
    } catch {
      return { success: false, error: { name: "Login Error", message: "Invalid credentials" } }
    }
  },

  logout: async () => {
    localStorage.removeItem("oes_token")
    localStorage.removeItem("oes_user")
    return { success: true, redirectTo: "/login" }
  },

  check: async () => {
    const token = localStorage.getItem("oes_token")
    return token ? { authenticated: true } : { authenticated: false, redirectTo: "/login" }
  },

  getIdentity: async () => {
    const user = localStorage.getItem("oes_user")
    return user ? JSON.parse(user) : null
  },

  onError: async (error) => {
    if (error?.statusCode === 401) {
      return { logout: true, redirectTo: "/login" }
    }
    return { error }
  },
}
