# Shadow Web

Web application and backend for shadowcheat.cc. The interface includes product pages, account access, subscriptions, a distributor area, an administration panel, and a radar page.

## Stack

- React 19, React Router, Vite, and plain CSS.
- Leaflet for the radar map; Lucide for interface icons.
- Express with SQLite, JWT authentication, and bcrypt.
- Docker and nginx configuration alongside the application.

## Source layout

| Path | Purpose |
| --- | --- |
| `src/pages/` | Public pages and account views |
| `src/components/` | Shared interface components |
| `src/layouts/` | Dashboard layout |
| `public/` | Static images, icons, and map assets |
| `back/backend/` | API, database integration, and backend tests |

## Local frontend

```sh
npm ci
npm run dev
```

The package also exposes `build`, `preview`, and `lint` scripts. Backend dependencies and scripts are defined separately in `back/backend/package.json`; the backend tests use Node's built-in test runner.

## Configuration

Frontend environment files and backend configuration are separate. The backend provides `.env.example`. Review local configuration and database contents before distributing this branch publicly.

## Repository

See the [project overview](https://github.com/alexkweppas-beep/shadowcheat-portfolio) for the native components and preserved development history. Existing source files and third-party notices are retained. This documentation update does not claim a new build or runtime verification.
