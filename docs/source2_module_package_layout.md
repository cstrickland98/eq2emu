# Source2 Module And Content Package Layout

Source2 packages should keep code, data, scripts, config, and docs separate so feature migration can move in small reviewed slices.

## Package Shape

```text
modules/<module_name>/
  source/
  lua/
  sql/
    login/
    world/
  config/
  docs/
  tests/
```

## Source

`source/` contains C++ code owned by the module. New shared runtime code still belongs under `source2/<core|protocol|net|db|login|world|zone|scripting>` until there is a real module loader.

## Lua

`lua/` follows the source2 script loader category policy:

```text
lua/
  item/
  quest/
  spell/
  spawn/
  zone/
  player/
  region/
```

Script names resolve as `<category>/<name>.lua`.

## SQL

`sql/login` and `sql/world` hold migrations that should eventually be promoted into:

- `database/login/pending`
- `database/world/pending`

Accepted migrations move to the corresponding `updates` directory after review.

## Config

`config/` contains module-local defaults and examples. Server startup must not silently merge module config until the source2 config loader has an explicit module policy.

## Docs

`docs/` describes ownership, legacy references, supported Lua APIs, schema expectations, and verification commands for the module.

## Tests

`tests/` contains module-specific fixtures or scripts. C++ tests remain CMake targets under `source2` until module test discovery is implemented.
