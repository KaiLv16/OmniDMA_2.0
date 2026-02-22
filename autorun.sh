#!/bin/bash

cecho(){  # source: https://stackoverflow.com/a/53463162/2886168
    RED="\033[0;31m"
    GREEN="\033[0;32m"
    YELLOW="\033[0;33m"
    # ... ADD MORE COLORS
    NC="\033[0m" # No Color

    printf "${!1}${2} ${NC}\n"
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
python3 run.py --lb fecmp --pfc 1 --irn 0 --my_switch_total_drop_rate 0.1 --rate_bound ${RATE_BOUND} --simul_time ${RUNTIME} --netload ${NETLOAD} --topo ${TOPOLOGY} # 2>&1 # > /dev/null
tail -n 5 mix/output/noirn/noirn_snd_rcv_record_file.txt
# python3 analysis_switch_drop.py mix/output/noirn/noirn_switch_drop_output.txt
sleep 0.1

# # IRN RDMA
# cecho "GREEN" "Run IRN single flow experiments..."
# RATE_BOUND="0"
# HAS_WIN="1"
# SELF_DEFINE_WIN="1"
# SELF_WIN_BYTES="50000000"
# python3 run.py --lb fecmp --pfc 0 --irn 1 --my_switch_total_drop_rate 0.05 --rate_bound ${RATE_BOUND} --simul_time ${RUNTIME} --netload ${NETLOAD} --topo ${TOPOLOGY} --has_win ${HAS_WIN} --self_define_win ${SELF_DEFINE_WIN} --self_win_bytes ${SELF_WIN_BYTES} 2>&1 # > /dev/null
# # python3 analysis_switch_drop.py mix/output/irn/irn_switch_drop_output.txt
# tail -n 5 mix/output/irn/irn_snd_rcv_record_file.txt

# OmniDMA
# cecho "GREEN" "Run OmniDMA single flow experiments..."
# HAS_WIN="0"
# SELF_DEFINE_WIN="0"
# SELF_WIN_BYTES="1000000000"
# python3 run.py --lb fecmp --pfc 0 --irn 0 --omnidma 1 --my_switch_total_drop_rate 0.05 --simul_time ${RUNTIME} --netload ${NETLOAD} --topo ${TOPOLOGY} --has_win ${HAS_WIN} --self_define_win ${SELF_DEFINE_WIN} --self_win_bytes ${SELF_WIN_BYTES} 2>&1 # > /dev/null
# # python3 analysis_switch_drop.py mix/output/omnidma/omnidma_switch_drop_output.txt
# tail -n 5 mix/output/omnidma/omnidma_snd_rcv_record_file.txt

cecho "GREEN" "Runing end"

