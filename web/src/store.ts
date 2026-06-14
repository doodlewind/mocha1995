/**
 * Central app + debugger state. Every panel reads from here so the editor,
 * token view, bytecode view, stack, call stack and charts all stay in sync
 * with the single "current step" of the execution trace.
 */
import { create } from 'zustand'
import type { RunResult, TraceStep } from './engine/types'
import { runMocha } from './engine/runner'
import { DEFAULT_CODE } from './engine/examples'

export type RunStatus = 'idle' | 'running' | 'done' | 'error'

interface AppState {
  // --- source + run ---
  code: string
  status: RunStatus
  result: RunResult | null
  loadError: string | null
  setCode: (code: string) => void
  run: () => Promise<void>

  // --- debugger transport ---
  /** Index into result.trace, or -1 when no step is selected. */
  stepIndex: number
  playing: boolean
  /** Steps per second when playing. */
  speed: number
  setStep: (i: number) => void
  stepForward: () => void
  stepBack: () => void
  stepOver: () => void // advance to next step at same-or-shallower depth
  play: () => void
  pause: () => void
  togglePlay: () => void
  reset: () => void
  setSpeed: (s: number) => void
}

function clampStep(i: number, len: number): number {
  if (len === 0) return -1
  return Math.max(0, Math.min(i, len - 1))
}

export const useStore = create<AppState>((set, get) => ({
  code: DEFAULT_CODE,
  status: 'idle',
  result: null,
  loadError: null,

  setCode: (code) => set({ code }),

  run: async () => {
    set({ status: 'running', loadError: null })
    try {
      const result = await runMocha(get().code)
      set({
        result,
        status: result.ok ? 'done' : 'error',
        stepIndex: result.trace.length ? 0 : -1,
        playing: false,
      })
    } catch (e) {
      set({
        status: 'error',
        loadError: e instanceof Error ? e.message : String(e),
        result: null,
        stepIndex: -1,
        playing: false,
      })
    }
  },

  stepIndex: -1,
  playing: false,
  speed: 6,

  setStep: (i) => {
    const len = get().result?.trace.length ?? 0
    set({ stepIndex: clampStep(i, len), playing: false })
  },

  stepForward: () => {
    const { stepIndex, result } = get()
    const len = result?.trace.length ?? 0
    if (len === 0) return
    set({ stepIndex: clampStep(stepIndex + 1, len) })
  },

  stepBack: () => {
    const { stepIndex, result } = get()
    const len = result?.trace.length ?? 0
    if (len === 0) return
    set({ stepIndex: clampStep(stepIndex - 1, len), playing: false })
  },

  stepOver: () => {
    const { stepIndex, result } = get()
    const trace = result?.trace
    if (!trace || trace.length === 0) return
    const cur: TraceStep | undefined = trace[stepIndex]
    if (!cur) {
      set({ stepIndex: 0 })
      return
    }
    let i = stepIndex + 1
    while (i < trace.length && trace[i].depth > cur.depth) i++
    set({ stepIndex: clampStep(i, trace.length), playing: false })
  },

  play: () => {
    const { result, stepIndex } = get()
    const len = result?.trace.length ?? 0
    if (len === 0) return
    if (stepIndex >= len - 1) set({ stepIndex: 0 })
    set({ playing: true })
  },
  pause: () => set({ playing: false }),
  togglePlay: () => (get().playing ? get().pause() : get().play()),
  reset: () =>
    set({ stepIndex: get().result?.trace.length ? 0 : -1, playing: false }),
  setSpeed: (speed) => set({ speed }),
}))

/** Convenience selector: the currently selected trace step (or null). */
export function useCurrentStep(): TraceStep | null {
  return useStore((s) =>
    s.result && s.stepIndex >= 0 ? s.result.trace[s.stepIndex] ?? null : null,
  )
}
