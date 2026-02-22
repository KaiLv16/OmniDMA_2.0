#!/bin/bash

cecho(){  # source: https://stackoverflow.com/a/53463162/2886168
    RED="\033[0;31m"
    GREEN="\033[0;32m"
    YELLOW="\033[0;33m"
    # ... ADD MORE COLORS
    NC="\033[0m" # No Color

    printf "${!1}${2} ${NC}\n"
}

build_config_id() {
    local topo="$1"
    local flow="$2"
    local drop_rate="$3"
    local pfc="$4"
    local irn="$5"
    local omnidma="$6"

    local base_config_id="GBN"
    if [[ "${irn}" == "1" ]]; then
        base_config_id="irn"
    fi
    if [[ "${omnidma}" == "1" ]]; then
        base_config_id="omnidma"
    fi

    local drop_rate_tag
    drop_rate_tag="$(python3 -c 'import sys; print(str(float(sys.argv[1])))' "${drop_rate}" 2>/dev/null)"
    if [[ -z "${drop_rate_tag}" ]]; then
        drop_rate_tag="${drop_rate}"
    fi

    local topo_tag="${topo//\//-}"
    local flow_tag="${flow//\//-}"
    echo "${base_config_id}_${topo_tag}_${flow_tag}_drop${drop_rate_tag}_pfc${pfc}_irn${irn}"
}

cecho "GREEN" "Running RDMA Network Load Balancing Simulations (leaf-spine topology)"

TOPOLOGY="topo_simple_dumbbell_OS2" # or, fat_k8_100G_OS2
NETLOAD="50" # network load 50%
RUNTIME="600" # 10 min simulation time

cecho "YELLOW" "\n----------------------------------"
cecho "YELLOW" "TOPOLOGY: ${TOPOLOGY}" 
cecho "YELLOW" "NETWORK LOAD: ${NETLOAD}" 
cecho "YELLOW" "TIME: ${RUNTIME}" 
cecho "YELLOW" "----------------------------------\n"


# cecho "GREEN" "Run Motivation GBN experiments..."
RATE_BOUND="0"
FLOW_NAME="omniDMA_flow"
PFC="1"
IRN="0"
OMNIDMA="0"
DROP_RATE="0.1"
CONFIG_ID="$(build_config_id "${TOPOLOGY}" "${FLOW_NAME}" "${DROP_RATE}" "${PFC}" "${IRN}" "${OMNIDMA}")"
OUTPUT_DIR="mix/output/${CONFIG_ID}"
SND_RCV_FILE="${OUTPUT_DIR}/snd_rcv_record_file.txt"
SWITCH_DROP_FILE="${OUTPUT_DIR}/switch_drop_output.txt"
# Backward compatibility with old naming style: {id}_*.txt
if [[ ! -f "${SND_RCV_FILE}" ]]; then
    SND_RCV_FILE="${OUTPUT_DIR}/${CONFIG_ID}_snd_rcv_record_file.txt"
fi
if [[ ! -f "${SWITCH_DROP_FILE}" ]]; then
    SWITCH_DROP_FILE="${OUTPUT_DIR}/${CONFIG_ID}_switch_drop_output.txt"
fi

python3 run.py --lb fecmp --pfc ${PFC} --irn ${IRN} --my_switch_total_drop_rate ${DROP_RATE} --rate_bound ${RATE_BOUND} --simul_time ${RUNTIME} --netload ${NETLOAD} --topo ${TOPOLOGY} # 2>&1 # > /dev/null
tail -n 5 "${SND_RCV_FILE}"
# python3 analysis_switch_drop.py "${SWITCH_DROP_FILE}"
sleep 0.1

# # IRN RDMA
# cecho "GREEN" "Run IRN single flow experiments..."
# RATE_BOUND="0"
# HAS_WIN="1"
# SELF_DEFINE_WIN="1"
# SELF_WIN_BYTES="50000000"
# FLOW_NAME="omniDMA_flow"
# PFC="0"
# IRN="1"
# OMNIDMA="0"
# DROP_RATE="0.05"
# CONFIG_ID="$(build_config_id "${TOPOLOGY}" "${FLOW_NAME}" "${DROP_RATE}" "${PFC}" "${IRN}" "${OMNIDMA}")"
# OUTPUT_DIR="mix/output/${CONFIG_ID}"
# SND_RCV_FILE="${OUTPUT_DIR}/snd_rcv_record_file.txt"
# SWITCH_DROP_FILE="${OUTPUT_DIR}/switch_drop_output.txt"
# if [[ ! -f "${SND_RCV_FILE}" ]]; then SND_RCV_FILE="${OUTPUT_DIR}/${CONFIG_ID}_snd_rcv_record_file.txt"; fi
# if [[ ! -f "${SWITCH_DROP_FILE}" ]]; then SWITCH_DROP_FILE="${OUTPUT_DIR}/${CONFIG_ID}_switch_drop_output.txt"; fi
# python3 run.py --lb fecmp --pfc ${PFC} --irn ${IRN} --my_switch_total_drop_rate ${DROP_RATE} --rate_bound ${RATE_BOUND} --simul_time ${RUNTIME} --netload ${NETLOAD} --topo ${TOPOLOGY} --has_win ${HAS_WIN} --self_define_win ${SELF_DEFINE_WIN} --self_win_bytes ${SELF_WIN_BYTES} 2>&1 # > /dev/null
# # python3 analysis_switch_drop.py "${SWITCH_DROP_FILE}"
# tail -n 5 "${SND_RCV_FILE}"

# OmniDMA
# cecho "GREEN" "Run OmniDMA single flow experiments..."
# HAS_WIN="0"
# SELF_DEFINE_WIN="0"
# SELF_WIN_BYTES="1000000000"
# FLOW_NAME="omniDMA_flow"
# PFC="0"
# IRN="0"
# OMNIDMA="1"
# DROP_RATE="0.05"
# CONFIG_ID="$(build_config_id "${TOPOLOGY}" "${FLOW_NAME}" "${DROP_RATE}" "${PFC}" "${IRN}" "${OMNIDMA}")"
# OUTPUT_DIR="mix/output/${CONFIG_ID}"
# SND_RCV_FILE="${OUTPUT_DIR}/snd_rcv_record_file.txt"
# SWITCH_DROP_FILE="${OUTPUT_DIR}/switch_drop_output.txt"
# if [[ ! -f "${SND_RCV_FILE}" ]]; then SND_RCV_FILE="${OUTPUT_DIR}/${CONFIG_ID}_snd_rcv_record_file.txt"; fi
# if [[ ! -f "${SWITCH_DROP_FILE}" ]]; then SWITCH_DROP_FILE="${OUTPUT_DIR}/${CONFIG_ID}_switch_drop_output.txt"; fi
# python3 run.py --lb fecmp --pfc ${PFC} --irn ${IRN} --omnidma ${OMNIDMA} --my_switch_total_drop_rate ${DROP_RATE} --simul_time ${RUNTIME} --netload ${NETLOAD} --topo ${TOPOLOGY} --has_win ${HAS_WIN} --self_define_win ${SELF_DEFINE_WIN} --self_win_bytes ${SELF_WIN_BYTES} 2>&1 # > /dev/null
# # python3 analysis_switch_drop.py "${SWITCH_DROP_FILE}"
# tail -n 5 "${SND_RCV_FILE}"

cecho "GREEN" "Runing end"
