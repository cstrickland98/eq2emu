#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: scripts/rebuild_eq2emu_linux.sh [options]

Rebuild only the EQ2Emu LoginServer and WorldServer from an existing checkout,
then deploy the runnable server files into /media/nvme0n1/eq2emu/server/build.

Defaults match this layout:
  /media/nvme0n1/eq2emu/source/eq2emu        EQ2Emu source checkout
  /media/nvme0n1/eq2emu/server               Existing fmt/recast/content/maps repos
  /media/nvme0n1/eq2emu/server/build         Runtime server directory

Options:
  --base PATH       Base EQ2Emu directory. Default: /media/nvme0n1/eq2emu
  --source PATH     EQ2Emu source checkout. Default: BASE/source/eq2emu
  --deps PATH       Directory containing fmt and recastnavigation. Default: BASE/server
  --run PATH        Runtime deploy directory. Default: BASE/server/build
  --jobs N          Parallel make jobs. Default: nproc
  --skip-assets     Do not copy eq2emu-content or eq2emu-maps assets
  -h, --help        Show this help

Environment overrides:
  EQ2EMU_BASE, EQ2EMU_SRC, EQ2EMU_DEPS, EQ2EMU_RUN, JOBS
  FMT_DIR, RECAST_DIR, CONTENT_DIR, MAPS_DIR
USAGE
}

log() {
    printf '[eq2emu-rebuild] %s\n' "$*"
}

die() {
    printf '[eq2emu-rebuild] ERROR: %s\n' "$*" >&2
    exit 1
}

default_jobs() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    else
        printf '1\n'
    fi
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "Missing required command: $1"
}

require_dir() {
    local path="$1"
    local label="$2"

    [[ -d "$path" ]] || die "Missing ${label}: ${path}"
}

require_file() {
    local path="$1"
    local label="$2"

    [[ -f "$path" ]] || die "Missing ${label}: ${path}"
}

first_existing_dir() {
    local candidate

    for candidate in "$@"; do
        if [[ -d "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

sed_replacement_escape() {
    printf '%s' "$1" | sed -e 's/[&|\\]/\\&/g'
}

copy_tree_contents() {
    local src="$1"
    local dst="$2"

    mkdir -p "$dst"
    if command -v rsync >/dev/null 2>&1; then
        rsync -a "$src"/ "$dst"/
    else
        cp -a "$src"/. "$dst"/
    fi
}

copy_named_dir() {
    local parent="$1"
    local name="$2"
    local dst="$3"

    [[ -d "$parent/$name" ]] || return 0

    mkdir -p "$dst/$name"
    if command -v rsync >/dev/null 2>&1; then
        rsync -a "$parent/$name"/ "$dst/$name"/
    else
        cp -a "$parent/$name"/. "$dst/$name"/
    fi
}

WORLD_MAKEFILE_BACKUP=""
WORLD_MAKEFILE=""

restore_world_makefile() {
    if [[ -n "${WORLD_MAKEFILE:-}" && -n "${WORLD_MAKEFILE_BACKUP:-}" && -f "$WORLD_MAKEFILE_BACKUP" ]]; then
        cp "$WORLD_MAKEFILE_BACKUP" "$WORLD_MAKEFILE"
        rm -f "$WORLD_MAKEFILE_BACKUP"
    fi
}

patch_world_makefile() {
    local fmt_include
    local detour_include
    local recast_include
    local debugutils_include
    local recast_lib

    WORLD_MAKEFILE="$SRC_DIR/source/WorldServer/makefile"
    WORLD_MAKEFILE_BACKUP="$(mktemp)"
    cp "$WORLD_MAKEFILE" "$WORLD_MAKEFILE_BACKUP"
    trap restore_world_makefile EXIT

    fmt_include="$(sed_replacement_escape "$FMT_DIR/include")"
    detour_include="$(sed_replacement_escape "$RECAST_DIR/Detour/Include")"
    recast_include="$(sed_replacement_escape "$RECAST_DIR/Recast/Include")"
    debugutils_include="$(sed_replacement_escape "$RECAST_DIR/DebugUtils/Include")"
    recast_lib="$(sed_replacement_escape "$RECAST_DIR/RecastDemo/Build/gmake2/lib/Debug")"

    sed -i -E \
        -e "s|-I[^[:space:]]*/fmt/include|-I${fmt_include}|g" \
        -e "s|-I[^[:space:]]*/Detour/Include|-I${detour_include}|g" \
        -e "s|-I[^[:space:]]*/Recast/Include|-I${recast_include}|g" \
        -e "s|-I[^[:space:]]*/DebugUtils/Include|-I${debugutils_include}|g" \
        -e "s|-L[^[:space:]]*/RecastDemo/Build/gmake2/lib/Debug|-L${recast_lib}|g" \
        "$WORLD_MAKEFILE"
}

build_server() {
    local name="$1"
    local dir="$2"

    log "Building ${name}"
    make -C "$dir" clean
    make -C "$dir" -j"$JOBS"
}

deploy_server() {
    local login_bin="$SRC_DIR/source/LoginServer/login"
    local world_bin="$SRC_DIR/source/WorldServer/eq2world"

    [[ -f "$login_bin" ]] || die "LoginServer binary was not produced: $login_bin"
    [[ -f "$world_bin" ]] || die "WorldServer binary was not produced: $world_bin"

    log "Deploying runtime files to $RUN_DIR"
    mkdir -p "$RUN_DIR"
    copy_tree_contents "$SRC_DIR/server" "$RUN_DIR"
    install -m 0755 "$login_bin" "$RUN_DIR/login"
    install -m 0755 "$world_bin" "$RUN_DIR/eq2world"

    if [[ "$COPY_ASSETS" -eq 1 ]]; then
        if [[ -n "$CONTENT_DIR" ]]; then
            log "Copying eq2emu-content assets from $CONTENT_DIR"
            copy_named_dir "$CONTENT_DIR" ItemScripts "$RUN_DIR"
            copy_named_dir "$CONTENT_DIR" PlayerScripts "$RUN_DIR"
            copy_named_dir "$CONTENT_DIR" Quests "$RUN_DIR"
            copy_named_dir "$CONTENT_DIR" RegionScripts "$RUN_DIR"
            copy_named_dir "$CONTENT_DIR" SpawnScripts "$RUN_DIR"
            copy_named_dir "$CONTENT_DIR" Spells "$RUN_DIR"
            copy_named_dir "$CONTENT_DIR" ZoneScripts "$RUN_DIR"
        else
            log "Skipping content assets; eq2emu-content was not found"
        fi

        if [[ -n "$MAPS_DIR" ]]; then
            log "Copying eq2emu-maps assets from $MAPS_DIR"
            copy_named_dir "$MAPS_DIR" Maps "$RUN_DIR"
            copy_named_dir "$MAPS_DIR" Regions "$RUN_DIR"
        else
            log "Skipping map assets; eq2emu-maps was not found"
        fi
    fi
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --base)
                [[ $# -ge 2 ]] || die "--base requires a path"
                BASE_DIR="$2"
                shift 2
                ;;
            --source)
                [[ $# -ge 2 ]] || die "--source requires a path"
                SRC_DIR="$2"
                shift 2
                ;;
            --deps)
                [[ $# -ge 2 ]] || die "--deps requires a path"
                DEPS_DIR="$2"
                shift 2
                ;;
            --run)
                [[ $# -ge 2 ]] || die "--run requires a path"
                RUN_DIR="$2"
                shift 2
                ;;
            --jobs)
                [[ $# -ge 2 ]] || die "--jobs requires a number"
                JOBS="$2"
                shift 2
                ;;
            --skip-assets)
                COPY_ASSETS=0
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                die "Unknown option: $1"
                ;;
        esac
    done
}

main() {
    BASE_DIR="${EQ2EMU_BASE:-/media/nvme0n1/eq2emu}"
    SRC_DIR="${EQ2EMU_SRC:-}"
    DEPS_DIR="${EQ2EMU_DEPS:-}"
    RUN_DIR="${EQ2EMU_RUN:-}"
    JOBS="${JOBS:-$(default_jobs)}"
    FMT_DIR="${FMT_DIR:-}"
    RECAST_DIR="${RECAST_DIR:-}"
    CONTENT_DIR="${CONTENT_DIR:-}"
    MAPS_DIR="${MAPS_DIR:-}"
    COPY_ASSETS=1

    parse_args "$@"

    SRC_DIR="${SRC_DIR:-$BASE_DIR/source/eq2emu}"
    DEPS_DIR="${DEPS_DIR:-$BASE_DIR/server}"
    RUN_DIR="${RUN_DIR:-$BASE_DIR/server/build}"

    [[ "$JOBS" =~ ^[0-9]+$ && "$JOBS" -gt 0 ]] || die "--jobs must be a positive integer"

    require_command make
    require_command sed
    require_command cp
    require_command install

    FMT_DIR="${FMT_DIR:-$(first_existing_dir "$DEPS_DIR/fmt" "$BASE_DIR/source/fmt" "$BASE_DIR/fmt" 2>/dev/null || true)}"
    RECAST_DIR="${RECAST_DIR:-$(first_existing_dir "$DEPS_DIR/recastnavigation" "$BASE_DIR/source/recastnavigation" "$BASE_DIR/recastnavigation" 2>/dev/null || true)}"

    if [[ "$COPY_ASSETS" -eq 1 ]]; then
        CONTENT_DIR="${CONTENT_DIR:-$(first_existing_dir "$DEPS_DIR/eq2emu-content" "$BASE_DIR/source/eq2emu-content" "$BASE_DIR/eq2emu-content" 2>/dev/null || true)}"
        MAPS_DIR="${MAPS_DIR:-$(first_existing_dir "$DEPS_DIR/eq2emu-maps" "$BASE_DIR/source/eq2emu-maps" "$BASE_DIR/eq2emu-maps" 2>/dev/null || true)}"
    fi

    require_dir "$SRC_DIR/source/LoginServer" "LoginServer source directory"
    require_dir "$SRC_DIR/source/WorldServer" "WorldServer source directory"
    require_dir "$SRC_DIR/server" "server runtime template directory"
    require_file "$SRC_DIR/source/WorldServer/makefile" "WorldServer makefile"
    [[ -n "$FMT_DIR" ]] || die "Could not find fmt. Set FMT_DIR or place it at $DEPS_DIR/fmt"
    [[ -n "$RECAST_DIR" ]] || die "Could not find recastnavigation. Set RECAST_DIR or place it at $DEPS_DIR/recastnavigation"
    require_dir "$FMT_DIR/include" "fmt headers"
    require_dir "$RECAST_DIR/Detour/Include" "Recast Detour headers"
    require_dir "$RECAST_DIR/Recast/Include" "Recast headers"
    require_dir "$RECAST_DIR/DebugUtils/Include" "Recast DebugUtils headers"
    require_dir "$RECAST_DIR/RecastDemo/Build/gmake2/lib/Debug" "Recast built libraries"

    log "Source: $SRC_DIR"
    log "Dependencies: $DEPS_DIR"
    log "Runtime: $RUN_DIR"
    log "Jobs: $JOBS"

    patch_world_makefile
    build_server "LoginServer" "$SRC_DIR/source/LoginServer"
    build_server "WorldServer" "$SRC_DIR/source/WorldServer"
    deploy_server

    log "Done. Start the server from: $RUN_DIR"
}

main "$@"
