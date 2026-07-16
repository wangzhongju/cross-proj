#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE_PATH="$(cd "${SCRIPT_DIR}/.." && pwd)"
COMPOSE_FILE="${SCRIPT_DIR}/docker-compose-dev.yaml"
SERVICE_NAME="cross-proj"
COMPOSE_BIN=""
SDK_VERSION="${SDK_VERSION:-20250730}"
if [ -z "${COMPOSE_PROJECT_NAME:-}" ]; then
  if [ "$SDK_VERSION" = "20250730" ]; then
    COMPOSE_PROJECT_NAME="cross-proj"
  else
    COMPOSE_PROJECT_NAME="cross-proj-${SDK_VERSION}"
  fi
fi
IMAGE_NAME="${IMAGE_NAME:-cross-proj-p550:${SDK_VERSION}}"
if [ -z "${CONTAINER_NAME:-}" ]; then
  if [ "$SDK_VERSION" = "20250730" ]; then
    CONTAINER_NAME="cross-proj-$(id -un)"
  else
    CONTAINER_NAME="cross-proj-${SDK_VERSION}-$(id -un)"
  fi
fi
DOCKERFILE_PATH="${SCRIPT_DIR}/Dockerfile"

if [ -z "${DOCKER_USER:-}" ]; then DOCKER_USER="$(id -un)"; fi
if [ -z "${DOCKER_USER_ID:-}" ]; then DOCKER_USER_ID="$(id -u)"; fi
if [ -z "${DOCKER_GRP:-}" ]; then DOCKER_GRP="$(id -gn)"; fi
if [ -z "${DOCKER_GRP_ID:-}" ]; then DOCKER_GRP_ID="$(id -g)"; fi

export WORKSPACE_PATH IMAGE_NAME CONTAINER_NAME DOCKER_USER DOCKER_USER_ID DOCKER_GRP DOCKER_GRP_ID COMPOSE_PROJECT_NAME SDK_VERSION

compose() {
  if [ -z "${COMPOSE_BIN}" ]; then
    if docker compose version >/dev/null 2>&1; then
      COMPOSE_BIN="docker compose"
    elif command -v docker-compose >/dev/null 2>&1; then
      COMPOSE_BIN="docker-compose"
    else
      echo "docker compose is unavailable. Please install docker compose plugin or docker-compose." >&2
      exit 1
    fi
  fi
  if [ "${COMPOSE_BIN}" = "docker compose" ]; then
    docker compose -f "${COMPOSE_FILE}" "$@"
  else
    docker-compose -f "${COMPOSE_FILE}" "$@"
  fi
}

container_exists() { docker ps -a --format '{{.Names}}' | grep -Fxq "${CONTAINER_NAME}"; }
container_running() { docker ps --format '{{.Names}}' | grep -Fxq "${CONTAINER_NAME}"; }

is_compose_container() {
  local project service
  project="$(docker inspect "${CONTAINER_NAME}" --format '{{ index .Config.Labels "com.docker.compose.project" }}' 2>/dev/null || true)"
  service="$(docker inspect "${CONTAINER_NAME}" --format '{{ index .Config.Labels "com.docker.compose.service" }}' 2>/dev/null || true)"
  [ "${project}" = "${COMPOSE_PROJECT_NAME}" ] && [ "${service}" = "${SERVICE_NAME}" ]
}

remove_legacy_container() {
  if container_exists && ! is_compose_container; then
    echo "remove legacy non-compose container: ${CONTAINER_NAME}"
    docker rm -f "${CONTAINER_NAME}" >/dev/null
  fi
}

init_user_env() {
  if ! container_exists; then
    echo "container is not created: ${CONTAINER_NAME}" >&2
    exit 1
  fi
  docker cp "${SCRIPT_DIR}/env.sh" "${CONTAINER_NAME}:/tmp/cross-proj-env.sh"
  docker exec -u root     -e DOCKER_USER="${DOCKER_USER}"     -e DOCKER_USER_ID="${DOCKER_USER_ID}"     -e DOCKER_GRP="${DOCKER_GRP}"     -e DOCKER_GRP_ID="${DOCKER_GRP_ID}"     "${CONTAINER_NAME}" bash -lc "/tmp/cross-proj-env.sh"
}

build_image() {
  DOCKER_BUILDKIT=1 docker build --network host -t "${IMAGE_NAME}" -f "${DOCKERFILE_PATH}" "${SCRIPT_DIR}"
}

prepare_build_env() {
  if [ -x "${WORKSPACE_PATH}/scripts/prepare_desktop_build_env.sh" ]; then
    docker exec -u root -w /workspace "${CONTAINER_NAME}" bash -lc '/workspace/scripts/prepare_desktop_build_env.sh setup'
  fi
}

compile_image() { compose build "${SERVICE_NAME}"; }

start_container() {
  remove_legacy_container
  if ! docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1; then
    compile_image
  fi
  compose up -d --no-build "${SERVICE_NAME}"
  init_user_env
  prepare_build_env
  echo "container is running: ${CONTAINER_NAME}"
}

enter_container() {
  start_container
  if [ "$#" -gt 0 ]; then
    docker exec -u "${DOCKER_USER}" -w /workspace "${CONTAINER_NAME}" bash -lc "$*"
  elif [ -t 0 ]; then
    docker exec -it -u "${DOCKER_USER}" -w /workspace "${CONTAINER_NAME}" bash
  else
    echo "enter with: ${0} init"
  fi
}

into_container() {
  if ! container_running; then
    start_container
  else
    init_user_env
    prepare_build_env
  fi
  if [ "$#" -gt 0 ]; then
    docker exec -u "${DOCKER_USER}" -w /workspace "${CONTAINER_NAME}" bash -lc "$*"
  elif [ -t 0 ]; then
    docker exec -it -u "${DOCKER_USER}" -w /workspace "${CONTAINER_NAME}" bash
  else
    echo "container is running: ${CONTAINER_NAME}"
    echo "enter with: ${0} init"
  fi
}

stop_container() {
  compose down
  if container_exists; then
    docker rm -f "${CONTAINER_NAME}" >/dev/null
    echo "removed legacy container: ${CONTAINER_NAME}"
  fi
}

restart_container() {
  stop_container
  start_container
  if [ -t 0 ]; then
    docker exec -it -u "${DOCKER_USER}" -w /workspace "${CONTAINER_NAME}" bash
  fi
}

show_status() {
  echo "workspace: ${WORKSPACE_PATH}"
  echo "compose:   ${COMPOSE_FILE}"
  echo "project:   ${COMPOSE_PROJECT_NAME}"
  echo "service:   ${SERVICE_NAME}"
  echo "sdk:       ${SDK_VERSION}"
  echo "image:     ${IMAGE_NAME}"
  echo "container: ${CONTAINER_NAME}"
  docker images --format '{{.Repository}}:{{.Tag}} {{.ID}} {{.Size}}' | grep -F "${IMAGE_NAME%:*}:" || true
  docker ps -a --format '{{.Names}} {{.Image}} {{.Status}}' | grep -E "^${CONTAINER_NAME} " || true
}

show_help() {
  cat <<EOF
Usage: ./docker.sh <command> [command string]

Commands:
  compile    Build the Ubuntu 24.04 cross-compile image with docker compose.
  build      Alias of compile.
  start      Start the compose container, initialize user sudo, and enter if TTY is available.
  init       Enter the running container as the host user, or run an optional command string.
  into       Alias of init.
  stop       Stop and remove the compose container.
  restart    Restart the compose container and initialize user sudo.
  status     Show image/container status.
  help       Show this help message.

Examples:
  ./docker.sh compile
  ./docker.sh start
  ./docker.sh init
  ./docker.sh init 'cd /workspace && ./scripts/build_minimal_system.sh P550'
  ./docker.sh stop
EOF
}

main() {
  local cmd="${1:-help}"
  shift || true
  case "${cmd}" in
    compile|build) compile_image ;;
    start) enter_container "$@" ;;
    init|into) into_container "$@" ;;
    stop) stop_container ;;
    restart) restart_container ;;
    status) show_status ;;
    -h|--help|help) show_help ;;
    *) echo "unknown command: ${cmd}" >&2; show_help; exit 1 ;;
  esac
}

main "$@"
