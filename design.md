# Design — shadow

A locked design system for this app. Every page redesign reads this file before
emitting code. Do not regenerate per page — extend or amend this file when the
system needs to grow.

## Genre
atmospheric

## Macrostructure family
- Marketing pages: Split Studio (H2 split diptych, alternating halves; S2 hanging section heads)
- App pages:       Workbench (functional headings, product surfaces, no enrichment)
- Content pages:   Conversational FAQ (accordion, left-aligned)

## Theme
Midnight — dark cool paper, warm amber accent, grotesk sans.

- `--color-paper`   oklch(13.4% 0.022 250)
- `--color-paper-2` oklch(17.2% 0.020 250)
- `--color-paper-3` oklch(21.4% 0.018 248)
- `--color-ink`     oklch(94.2% 0.012 80)
- `--color-ink-2`   oklch(72.0% 0.016 250)
- `--color-rule`    oklch(32.0% 0.018 250)
- `--color-accent`  oklch(74.0% 0.140 55)
- `--color-focus`   oklch(78.0% 0.150 55)

Axes: dark / grotesk-sans / warm

## Typography
- Display: Geist, weight 700, style normal
- Body:    Geist, weight 400
- Mono:    Geist Mono, weight 400 — product codes (EXTERNAL / TRIGGER) only
- Display tracking: -0.035em
- Type scale anchor: `--text-display` = clamp(2.75rem, 5vw + 1rem, 5.25rem)

## Spacing
4-point named scale. The values are in `tokens.css`. Pages must use named
tokens (`var(--space-md)`), never raw values.

## Motion
- Easings: `--ease-out` cubic-bezier(0.16, 1, 0.3, 1); `--ease-in`; `--ease-in-out`
- Reveal pattern: fade only (dropdown + accordion). No slide on page sections.
- Reduced-motion fallback: opacity-only, ≤ 150 ms.
- Primitives (max 3): CTA hover lift · dropdown fade · FAQ accordion.

## Microinteractions stance
- silent success / celebratory toasts: never
- hover delay 800 ms · focus delay 0 ms
- Glassmorphism: never. Elevated paper-2 surfaces instead.

## CTA voice
- Primary CTA: accent fill, pill radius, verb-first («Приобрести», «Войти»)
- Secondary CTA: outlined chip, ink border, no fill («Наши продукты»)

## Nav / footer
- Nav: N1b three-section, 3 centre links, 1 dropdown, frost-on-scroll
- Footer: Ft5 Statement — closing line, then meta row

## Per-page allowances
- Marketing pages MAY use existing product captures in `<figure>` (no fake chrome).
- App pages MUST NOT use enrichment — function carries the page.
- Content pages: typography only.

## What pages MUST share
- The wordmark (`/logo.svg`).
- The accent colour and its placement (≤ 5 % per viewport).
- The display + body fonts.
- The CTA voice (button shape, radius, padding rhythm).
- Section heading rhythm: hanging head stacked above body — never numbered eyebrows.

## What pages MAY differ on
- Macrostructure within the page-type family.
- Hero archetype (marketing: H2 only).
- Enrichment — marketing only, existing captures.

## Tone
austere — dry, nocturnal, no gamer neon.

## Exports

### tokens.css
See `/tokens.css` at the project root — source of truth.

### Tailwind v4 `@theme`
```css
@theme {
  --color-paper:   oklch(13.4% 0.022 250);
  --color-paper-2: oklch(17.2% 0.020 250);
  --color-paper-3: oklch(21.4% 0.018 248);
  --color-ink:     oklch(94.2% 0.012 80);
  --color-ink-2:   oklch(72.0% 0.016 250);
  --color-rule:    oklch(32.0% 0.018 250);
  --color-accent:  oklch(74.0% 0.140 55);
  --color-focus:   oklch(78.0% 0.150 55);
  --font-display:  "Geist", sans-serif;
  --font-body:     "Geist", sans-serif;
  --font-outlier:  "Geist Mono", monospace;
  --spacing-md:    1rem;
  --text-md:       1.25rem;
  --ease-out:      cubic-bezier(0.16, 1, 0.3, 1);
}
```

### DTCG `tokens.json`
```json
{
  "color": {
    "paper":  { "$value": "oklch(13.4% 0.022 250)", "$type": "color" },
    "ink":    { "$value": "oklch(94.2% 0.012 80)", "$type": "color" },
    "accent": { "$value": "oklch(74.0% 0.140 55)", "$type": "color" }
  },
  "font": {
    "display": { "$value": "Geist", "$type": "fontFamily" },
    "body":    { "$value": "Geist", "$type": "fontFamily" },
    "outlier": { "$value": "Geist Mono", "$type": "fontFamily" }
  },
  "space": {
    "md": { "$value": "1rem", "$type": "dimension" }
  }
}
```

### shadcn/ui CSS variables
```css
:root {
  --background:         13.4% 0.022 250;
  --foreground:         94.2% 0.012 80;
  --primary:            74.0% 0.140 55;
  --primary-foreground: 18.0% 0.040 55;
  --muted:              32.0% 0.018 250;
  --muted-foreground:   58.0% 0.014 250;
  --border:             32.0% 0.018 250;
  --input:              32.0% 0.018 250;
  --ring:               78.0% 0.150 55;
  --radius:             12px;
}
```
