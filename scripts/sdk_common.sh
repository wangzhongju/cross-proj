#!/usr/bin/env bash

sdk_canonical_version() {
  local version=${1:-20250730}
  case "$version" in
    20260630) echo "202606" ;;
    *) echo "$version" ;;
  esac
}

sdk_dir_name_from_version() {
  local version
  version=$(sdk_canonical_version "${1:-20250730}")
  case "$version" in
    20250730) echo "eswin-sdk-20250730" ;;
    202606) echo "eswin-sdk-202606-ubuntu" ;;
    *) echo "eswin-sdk-${version}" ;;
  esac
}

sdk_resolve_dir() {
  local workspace=${WORKSPACE:-/workspace}
  if [ -n "${SDK_DIR:-}" ]; then
    echo "$SDK_DIR"
  elif [ -n "${SDK_NAME:-}" ]; then
    echo "$workspace/$SDK_NAME"
  else
    echo "$workspace/$(sdk_dir_name_from_version "${SDK_VERSION:-20250730}")"
  fi
}

sdk_resolve_version() {
  local sdk_dir=${1:-$(sdk_resolve_dir)}
  if [ -n "${SDK_VERSION:-}" ]; then
    sdk_canonical_version "$SDK_VERSION"
    return
  fi
  case "$(basename "$sdk_dir")" in
    eswin-sdk-20250730) echo "20250730" ;;
    eswin-sdk-202606*) echo "202606" ;;
    eswin-sdk-*) basename "$sdk_dir" | sed -E 's/^eswin-sdk-([^-/]+).*/\1/' ;;
    *) echo "unknown" ;;
  esac
}

sdk_layout() {
  local sdk_dir=${1:-$(sdk_resolve_dir)}
  if [ -d "$sdk_dir/source/risc-v-gadget" ]; then
    echo "ubuntu-image"
  elif [ -d "$sdk_dir/source/mkimg-eswin" ] || [ -f "$sdk_dir/source/mkimg-eswin.tar.gz" ]; then
    echo "mkimg"
  else
    echo "unknown"
  fi
}

sdk_board_name() {
  local model=${1:-P550}
  local sdk_dir=${2:-$(sdk_resolve_dir)}
  local version
  version=$(sdk_resolve_version "$sdk_dir")
  case "$version:$model" in
    202606:P550) echo "eic7700-hifive-premier-p550" ;;
    *) echo "$model" ;;
  esac
}

sdk_source_choice() {
  local model=${1:-P550}
  local sdk_dir=${2:-$(sdk_resolve_dir)}
  local version
  version=$(sdk_resolve_version "$sdk_dir")
  case "$version:$model" in
    202606:P550|202606:eic7700-hifive-premier-p550) echo 1 ;;
    202606:eic7700-sbc|202606:SBC-A1) echo 2 ;;
    202606:eic7702-d560|202606:EBC7702-D01) echo 0 ;;
    *:EIC7700FG-LP4*|*:EIC7700FG-LP5*) echo 1 ;;
    *:EIC7700FG-EVB-LP5-A2) echo 2 ;;
    *:EIC7700-E-L5G4) echo 3 ;;
    *:EIC7702-E-L5G5) echo 4 ;;
    *:P550) echo 5 ;;
    *:EIC7700-02-1154B1) echo 6 ;;
    *:EBC7702-D01) echo 7 ;;
    *) echo "unsupported model for SDK $(basename "$sdk_dir"): $model" >&2; return 1 ;;
  esac
}

sdk_export_selection() {
  MODEL=${MODEL:-P550}
  WORKSPACE=${WORKSPACE:-/workspace}
  SDK_DIR=$(sdk_resolve_dir)
  SDK_VERSION=$(sdk_resolve_version "$SDK_DIR")
  SDK_LAYOUT=$(sdk_layout "$SDK_DIR")
  SDK_BOARD_NAME=$(sdk_board_name "$MODEL" "$SDK_DIR")
  SDK_OUTPUT_DIR=$SDK_DIR/$SDK_BOARD_NAME/output
  export MODEL WORKSPACE SDK_DIR SDK_VERSION SDK_LAYOUT SDK_BOARD_NAME SDK_OUTPUT_DIR
}
