#!/bin/bash

cecho(){  # source: https://stackoverflow.com/a/53463162/2886168
    RED="\033[0;31m"
    GREEN="\033[0;32m"
    YELLOW="\033[0;33m"
    NC="\033[0m" # No Color

    printf "%b%s %b\n" "${!1}" "${2}" "${NC}"
}

percent_to_drop_rate() {
    local pct="$1"
    python3 -c 'import sys; print(str(float(sys.argv[1]) / 100.0))' "${pct}"
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

resolve_output_file() {
    local output_dir="$1"
    local config_id="$2"
    local stem="$3"

    local new_path="${output_dir}/${stem}.txt"
    local old_path="${output_dir}/${config_id}_${stem}.txt"
    if [[ -f "${new_path}" ]]; then
        echo "${new_path}"
    else
        echo "${old_path}"
    fi
}

run_case_impl() {
    local topology="$1"
    local drop_rate_pct="$2"
    local pfc="$3"
    local irn="$4"
    local omnidma="$5"
    shift 5

    local drop_rate
    drop_rate="$(percent_to_drop_rate "${drop_rate_pct}")"

    local config_id
    config_id="$(build_config_id "${topology}" "${FLOW_NAME}" "${drop_rate}" "${pfc}" "${irn}" "${omnidma}")"
    local output_dir="mix/output/${config_id}"

    cecho "GREEN" "Run: topo=${topology}, drop_rate=${drop_rate_pct}% (ratio=${drop_rate}), pfc=${pfc}, irn=${irn}, omnidma=${omnidma}"
    local cmd=(python3 run.py --lb fecmp --pfc "${pfc}" --irn "${irn}")
    if [[ "${omnidma}" == "1" ]]; then
        cmd+=(--omnidma "${omnidma}")
    fi
    cmd+=(--my_switch_total_drop_rate "${drop_rate}" --rate_bound "${RATE_BOUND}" --simul_time "${RUNTIME}" --netload "${NETLOAD}" --topo "${topology}")
    if [[ $# -gt 0 ]]; then
        cmd+=("$@")
    fi
    "${cmd[@]}"

    local snd_rcv_file
    snd_rcv_file="$(resolve_output_file "${output_dir}" "${config_id}" "snd_rcv_record_file")"
    local switch_drop_file
    switch_drop_file="$(resolve_output_file "${output_dir}" "${config_id}" "switch_drop_output")"

    if [[ -f "${snd_rcv_file}" ]]; then
        tail -n 5 "${snd_rcv_file}"
    else
        cecho "RED" "Missing output file: ${snd_rcv_file}"
    fi
    # python3 analysis_switch_drop.py "${switch_drop_file}"
    sleep 0.1
}




run_case() {
    local topology="${1:-${DEFAULT_TOPOLOGY}}"
    local drop_rate_pct="${2:-${DEFAULT_DROP_RATE_PCT}}"
    run_case_impl "${topology}" "${drop_rate_pct}" "${PFC}" "${IRN}" "${OMNIDMA}"
}

run_irn_case() {
    local topology="${1:-${DEFAULT_TOPOLOGY}}"
    local drop_rate_pct="${2:-${DEFAULT_DROP_RATE_PCT}}"
    run_case_impl "${topology}" "${drop_rate_pct}" "0" "1" "0" \
        --has_win "${IRN_HAS_WIN}" \
        --self_define_win "${IRN_SELF_DEFINE_WIN}" \
        --self_win_bytes "${IRN_SELF_WIN_BYTES}"
}

run_omnidma_case() {
    local topology="${1:-${DEFAULT_TOPOLOGY}}"
    local drop_rate_pct="${2:-${DEFAULT_DROP_RATE_PCT}}"
    run_case_impl "${topology}" "${drop_rate_pct}" "0" "0" "1" \
        --has_win "${OMNIDMA_HAS_WIN}" \
        --self_define_win "${OMNIDMA_SELF_DEFINE_WIN}" \
        --self_win_bytes "${OMNIDMA_SELF_WIN_BYTES}"
}

run_sweep() {
    local runner="${1:-run_case}"
    for TOPOLOGY in "${TOPOLOGIES[@]}"; do
        for DROP_RATE_PCT in "${DROP_RATE_PCTS[@]}"; do
            "${runner}" "${TOPOLOGY}" "${DROP_RATE_PCT}"
        done
    done
}

merge_sweep_out_fct() {
    local pfc="${1:-${PFC}}"
    local irn="${2:-${IRN}}"
    local omnidma="${3:-${OMNIDMA}}"
    local merge_tag="${4:-pfc${pfc}_irn${irn}_omnidma${omnidma}}"

    local outdir="mix/output/fct_merged"
    local outfile="${outdir}/${merge_tag}_out_fct_prefixed.txt"
    mkdir -p "${outdir}"
    : > "${outfile}"
    printf '# delay drop_rate_pct src_id dst_id src_port dst_port flow_bytes start_time fct standalone_fct\n' >> "${outfile}"

    local found_count=0
    local miss_count=0

    for TOPOLOGY in "${TOPOLOGIES[@]}"; do
        local delay="${TOPOLOGY##*_}"
        for DROP_RATE_PCT in "${DROP_RATE_PCTS[@]}"; do
            local drop_rate
            drop_rate="$(percent_to_drop_rate "${DROP_RATE_PCT}")"
            local config_id
            config_id="$(build_config_id "${TOPOLOGY}" "${FLOW_NAME}" "${drop_rate}" "${pfc}" "${irn}" "${omnidma}")"
            local output_dir="mix/output/${config_id}"
            local fct_file
            fct_file="$(resolve_output_file "${output_dir}" "${config_id}" "out_fct")"

            if [[ -f "${fct_file}" ]]; then
                awk -v delay="${delay}" -v drop_pct="${DROP_RATE_PCT}" 'NF {print delay, drop_pct, $0}' "${fct_file}" >> "${outfile}"
                found_count=$((found_count + 1))
            else
                miss_count=$((miss_count + 1))
            fi
        done
    done

    cecho "YELLOW" "Merged out_fct -> ${outfile} (found=${found_count}, missing=${miss_count})"
}

merge_gbn_sweep_out_fct() {
    merge_sweep_out_fct "1" "0" "0" "GBN_dumbbell_sweep"
}

merge_irn_sweep_out_fct() {
    merge_sweep_out_fct "0" "1" "0" "IRN_dumbbell_sweep"
}

merge_omnidma_sweep_out_fct() {
    merge_sweep_out_fct "0" "0" "1" "OmniDMA_dumbbell_sweep"
}

cecho "GREEN" "Running RDMA Network Load Balancing Simulations (dumbbell topology sweep)"

NETLOAD="50"   # network load 50%
RUNTIME="600"  # simulation time (seconds)
RATE_BOUND="0"
FLOW_NAME="omniDMA_flow"
PFC="1"
IRN="0"
OMNIDMA="0"
DEFAULT_TOPOLOGY="topo_simple_dumbbell_OS2_1us"
DEFAULT_DROP_RATE_PCT="0"
IRN_HAS_WIN="1"
IRN_SELF_DEFINE_WIN="1"
IRN_SELF_WIN_BYTES="50000000"
OMNIDMA_HAS_WIN="0"
OMNIDMA_SELF_DEFINE_WIN="0"
OMNIDMA_SELF_WIN_BYTES="1000000000"

TOPOLOGIES=(
    "topo_simple_dumbbell_OS2_1us"
    "topo_simple_dumbbell_OS2_50us"
    "topo_simple_dumbbell_OS2_500us"
    "topo_simple_dumbbell_OS2_5000us"
    "topo_simple_dumbbell_OS2_15000us"
)

# User-requested percent values; run.py receives the ratio value (pct / 100).
DROP_RATE_PCTS=(
    "0"
    "0.01"
    "0.05"
    "0.1"
    "0.5"
    "1"
    "2"
    "3"
    "5"
)

cecho "YELLOW" "\n----------------------------------"
cecho "YELLOW" "TOPOLOGY count: ${#TOPOLOGIES[@]}"
cecho "YELLOW" "NETWORK LOAD: ${NETLOAD}"
cecho "YELLOW" "TIME: ${RUNTIME}"
cecho "YELLOW" "DROP-RATE count: ${#DROP_RATE_PCTS[@]} (percent list)"
cecho "YELLOW" "PFC=${PFC}, IRN=${IRN}, OMNIDMA=${OMNIDMA}"
cecho "YELLOW" "----------------------------------\n"

# run_sweep run_case
# merge_gbn_sweep_out_fct

cecho "YELLOW" "Run extra OmniDMA case: 1ms RTT (topo=500us one-way), drop_rate=0.1%"
run_omnidma_case topo_simple_dumbbell_OS2_500us 0.1

cecho "GREEN" "Runing end"
# IRN / OmniDMA examples:
# run_irn_case
# run_irn_case topo_simple_dumbbell_OS2_500us 0.1
# run_sweep run_irn_case
# merge_irn_sweep_out_fct
#
# run_omnidma_case
# run_omnidma_case topo_simple_dumbbell_OS2_500us 0.1
# run_sweep run_omnidma_case
# merge_omnidma_sweep_out_fct
