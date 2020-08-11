#!/usr/bin/env bash

function die() {
  echo >&2 "ERROR: ${*}"
  exit 1
}

function run() {
  "$@"
  local code=$?
  [[ ${code} -ne 0 ]] && die "command [$*] failed with error code ${code}"
  exit 0
}

function check_status_code() {
  code=$?
  if [[ ${code} != 0 ]]; then
    exit 1
  fi
}

function get_opcode() {
  local tmp

  tmp=$(run grep "^DECLARE_INSN.*\<${2}\>" "${1}")
  check_status_code
  tmp=$(run sed 's/DECLARE_INSN(.*,\(.*\),.*)/\1/' <<<"${tmp}")
  check_status_code

  echo "${tmp}"
}

function get_insn_list() {
  local tmp

  tmp=$(run grep ^DECLARE_INSN "${1}")
  check_status_code
  tmp=$(run sed 's/DECLARE_INSN(\(.*\),.*,.*)/\1/' <<<"${tmp}")
  check_status_code

  echo "${tmp}"
}

function get_basename() {
  local b=${1##*/}
  echo "${b%.*}"
}

function riscv_gen_srcs() {
  local output=$1
  local insn_basename=$2
  local encoding_h=$3
  local insn_template_cc=$4

  local gen_src
  local insn_opcode

  echo "Generating instruction src [${output}]"

  #  insn_basename=$(get_basename "${output}")
  check_status_code
  insn_opcode=$(get_opcode "${encoding_h}" "${insn_basename}")
  check_status_code
  gen_src=$(run sed "s/NAME/${insn_basename}/" "${insn_template_cc}")
  check_status_code
  gen_src=$(run sed "s/OPCODE/${insn_opcode}/" <<<"${gen_src}")
  check_status_code

  echo "${gen_src}" >"${output}"
}

function riscv_gen_icache_h() {
  local output=$1
  local exe_gen_icache=$2
  local mmu_h=$3

  local icache_entries
  local gen_icache_h

  echo "Generating icache header [${output}]"

  icache_entries=$(run grep "ICACHE_ENTRIES =" "${mmu_h}")
  check_status_code
  icache_entries=$(run sed 's/.* = \(.*\);/\1/' <<<"${icache_entries}")
  check_status_code
  gen_icache_h=$(run "${exe_gen_icache}" "${icache_entries}")
  check_status_code

  echo "${gen_icache_h}" >"${output}"
}

function riscv_gen_insn_list() {
  local output=$1
  local insn_list=( "${@:2}" )

  echo "Generating instruction list [${output}] (contains ${#insn_list[@]} instructions)"

  for insn in "${insn_list[@]}"; do
    printf 'DEFINE_INSN(%s)\n' "${insn//\./_}"
  done >"${output}"
}

case $1 in
"insn_list")
  riscv_gen_insn_list "${@:2}"
  ;;
"insn_src")
  riscv_gen_srcs "${@:2}"
  ;;
"icache")
  riscv_gen_icache_h "${@:2}"
  ;;
*)
  echo >&2 Invalid argument
  echo >&2 Usage:
  echo >&2 "$0 insn_list <output> [insn_1 insn_2 ... insn_n]"
  echo >&2 "$0 insn_src  <output> <insn> <path encoding.h> <path insn_template.cc>"
  echo >&2 "$0 icache    <output> <path gen_icache.sh> <path mmu.h>"
  exit 2
  ;;
esac
