# Shadow

Source portfolio for shadowcheat.cc: Windows clients, a native launcher, and a web application for a GTA V / RAGE Multiplayer project.

The project spans C++, Direct3D 11, Dear ImGui, React, and an Express / SQLite backend. This branch provides the project overview; implementation histories live in the component branches below.

## Components

| Component | Stack | Source branch |
| --- | --- | --- |
| Internal client | C++, Direct3D 11, Dear ImGui, CMake | [components/internal](https://github.com/alexkweppas-beep/shadowcheat-portfolio/tree/components/internal) |
| Alternate internal implementation | C++, CMake | [components/internal-next](https://github.com/alexkweppas-beep/shadowcheat-portfolio/tree/components/internal-next) |
| External client | C++, Dear ImGui | [components/external](https://github.com/alexkweppas-beep/shadowcheat-portfolio/tree/components/external) |
| Windows launcher | C++, Visual Studio / MSBuild | [components/launcher](https://github.com/alexkweppas-beep/shadowcheat-portfolio/tree/components/launcher) |
| Website and backend | React, Vite, Express, SQLite | [components/website](https://github.com/alexkweppas-beep/shadowcheat-portfolio/tree/components/website) |

## Repository layout

`main` is the documentation entry point. Each `components/*` branch retains its own source layout and development history. The components have not been combined into a single working build.

The directories on `main` are the original integration skeleton. Full implementations are in the branches listed above. `archive/dev-skeleton` preserves the former integration branch, and the `archive/monorepo-presplit` tag preserves the earlier combined source tree.

## Exploring the project

Start with the website branch to inspect the user interface and backend. Use the internal branch for the native rendering and configuration code, and the launcher branch for the Windows application.

Component-specific requirements and build configuration are stored alongside their sources. The launcher depends on a local VMProtect installation. Website configuration and database files require review before public distribution.

## Development history

The source history contains 176 commits. Author identities from the owner's alternate accounts have been consolidated under `alexkweppas-beep`; original source snapshots and commit dates were retained during that migration.

Third-party code retains its existing copyright and license notices. Build success and runtime behavior have not been revalidated as part of the repository presentation update.
