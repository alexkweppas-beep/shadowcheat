# Shadow — сайт і API

React-сайт і Express API для shadowcheat.cc. Тут є сторінки продуктів, особистий кабінет, підписки, адмінка та веб-радар.

Фронтенд використовує Vite, React Router, Leaflet і звичайний CSS. Бекенд працює з SQLite, JWT та bcrypt.

## Локальний фронтенд

```sh
npm ci
npm run dev
```

У `package.json` також є команди `build`, `preview` і `lint`.

## Папки

- `src/pages/` — сторінки сайту й кабінету.
- `src/components/`, `src/layouts/` — спільні компоненти та розкладка.
- `public/` — зображення, іконки й карта.
- `back/backend/` — API, база даних і тести бекенду.

Залежності та команди бекенду описані окремо в `back/backend/package.json`. Приклад конфігурації — `back/backend/.env.example`. У репозиторії є Dockerfile і налаштування nginx.

Перед відкритою публікацією потрібно перевірити `.env` і бази даних, зокрема їхні старі версії в історії.

Інші компоненти — в [основному README](https://github.com/alexkweppas-beep/shadowcheat).
