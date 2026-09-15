#!/usr/bin/env bash
set -euo pipefail

USER_NAME="${DOCKER_USER:-dev}"
USER_ID="${DOCKER_USER_ID:-1000}"
GROUP_NAME="${DOCKER_GRP:-dev}"
GROUP_ID="${DOCKER_GRP_ID:-1000}"
HOST_NAME="$(hostname)"

ensure_hosts() {
  if ! grep -q "[[:space:]]${HOST_NAME}$" /etc/hosts; then
    echo "127.0.1.1 ${HOST_NAME}" >>/etc/hosts
  fi
}

ensure_group() {
  if getent group "${GROUP_ID}" >/dev/null 2>&1; then
    GROUP_NAME="$(getent group "${GROUP_ID}" | cut -d: -f1)"
  elif ! getent group "${GROUP_NAME}" >/dev/null 2>&1; then
    groupadd --gid "${GROUP_ID}" "${GROUP_NAME}"
  fi
}

ensure_user() {
  if id -u "${USER_NAME}" >/dev/null 2>&1; then
    usermod -aG sudo,video,dialout "${USER_NAME}" || true
    return
  fi

  local existing_user=""
  existing_user="$(getent passwd "${USER_ID}" | cut -d: -f1 | head -n 1 || true)"
  if [ -n "${existing_user}" ]; then
    usermod -l "${USER_NAME}" "${existing_user}"
    usermod -d "/home/${USER_NAME}" -m "${USER_NAME}" 2>/dev/null || true
  else
    useradd --uid "${USER_ID}" --gid "${GROUP_NAME}" --create-home --shell /bin/bash "${USER_NAME}"
  fi
  usermod -aG sudo,video,dialout "${USER_NAME}" || true
}

configure_sudo() {
  echo "%sudo ALL=(ALL) NOPASSWD:ALL" >/etc/sudoers.d/90-cross-proj-nopasswd
  chmod 0440 /etc/sudoers.d/90-cross-proj-nopasswd
}

configure_shell() {
  local home_dir bashrc
  home_dir="$(getent passwd "${USER_NAME}" | cut -d: -f6)"
  bashrc="${home_dir}/.bashrc"
  mkdir -p "${home_dir}"
  touch "${bashrc}"
  if ! grep -q "cross-proj environment" "${bashrc}"; then
    cat >>"${bashrc}" <<'BASHRC_EOF'
# cross-proj environment
export PATH=/opt/riscv/bin:$PATH
export ARCH=riscv
export CROSS_COMPILE=riscv64-unknown-linux-gnu-
alias set-p550='source /workspace/scripts/source_sdk_env.sh P550'
BASHRC_EOF
  fi
  chown "${USER_NAME}:${GROUP_NAME}" "${bashrc}"
}

main() {
  ensure_hosts
  ensure_group
  ensure_user
  configure_sudo
  configure_shell

  echo "PS1='\[\033[01;32m\]\u@\[\033[01;35m\]\h\[\033[00m\]:\[\033[01;36m\]\w\[\033[00m\]$ '" >> /home/${USER_NAME}/.bashrc

  echo "user ready: ${USER_NAME}(${USER_ID}:${GROUP_ID}), sudo NOPASSWD enabled"
}

main "$@"
