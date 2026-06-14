/**
 * Loads the Mocha 1995 engine (compiled to WebAssembly) and runs source code,
 * returning the structured RunResult. The .mjs/.wasm pair lives in
 * /public/engine and is produced by ../../build_wasm.sh.
 */
import type { RunResult } from './types'

interface MochaModule {
  cwrap: (
    name: string,
    ret: string | null,
    args: string[],
  ) => (...a: unknown[]) => number
  UTF8ToString: (ptr: number) => string
  _free: (ptr: number) => void
}

type ModuleFactory = (opts?: Record<string, unknown>) => Promise<MochaModule>

let modulePromise: Promise<{
  mod: MochaModule
  run: (src: string) => number
}> | null = null

// Resolve the engine URL relative to the deployed base path.
const engineUrl = new URL('engine/mocha.mjs', document.baseURI).href

async function load() {
  if (!modulePromise) {
    modulePromise = (async () => {
      const factory: ModuleFactory = (
        await import(/* @vite-ignore */ engineUrl)
      ).default
      const mod = await factory()
      const run = mod.cwrap('mocha_run_json', 'number', ['string'])
      return { mod, run: run as (src: string) => number }
    })()
  }
  return modulePromise
}

/** Kick off engine loading early (e.g. on app mount). */
export function preloadEngine(): void {
  void load()
}

/** Compile and run `source` with the original engine. */
export async function runMocha(source: string): Promise<RunResult> {
  const { mod, run } = await load()
  const ptr = run(source)
  if (!ptr) throw new Error('engine returned null')
  const json = mod.UTF8ToString(ptr)
  mod._free(ptr)
  return JSON.parse(json) as RunResult
}
