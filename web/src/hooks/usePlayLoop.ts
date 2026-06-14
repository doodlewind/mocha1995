/**
 * Drives the debugger's "play" transport: while `playing` is true, advances
 * the trace one step at a time at `speed` steps/second, pausing at the end.
 */
import { useEffect } from 'react'
import { useStore } from '../store'

export function usePlayLoop(): void {
  const playing = useStore((s) => s.playing)
  const speed = useStore((s) => s.speed)

  useEffect(() => {
    if (!playing) return
    const interval = Math.max(16, 1000 / Math.max(1, speed))
    const id = window.setInterval(() => {
      const s = useStore.getState()
      const len = s.result?.trace.length ?? 0
      if (len === 0) {
        s.pause()
        return
      }
      if (s.stepIndex >= len - 1) {
        s.pause()
        return
      }
      s.stepForward()
    }, interval)
    return () => window.clearInterval(id)
  }, [playing, speed])
}
