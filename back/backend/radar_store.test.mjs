import { createRequire } from 'node:module'
import assert from 'node:assert/strict'

const { createRadarStore } = createRequire(import.meta.url)('./radar_store.js')
const store = createRadarStore(1000)
const frame = { players: [{ id: 1 }] }
store.put(7, frame, 1000)
assert.deepEqual(store.get(7, 1500), frame)
assert.equal(store.get(7, 2100), null)

let seen = null
const unsub = store.subscribe(3, (next) => { seen = next })
store.put(3, { at: 1 }, 0)
assert.equal(seen.at, 1)
unsub()
store.put(3, { at: 2 }, 1)
assert.equal(seen.at, 1)
console.log('radar_store ok')
