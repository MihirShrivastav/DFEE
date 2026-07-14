# DFEE Documentation

Canonical home for DFEE product, architecture, and design documentation.

> Note on folders: `documentation/` holds the durable, canonical product and
> architecture docs. `docs/superpowers/` holds the brainstorming-workflow
> artifacts (dated specs and implementation plans) used to drive individual
> pieces of work; treat those as process history, and treat the docs here as
> the source of truth.

## Architecture

- [Film Lab Framework Architecture](architecture/film-lab-framework-architecture.md)
  — the overall framework: the scene-referred film-emulation vision, the
  subtractive colour core, the image-analysis steering layer, the pipeline and
  components, how each film stock gets its character, the user controls, and the
  delivery roadmap.

## Related engine docs

- Migration / engine notes live under `cpp_engine/migration_docs/`
  (`FILM_LAB_WORKFLOW.md`, `FILM_LAB_IMPLEMENTATION_PLAN.md`,
  `STOCK_PROFILE_CONTRACT.md`, etc.).
- `docs/technical_architecture.md` covers the current native engine and API
  contract.
