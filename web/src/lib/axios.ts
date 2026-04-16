import axios from "axios"

const API_BASE = import.meta.env.VITE_API_BASE_URL || "/api"

export const api = axios.create({
  baseURL: API_BASE,
  timeout: Number(import.meta.env.VITE_API_TIMEOUT) || 30000,
  headers: {
    "Content-Type": "application/json",
  },
})

api.interceptors.request.use((config) => {
  const token = localStorage.getItem("oes_token")
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

api.interceptors.response.use(
  (response) => response,
  (error) => {
    // Don't auto-logout here — let Refine's onError handle auth failures
    return Promise.reject(error)
  },
)

export const getApiUrl = () => API_BASE
