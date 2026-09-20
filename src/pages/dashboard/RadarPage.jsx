import { useEffect, useRef, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import L from 'leaflet'
import 'leaflet/dist/leaflet.css'
import Header from '../../components/Header'
import { API_BASE } from '../../api.js'
import { DEFAULT_BOUNDS, overlayBounds, worldToLatLng } from './radarCoords.js'
import './RadarPage.css'

const STALE_MS = 8000

function markerIcon(kind, heading) {
  const extra = kind === 'self' ? ' radar-dot--self'
    : kind === 'stale' ? ' radar-dot--stale'
    : kind === 'veh' ? ' radar-dot--veh'
    : ' radar-dot--player'
  return L.divIcon({
    className: '',
    html: `<div class="radar-dot${extra}" style="transform:rotate(${heading || 0}deg)"></div>`,
    iconSize: [12, 12],
    iconAnchor: [6, 6]
  })
}

function speedOf(prev, next, dtMs) {
  if (!prev || !next || dtMs <= 0)
    return 0
  const dx = next.x - prev.x
  const dy = next.y - prev.y
  return Math.hypot(dx, dy) / (dtMs / 1000)
}

export default function RadarPage() {
  const navigate = useNavigate()
  const mapEl = useRef(null)
  const mapRef = useRef(null)
  const overlayRef = useRef(null)
  const layerRef = useRef(null)
  const lastRef = useRef({})
  const [status, setStatus] = useState('connecting')
  const [frame, setFrame] = useState(null)
  const [followId, setFollowId] = useState(null) // null=self, undefined=free
  const [rotate, setRotate] = useState(false)

  useEffect(() => {
    const token = localStorage.getItem('token')
    if (!token) {
      navigate('/')
      return
    }
    const map = L.map(mapEl.current, {
      crs: L.CRS.Simple,
      minZoom: -2,
      maxZoom: 5,
      zoomControl: true,
      attributionControl: false
    })
    const overlay = L.imageOverlay('/radar/gta_map_4k.png', overlayBounds(DEFAULT_BOUNDS), { opacity: 0.92 })
    overlay.addTo(map)
    map.fitBounds(overlayBounds(DEFAULT_BOUNDS))
    const layer = L.layerGroup().addTo(map)
    map.on('dragstart', () => setFollowId(undefined))
    mapRef.current = map
    overlayRef.current = overlay
    layerRef.current = layer
    const source = new EventSource(`${API_BASE}/api/radar/stream?token=${encodeURIComponent(token)}`)
    source.addEventListener('frame', (event) => {
      setStatus('live')
      setFrame(JSON.parse(event.data))
    })
    source.addEventListener('idle', () => setStatus('idle'))
    source.onerror = () => setStatus('offline')
    return () => {
      source.close()
      map.remove()
    }
  }, [navigate])

  useEffect(() => {
    const map = mapRef.current
    const layer = layerRef.current
    if (!map || !layer)
      return
    layer.clearLayers()
    if (!frame)
      return
    const now = Date.now()
    const memory = lastRef.current
    const seen = new Set()
    const drawPlayer = (player, kind) => {
      const marker = L.marker(worldToLatLng(player.x, player.y), {
        icon: markerIcon(kind, player.h)
      })
      marker.bindTooltip(player.n || 'player', { permanent: false, direction: 'top' })
      marker.on('click', () => setFollowId(player.id))
      marker.addTo(layer)
    }
    if (frame.self)
      L.marker(worldToLatLng(frame.self.x, frame.self.y), { icon: markerIcon('self', frame.self.h) }).addTo(layer)
    for (const player of frame.players || []) {
      seen.add(String(player.id))
      const prev = memory[player.id]
      memory[player.id] = { ...player, at: now, speed: speedOf(prev, player, prev ? now - prev.at : 0) }
      drawPlayer(player, 'player')
    }
    for (const [id, stale] of Object.entries(memory)) {
      if (seen.has(id) || now - stale.at > STALE_MS) {
        if (now - stale.at > STALE_MS)
          delete memory[id]
        continue
      }
      drawPlayer({ ...stale, n: `${stale.n} · вне зоны` }, 'stale')
    }
    for (const vehicle of frame.vehicles || []) {
      L.marker(worldToLatLng(vehicle.x, vehicle.y), { icon: markerIcon('veh', vehicle.h) }).addTo(layer)
    }
    const followed = followId === undefined
      ? null
      : followId == null
        ? frame.self
        : (frame.players || []).find((player) => player.id === followId) || memory[followId]
    if (followed) {
      map.panTo(worldToLatLng(followed.x, followed.y), { animate: true, duration: 0.25 })
      map.getContainer().style.transform = rotate ? `rotate(${-followed.h}deg)` : ''
    }
  }, [frame, followId, rotate])

  const followed = followId == null || followId === undefined
    ? null
    : (frame?.players || []).find((player) => player.id === followId) || lastRef.current[followId]
  const staleSec = followed && !(frame?.players || []).some((player) => player.id === followId)
    ? Math.round((Date.now() - (followed.at || Date.now())) / 1000)
    : 0

  return (
    <div className="radar-page">
      <Header />
      <div ref={mapEl} className="radar-page__map" />
      <aside className="radar-hud">
        <div className="radar-hud__title">Web radar</div>
        <div className="radar-hud__row">
          <span>Статус</span>
          <span className={status === 'live' ? 'radar-hud__live' : 'radar-hud__off'}>{status}</span>
        </div>
        <div className="radar-hud__row">
          <span>Игроки</span>
          <span>{frame?.players?.length || 0}</span>
        </div>
        <div className="radar-hud__row">
          <span>Транспорт</span>
          <span>{frame?.vehicles?.length || 0}</span>
        </div>
      </aside>
      <aside className="radar-panel">
        <button type="button" className={followId === null ? 'is-on' : ''} onClick={() => setFollowId(null)}>
          Следить за собой
        </button>
        <button type="button" className={rotate ? 'is-on' : ''} onClick={() => setRotate((v) => !v)}>
          Поворот по курсу
        </button>
      </aside>
      {followed && (
        <aside className="radar-follow">
          <h2>{followed.n || 'Игрок'}</h2>
          <p>HP {followed.hp ?? '—'}</p>
          <p>{followed.veh ? 'В машине' : 'Пешком'}</p>
          <p>{(followed.speed || 0).toFixed(0)} м/с</p>
          {staleSec > 0 && <p>Вне зоны, {staleSec} с назад</p>}
        </aside>
      )}
    </div>
  )
}
