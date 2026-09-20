function createRadarStore(ttlMs) {
  const frames = new Map()
  const listeners = new Map()

  function prune(now) {
    for (const [id, entry] of frames) {
      if (now - entry.at > ttlMs)
        frames.delete(id)
    }
  }

  function put(userId, frame, now) {
    frames.set(userId, { frame, at: now })
    const set = listeners.get(userId)
    if (!set)
      return
    for (const send of set)
      send(frame)
  }

  function get(userId, now) {
    const entry = frames.get(userId)
    if (!entry)
      return null
    if (now - entry.at > ttlMs) {
      frames.delete(userId)
      return null
    }
    return entry.frame
  }

  function subscribe(userId, send) {
    let set = listeners.get(userId)
    if (!set) {
      set = new Set()
      listeners.set(userId, set)
    }
    set.add(send)
    return () => {
      set.delete(send)
      if (set.size === 0)
        listeners.delete(userId)
    }
  }

  return { put, get, prune, subscribe }
}

module.exports = { createRadarStore }
