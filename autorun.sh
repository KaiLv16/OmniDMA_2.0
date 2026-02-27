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
    local switch_drop_mode="${7:-lossrate}"

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
    echo "${base_config_id}_${topo_tag}_${flow_tag}_dropm${switch_drop_mode}_drop${drop_rate_tag}_pfc${pfc}_irn${irn}"
}

build_output_dir_from_params() {
    local topology="$1"
    local drop_rate_pct="$2"
    local pfc="$3"
    local irn="$4"
    local omnidma="$5"
    local flow_name="$6"
    local switch_drop_mode="${7:-lossrate}"

    local drop_rate
    drop_rate="$(percent_to_drop_rate "${drop_rate_pct}")"

    local config_id
    config_id="$(build_config_id "${topology}" "${flow_name}" "${drop_rate}" "${pfc}" "${irn}" "${omnidma}" "${switch_drop_mode}")"
    echo "mix/output/${config_id}"
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
    local switch_drop_mode="${SWITCH_DROP_MODE:-lossrate}"

    local config_id
    config_id="$(build_config_id "${topology}" "${FLOW_NAME}" "${drop_rate}" "${pfc}" "${irn}" "${omnidma}" "${switch_drop_mode}")"
    local output_dir="mix/output/${config_id}"

    cecho "GREEN" "Run: topo=${topology}, drop_rate=${drop_rate_pct}% (ratio=${drop_rate}), pfc=${pfc}, irn=${irn}, omnidma=${omnidma}"
    local cmd=(python3 run.py --lb fecmp --pfc "${pfc}" --irn "${irn}")
    if [[ "${omnidma}" == "1" ]]; then
        cmd+=(--omnidma "${omnidma}")
    fi
    cmd+=(--switch_drop_mode "${switch_drop_mode}")
    if [[ -n "${SWITCH_DROP_SEQNUM_CONFIG:-}" ]]; then
        cmd+=(--switch_drop_seqnum_config "${SWITCH_DROP_SEQNUM_CONFIG}")
    fi
    if [[ -n "${SWITCH_DROP_TIMESTEP_CONFIG:-}" ]]; then
        cmd+=(--switch_drop_timestep_config "${SWITCH_DROP_TIMESTEP_CONFIG}")
    fi
    cmd+=(--my_switch_total_drop_rate "${drop_rate}" --rate_bound "${RATE_BOUND}" --simul_time "${RUNTIME}" --netload "${NETLOAD}" --topo "${topology}" --flow "${FLOW_NAME}")
    if [[ -n "${RNIC_DMA_BW:-}" ]]; then
        cmd+=(--rnic_dma_bw "${RNIC_DMA_BW}")
    fi
    if [[ -n "${RNIC_DMA_FIXED_LATENCY_NS:-}" ]]; then
        cmd+=(--rnic_dma_fixed_latency_ns "${RNIC_DMA_FIXED_LATENCY_NS}")
    fi
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

plot_case_impl() {
    local topology="$1"
    local drop_rate_pct="$2"
    local pfc="$3"
    local irn="$4"
    local omnidma="$5"

    local switch_drop_mode="${SWITCH_DROP_MODE:-lossrate}"

    local output_dir
    output_dir="$(build_output_dir_from_params "${topology}" "${drop_rate_pct}" "${pfc}" "${irn}" "${omnidma}" "${FLOW_NAME}" "${switch_drop_mode}")"
    local config_id="${output_dir#mix/output/}"
    local snd_rcv_file
    snd_rcv_file="$(resolve_output_file "${output_dir}" "${config_id}" "snd_rcv_record_file")"

    if [[ ! -f "${snd_rcv_file}" ]]; then
        cecho "RED" "Cannot plot: missing snd_rcv_record_file: ${snd_rcv_file}"
        return 1
    fi

    cecho "GREEN" "Plot: base_folder=${output_dir}"
    local plot_cmd=(python3 omni-tests/plot_flow_rate.py --base_folder "${output_dir}" --bucket "${PLOT_BUCKET}")
    if [[ -n "${PLOT_FLOWIDS}" ]]; then
        plot_cmd+=(--flowids "${PLOT_FLOWIDS}")
    fi
    if [[ -n "${PLOT_OUTPUT_SUBDIR}" ]]; then
        plot_cmd+=(--output_subdir "${PLOT_OUTPUT_SUBDIR}")
    fi
    if [[ -n "${PLOT_X_MIN_US}" ]]; then
        plot_cmd+=(--x_min_us "${PLOT_X_MIN_US}")
    fi
    if [[ -n "${PLOT_X_MAX_US}" ]]; then
        plot_cmd+=(--x_max_us "${PLOT_X_MAX_US}")
    fi
    "${plot_cmd[@]}"
}

plot_output_dir_impl() {
    local output_dir="$1"
    local snd_rcv_file="${output_dir}/snd_rcv_record_file.txt"

    if [[ ! -f "${snd_rcv_file}" ]]; then
        cecho "RED" "Cannot plot: missing snd_rcv_record_file: ${snd_rcv_file}"
        return 1
    fi

    cecho "GREEN" "Plot: base_folder=${output_dir}"
    local plot_cmd=(python3 omni-tests/plot_flow_rate.py --base_folder "${output_dir}" --bucket "${PLOT_BUCKET}")
    if [[ -n "${PLOT_FLOWIDS}" ]]; then
        plot_cmd+=(--flowids "${PLOT_FLOWIDS}")
    fi
    if [[ -n "${PLOT_OUTPUT_SUBDIR}" ]]; then
        plot_cmd+=(--output_subdir "${PLOT_OUTPUT_SUBDIR}")
    fi
    if [[ -n "${PLOT_X_MIN_US}" ]]; then
        plot_cmd+=(--x_min_us "${PLOT_X_MIN_US}")
    fi
    if [[ -n "${PLOT_X_MAX_US}" ]]; then
        plot_cmd+=(--x_max_us "${PLOT_X_MAX_US}")
    fi
    "${plot_cmd[@]}"
}

load_omnidma_case_profile() {
    local topology_arg="${1:-topo_simple_dumbbell_OS2_500us}"
    local drop_rate_pct_arg="${2:-0.1}"
    local flow_name_arg="${3:-omniDMA_flow}"
    local switch_drop_mode_arg="${4:-timestep}"
    local omnidma_cubic_arg="${5:-1}"

    # Manual OmniDMA experiment config (intentionally not using env vars).
    OMNICASE_TOPOLOGY="${topology_arg}"
    OMNICASE_DROP_RATE_PCT="${drop_rate_pct_arg}"
    OMNICASE_PFC="0"
    OMNICASE_IRN="0"
    OMNICASE_OMNIDMA="1"
    OMNICASE_OMNIDMA_CUBIC="${omnidma_cubic_arg}"
    OMNICASE_OMNIDMA_BITMAP_SIZE="16"
    OMNICASE_HAS_WIN="0"
    OMNICASE_SELF_DEFINE_WIN="0"
    OMNICASE_SELF_WIN_BYTES="1000000000"
    OMNICASE_RATE_BOUND="0"
    OMNICASE_RUNTIME="600"
    OMNICASE_NETLOAD="50"
    OMNICASE_FLOW_NAME="${flow_name_arg}"
    OMNICASE_SWITCH_DROP_MODE="${switch_drop_mode_arg}"  # none / lossrate / seqnum / timestep
    OMNICASE_SWITCH_DROP_SEQNUM_CONFIG="config/config_drop_by_seqnum.txt"
    OMNICASE_SWITCH_DROP_TIMESTEP_CONFIG="config/config_drop_by_timestep.txt"
    # Plot x-axis range (relative to first send packet, us). Empty means auto.
    OMNICASE_PLOT_X_MIN_US=""
    OMNICASE_PLOT_X_MAX_US=""
    # Example:
    # OMNICASE_PLOT_X_MIN_US="0"
    # OMNICASE_PLOT_X_MAX_US="5000"
    OMNICASE_OUTPUT_DIR="$(
        build_output_dir_from_params \
            "${OMNICASE_TOPOLOGY}" \
            "${OMNICASE_DROP_RATE_PCT}" \
            "${OMNICASE_PFC}" \
            "${OMNICASE_IRN}" \
            "${OMNICASE_OMNIDMA}" \
            "${OMNICASE_FLOW_NAME}" \
            "${OMNICASE_SWITCH_DROP_MODE}"
    )"
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
    load_omnidma_case_profile "$1" "$2" "$3" "$4" "$5"

    local topology="${OMNICASE_TOPOLOGY}"
    local drop_rate_pct="${OMNICASE_DROP_RATE_PCT}"
    local pfc="${OMNICASE_PFC}"
    local irn="${OMNICASE_IRN}"
    local omnidma="${OMNICASE_OMNIDMA}"
    local omnidma_cubic="${OMNICASE_OMNIDMA_CUBIC}"
    local omnidma_bitmap_size="${OMNICASE_OMNIDMA_BITMAP_SIZE}"
    local has_win="${OMNICASE_HAS_WIN}"
    local self_define_win="${OMNICASE_SELF_DEFINE_WIN}"
    local self_win_bytes="${OMNICASE_SELF_WIN_BYTES}"
    local rate_bound="${OMNICASE_RATE_BOUND}"
    local runtime="${OMNICASE_RUNTIME}"
    local netload="${OMNICASE_NETLOAD}"
    local flow_name="${OMNICASE_FLOW_NAME}"
    local switch_drop_mode="${OMNICASE_SWITCH_DROP_MODE}"
    local switch_drop_seqnum_config="${OMNICASE_SWITCH_DROP_SEQNUM_CONFIG}"
    local switch_drop_timestep_config="${OMNICASE_SWITCH_DROP_TIMESTEP_CONFIG}"

    # Dynamic scoping in bash lets run_case_impl read these locals instead of globals.
    local RATE_BOUND="${rate_bound}"
    local RUNTIME="${runtime}"
    local NETLOAD="${netload}"
    local FLOW_NAME="${flow_name}"
    local SWITCH_DROP_MODE="${switch_drop_mode}"
    local SWITCH_DROP_SEQNUM_CONFIG="${switch_drop_seqnum_config}"
    local SWITCH_DROP_TIMESTEP_CONFIG="${switch_drop_timestep_config}"

    cecho "YELLOW" "OmniDMA analysis folder: ${OMNICASE_OUTPUT_DIR}"
    run_case_impl "${topology}" "${drop_rate_pct}" "${pfc}" "${irn}" "${omnidma}" \
        --omnidma_cubic "${omnidma_cubic}" \
        --omnidma_bitmap_size "${omnidma_bitmap_size}" \
        --has_win "${has_win}" \
        --self_define_win "${self_define_win}" \
        --self_win_bytes "${self_win_bytes}"
}

plot_omnidma_case() {
    load_omnidma_case_profile "$1" "$2" "$3" "$4" "$5"
    # Dynamic scoping: plot_output_dir_impl -> plot_flow_rate.py reads these locals.
    local PLOT_X_MIN_US="${OMNICASE_PLOT_X_MIN_US}"
    local PLOT_X_MAX_US="${OMNICASE_PLOT_X_MAX_US}"
    cecho "YELLOW" "OmniDMA analysis folder: ${OMNICASE_OUTPUT_DIR}"
    cecho "YELLOW" "OmniDMA plot x-range(us): [${PLOT_X_MIN_US:-AUTO}, ${PLOT_X_MAX_US:-AUTO}]"
    plot_output_dir_impl "${OMNICASE_OUTPUT_DIR}"
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
            config_id="$(build_config_id "${TOPOLOGY}" "${FLOW_NAME}" "${drop_rate}" "${pfc}" "${irn}" "${omnidma}" "${SWITCH_DROP_MODE:-lossrate}")"
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
OMNIDMA_CUBIC="${OMNIDMA_CUBIC:-1}"
PLOT_BUCKET="${PLOT_BUCKET:-10}"
PLOT_FLOWIDS="${PLOT_FLOWIDS:-}"
PLOT_OUTPUT_SUBDIR="${PLOT_OUTPUT_SUBDIR:-flow_rate_plots}"
PLOT_X_MIN_US="${PLOT_X_MIN_US:-}"
PLOT_X_MAX_US="${PLOT_X_MAX_US:-}"
RNIC_DMA_BW="${RNIC_DMA_BW:-64Gb/s}"
RNIC_DMA_FIXED_LATENCY_NS="${RNIC_DMA_FIXED_LATENCY_NS:-200}"

# 在这里配置要运行的实验和绘图逻辑
SKIP_FLAG="${1:-12}"
# TOPO_ARG="${2:-topo_simple_dumbbell_O2_500us}"
TOPO_ARG="${2:-topo_dumbbell_incast100_OS2_500us}"
DROP_ARG="${3:-0.1}"
# FLOW_ARG="${4:-omniDMA_flow}"
FLOW_ARG="${4:-flow_omni_1flows_dumbbell_avg1ms_var1ms}"
SWITCH_DROP_MODE_ARG="${5:-seqnum}"  # none / lossrate / seqnum / timestep
OMNIDMA_CUBIC_ARG="${6:-0}"          # 0/1


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
cecho "YELLOW" "OMNIDMA_CUBIC_ARG=${OMNIDMA_CUBIC_ARG}"
cecho "YELLOW" "RNIC_DMA_BW=${RNIC_DMA_BW}, RNIC_DMA_FIXED_LATENCY_NS=${RNIC_DMA_FIXED_LATENCY_NS}"
cecho "YELLOW" "PLOT_BUCKET=${PLOT_BUCKET}, PLOT_FLOWIDS=${PLOT_FLOWIDS:-ALL}, PLOT_OUTPUT_SUBDIR=${PLOT_OUTPUT_SUBDIR}"
cecho "YELLOW" "PLOT_X_MIN_US=${PLOT_X_MIN_US:-AUTO}, PLOT_X_MAX_US=${PLOT_X_MAX_US:-AUTO}"
cecho "YELLOW" "----------------------------------\n"

usage() {
    cat <<'EOF'
Usage:
  ./autorun.sh <skip_flag> [topology] [drop_rate_pct] [flow_name] [switch_drop_mode] [omnidma_cubic]

skip_flag (string flags, can combine):
  contains '1' -> run simulation
  contains '2' -> plot
  examples: 1 / 2 / 12

Defaults:
  topology      = topo_simple_dumbbell_OS2_500us
  drop_rate_pct = 0.1
  flow_name     = omniDMA_flow
  switch_drop_mode = seqnum
  omnidma_cubic = 1

Optional env vars for plotting:
  PLOT_FLOWIDS=1,2,3
  PLOT_BUCKET=10
  PLOT_OUTPUT_SUBDIR=flow_rate_plots
  PLOT_X_MIN_US=0
  PLOT_X_MAX_US=5000
EOF
}



if [[ "${SKIP_FLAG}" != *1* && "${SKIP_FLAG}" != *2* ]]; then
    cecho "RED" "Invalid skip_flag: ${SKIP_FLAG} (must contain '1' and/or '2')"
    usage
    exit 1
fi

cecho "YELLOW" "skip_flag=${SKIP_FLAG} (1=simulate, 2=plot)"
cecho "YELLOW" "TOPO_ARG=${TOPO_ARG}, DROP_ARG=${DROP_ARG}, FLOW_ARG=${FLOW_ARG}, SWITCH_DROP_MODE_ARG=${SWITCH_DROP_MODE_ARG}, OMNIDMA_CUBIC_ARG=${OMNIDMA_CUBIC_ARG}"

if [[ "${SKIP_FLAG}" == *1* ]]; then
    cecho "YELLOW" "Run simulation enabled"
    run_omnidma_case "${TOPO_ARG}" "${DROP_ARG}" "${FLOW_ARG}" "${SWITCH_DROP_MODE_ARG}" "${OMNIDMA_CUBIC_ARG}" || exit $?
fi

if [[ "${SKIP_FLAG}" == *2* ]]; then
    cecho "YELLOW" "Plot enabled"
    plot_omnidma_case "${TOPO_ARG}" "${DROP_ARG}" "${FLOW_ARG}" "${SWITCH_DROP_MODE_ARG}" "${OMNIDMA_CUBIC_ARG}" || exit $?
fi

cecho "GREEN" "Running end"
# IRN / OmniDMA examples:
# run_irn_case
# run_irn_case topo_simple_dumbbell_OS2_500us 0.1
# run_sweep run_irn_case
# merge_irn_sweep_out_fct
#
# run_omnidma_case
# run_omnidma_case topo_simple_dumbbell_OS2_500us 0.1 omniDMA_flow timestep 1
# run_sweep run_omnidma_case
# merge_omnidma_sweep_out_fct
