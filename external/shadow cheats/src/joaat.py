def joaat(s):
    h = 0
    for char in s.lower():
        h += ord(char)
        h = h & 0xFFFFFFFF
        h += (h << 10)
        h = h & 0xFFFFFFFF
        h ^= (h >> 6)
    h += (h << 3)
    h = h & 0xFFFFFFFF
    h ^= (h >> 11)
    h += (h << 15)
    return h & 0xFFFFFFFF

words = [
    "PICKUP_WEAPON_ASSAULTRIFLE",
    "PICKUP_WEAPON_CARBINERIFLE",
    "PICKUP_WEAPON_SPECIALCARBINE",
    "PICKUP_WEAPON_MINISMG",
    "PICKUP_WEAPON_REVOLVER",
    "PICKUP_WEAPON_SAWNOFFSHOTGUN",
    "PICKUP_WEAPON_SAWEDOFFSHOTGUN",
    "WEAPON_ASSAULTRIFLE",
    "WEAPON_CARBINERIFLE",
    "w_ar_assaultrifle",
    "w_ar_carbinerifle",
    "w_ar_specialcarbine",
    "w_sb_minismg"
]

for w in words:
    print(f"{w}: 0x{joaat(w):X}")
