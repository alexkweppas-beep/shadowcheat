import assert from 'node:assert/strict'
import {
  worldToLatLng,
  latLngToWorld,
  overlayBounds,
  worldToPixel,
  DEFAULT_BOUNDS,
  MAP_PIXELS,
  METERS_PER_PIXEL
} from './radarCoords.js'

const [lat, lng] = worldToLatLng(100, -50)
assert.equal(lat, -50)
assert.equal(lng, 100)
assert.deepEqual(latLngToWorld(lat, lng), { x: 100, y: -50 })

const box = overlayBounds(DEFAULT_BOUNDS)
assert.deepEqual(box[0], [DEFAULT_BOUNDS.south, DEFAULT_BOUNDS.west])
assert.deepEqual(box[1], [DEFAULT_BOUNDS.north, DEFAULT_BOUNDS.east])

const spanX = DEFAULT_BOUNDS.east - DEFAULT_BOUNDS.west
const spanY = DEFAULT_BOUNDS.north - DEFAULT_BOUNDS.south
assert.equal(spanX, MAP_PIXELS * METERS_PER_PIXEL)
assert.equal(spanX, spanY)

const chumash = worldToPixel(-3426.683, 967.738)
assert.ok(Math.abs(chumash[0] - 746) < 3)
assert.ok(Math.abs(chumash[1] - 2449) < 3)

const delPerro = worldToPixel(-1850.127, -1231.751)
assert.ok(Math.abs(delPerro[0] - 1275) < 3)
assert.ok(Math.abs(delPerro[1] - 3181) < 3)

const alamo = worldToPixel(712, 3940)
assert.ok(alamo[0] > 1800 && alamo[0] < 2400)
assert.ok(alamo[1] > 1200 && alamo[1] < 1800)

console.log('radarCoords ok')
