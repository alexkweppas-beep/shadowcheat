// 4096 satellite, uniform 3 m/px. Origin from Chumash + Del Perro pier tips.
export const MAP_PIXELS = 4096
export const METERS_PER_PIXEL = 3
export const ORIGIN_WEST = -5669.905
export const ORIGIN_NORTH = 8312.994

const MAP_SPAN = MAP_PIXELS * METERS_PER_PIXEL

export const DEFAULT_BOUNDS = {
  west: ORIGIN_WEST,
  east: ORIGIN_WEST + MAP_SPAN,
  south: ORIGIN_NORTH - MAP_SPAN,
  north: ORIGIN_NORTH
}

export function worldToLatLng(x, y) {
  return [y, x]
}

export function latLngToWorld(lat, lng) {
  return { x: lng, y: lat }
}

export function overlayBounds(bounds) {
  return [
    [bounds.south, bounds.west],
    [bounds.north, bounds.east]
  ]
}

export function worldToPixel(x, y) {
  return [
    (x - ORIGIN_WEST) / METERS_PER_PIXEL,
    (ORIGIN_NORTH - y) / METERS_PER_PIXEL
  ]
}
