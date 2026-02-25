#!/usr/bin/python3
from genericpath import exists
import subprocess
import os
import time
from xmlrpc.client import boolean
import numpy as np
import copy
import shutil
import random
from datetime import datetime
import sys
import os
import argparse
from datetime import date

# randomID
random.seed(datetime.now())
MAX_RAND_RANGE = 1000000000

# config template
config_template = """TOPOLOGY_FILE config/{topo}.txt
FLOW_FILE config/{flow}.txt

OMNIDMA_EVENT_OUTPUT_FILE mix/output/{id}/omniDMA_event_output.txt
SWITCH_DROP_OUTPUT_FILE mix/output/{id}/switch_drop_output.txt
FLOW_INPUT_FILE mix/output/{id}/in.txt
CNP_OUTPUT_FILE mix/output/{id}/out_cnp.txt
FCT_OUTPUT_FILE mix/output/{id}/out_fct.txt
PFC_OUTPUT_FILE mix/output/{id}/out_pfc.txt
QLEN_MON_FILE mix/output/{id}/out_qlen.txt
VOQ_MON_FILE mix/output/{id}/out_voq.txt
VOQ_MON_DETAIL_FILE mix/output/{id}/out_voq_per_dst.txt
UPLINK_MON_FILE mix/output/{id}/out_uplink.txt
CONN_MON_FILE mix/output/{id}/out_conn.txt
EST_ERROR_MON_FILE mix/output/{id}/out_est_error.txt

SND_RCV_OUTPUT_FILE mix/output/{id}/snd_rcv_record_file.txt

QLEN_MON_START {qlen_mon_start}
QLEN_MON_END {qlen_mon_end}
SW_MONITORING_INTERVAL {sw_monitoring_interval}
OMNI_MONITORING_INTERVAL {omni_mon_interval}

FLOWGEN_START_TIME {flowgen_start_time}
FLOWGEN_STOP_TIME {flowgen_stop_time}
BUFFER_SIZE {buffer_size}

CC_MODE {cc_mode}
LB_MODE {lb_mode}
ENABLE_PFC {enabled_pfc}
ENABLE_IRN {enabled_irn}

CONWEAVE_TX_EXPIRY_TIME {cwh_tx_expiry_time}
CONWEAVE_REPLY_TIMEOUT_EXTRA {cwh_extra_reply_deadline}
CONWEAVE_PATH_PAUSE_TIME {cwh_path_pause_time}
CONWEAVE_EXTRA_VOQ_FLUSH_TIME {cwh_extra_voq_flush_time}
CONWEAVE_DEFAULT_VOQ_WAITING_TIME {cwh_default_voq_waiting_time}

ALPHA_RESUME_INTERVAL 1
RATE_DECREASE_INTERVAL 4
CLAMP_TARGET_RATE 0
RP_TIMER 300 
FAST_RECOVERY_TIMES 1
EWMA_GAIN {ewma_gain}
RATE_AI {ai}Mb/s
RATE_HAI {hai}Mb/s
MIN_RATE 100Mb/s
DCTCP_RATE_AI {dctcp_ai}Mb/s

ERROR_RATE_PER_LINK 0.0000
L2_CHUNK_SIZE 4000
L2_ACK_INTERVAL 1
L2_BACK_TO_ZERO 0

RATE_BOUND {rate_bound}
HAS_WIN {has_win}
VAR_WIN {var_win}

SELF_WIN_BYTES {self_win_bytes}
SELF_DEFINE_WIN {self_define_win}

FAST_REACT {fast_react}
MI_THRESH {mi}
INT_MULTI {int_multi}
GLOBAL_T 1
U_TARGET 0.95
MULTI_RATE 0
SAMPLE_FEEDBACK 0

ENABLE_QCN 1
USE_DYNAMIC_PFC_THRESHOLD 1
PACKET_PAYLOAD_SIZE 1000


LINK_DOWN 0 0 0
KMAX_MAP {kmax_map}
KMIN_MAP {kmin_map}
PMAX_MAP {pmax_map}
LOAD {load}
RANDOM_SEED 1

ENABLE_OMNIDMA {enabled_omnidma}
ENABLE_OMNIDMA_CUBIC {enabled_omnidma_cubic}
OMNIDMA_BITMAP_SIZE {omnidma_bitmap_size}
MY_SWITCH_TOTAL_DROP_RATE {my_switch_total_drop_rate}
OMNIDMA_TX_EXPIRY_TIME 1000]
OMNIDMA_REPLY_TIMEOUT_EXTRA 4
"""


# LB/CC mode matching
cc_modes = {
    "dcqcn": 1,
    "hpcc": 3,
    "timely": 7,
    "dctcp": 8,
}

lb_modes = {
    "fecmp": 0,
    "drill": 2,
    "conga": 3,
    "letflow": 6,
    "conweave": 9,
}

# 这里是用RTT算的
topo2bdp = {
    "leaf_spine_128_100G_OS2": 104000,  # 2-tier -> all 100Gbps
    "fat_k8_100G_OS2": 156000,  # 3-tier -> all 100Gbps
    "topo_simple_dumbbell_OS2": 500002000,  # 单向有2个10ms，中间有1个交换机，(2 * 20 * 1000000 + 80 * 4) * 12.5.
}
    # 104000 / 8320 = 12.5
    # topo2bdpMap[std::string("leaf_spine_128_100G_OS2")] = 104000;  // RTT=8320
    # topo2bdpMap[std::string("fat_k8_100G_OS2")] = 156000;      // RTT=12480 --> all 100G links

FLOWGEN_DEFAULT_TIME = 2.0  # see /traffic_gen/traffic_gen.py::base_t


def main():
    # make directory if not exists
    isExist = os.path.exists(os.getcwd() + "/mix/output/")
    if not isExist:
        os.makedirs(os.getcwd() + "/mix/output/")
        print("The new directory is created - {}".format(os.getcwd() + "/mix/output/"))

    parser = argparse.ArgumentParser(description='run simulation')
    parser.add_argument('--cc', dest='cc', action='store',
                        default='dcqcn', help="hpcc/dcqcn/timely/dctcp (default: dcqcn)")
    parser.add_argument('--lb', dest='lb', action='store',
                        default='fecmp', help="fecmp/pecmp/drill/conga (default: fecmp)")
    parser.add_argument('--pfc', dest='pfc', action='store',
                        type=int, default=1, help="enable PFC (default: 1)")
    parser.add_argument('--irn', dest='irn', action='store',
                        type=int, default=0, help="enable IRN (default: 0)")
    parser.add_argument('--omnidma', dest='omnidma', action='store',
                        type=int, default=0, help="enable omnidma (default: 0)")
    parser.add_argument('--omnidma_cubic', dest='omnidma_cubic', action='store',
                        type=int, default=0, help="enable OmniDMA CUBIC sender window control (default: 0)")
    parser.add_argument('--omnidma_bitmap_size', dest='omnidma_bitmap_size', action='store',
                        type=int, default=16, help="OmniDMA Adamap bitmap size (1..256, default: 16)")
    parser.add_argument('--simul_time', dest='simul_time', action='store',
                        default='0.1', help="traffic time to simulate (up to 3 seconds) (default: 0.1)")
    parser.add_argument('--buffer', dest="buffer", action='store',
                        default='9', help="the switch buffer size (MB) (default: 9)")
    parser.add_argument('--netload', dest='netload', action='store', type=int,
                        default=40, help="Network load at NIC to generate traffic (default: 40.0)")
    parser.add_argument('--bw', dest="bw", action='store',
                        default='100', help="the NIC bandwidth (Gbps) (default: 100)")
    parser.add_argument('--topo', dest='topo', action='store',
                        default='leaf_spine_128_100G', help="the name of the topology file (default: leaf_spine_128_100G_OS2)")
    parser.add_argument('--cdf', dest='cdf', action='store',
                        default='AliStorage2019', help="the name of the cdf file (default: AliStorage2019)")
    parser.add_argument('--enforce_win', dest='enforce_win', action='store',
                        type=int, default=0, help="enforce to use window scheme (default: 0)")
    parser.add_argument('--has_win', dest='has_win', action='store',
                        type=int, default=0, help="whether use win (default: 0)")
    parser.add_argument('--rate_bound', dest='rate_bound', action='store',
                        type=int, default=0, help="whether use rate bound (default: 0)")
    parser.add_argument('--self_define_win', dest='self_define_win', action='store',
                        type=int, default=0, help="whether use self defined win (default: 0)")
    parser.add_argument('--self_win_bytes', dest='self_win_bytes', action='store',
                        type=int, default=0, help="the window size in Bytes of self defined win (default: 0)")
    parser.add_argument('--sw_monitoring_interval', dest='sw_monitoring_interval', action='store',
                        type=int, default=10000, help="interval of sampling statistics for queue status (default: 10000ns)")
    parser.add_argument('--omni_mon_interval', dest='omni_mon_interval', action='store',
                        type=int, default=100000, help="interval of OmniDMA memory/RNIC DMA stats sampling (default: 100000ns = 100us)")
    parser.add_argument('--my_switch_total_drop_rate', dest='my_switch_total_drop_rate', action='store',
                        type=float, default=0.0, help="total drop rate of our switch (default: 0.0)")

    # #### CONWEAVE PARAMETERS ####
    # parser.add_argument('--cwh_extra_reply_deadline', dest='cwh_extra_reply_deadline', action='store',
    #                     type=int, default=4, help="extra-timeout, where reply_deadline = base-RTT + extra-timeout (default: 4us)")
    # parser.add_argument('--cwh_path_pause_time', dest='cwh_path_pause_time', action='store',
    #                     type=int, default=16, help="Time to pause the path with ECN feedback (default: 8us")
    # parser.add_argument('--cwh_extra_voq_flush_time', dest='cwh_extra_voq_flush_time', action='store',
    #                     type=int, default=16, help="Extra VOQ Flush Time (default: 8us for IRN)")
    # parser.add_argument('--cwh_default_voq_waiting_time', dest='cwh_default_voq_waiting_time', action='store',
    #                     type=int, default=400, help="Default VOQ Waiting Time (default: 400us)")
    # parser.add_argument('--cwh_tx_expiry_time', dest='cwh_tx_expiry_time', action='store',
    #                     type=int, default=1000, help="timeout value of ConWeave Tx for CLEAR signal (default: 1000us)")

    args = parser.parse_args()

    # make running ID of this config (base part keeps previous behavior)
    base_config_id = "GBN"


    # input parameters
    cc_mode = cc_modes[args.cc]
    lb_mode = lb_modes[args.lb]
    enabled_pfc = int(args.pfc)
    enabled_irn = int(args.irn)
    enabled_omnidma = int(args.omnidma)
    enabled_omnidma_cubic = int(args.omnidma_cubic)
    omnidma_bitmap_size = int(args.omnidma_bitmap_size)
    bw = int(args.bw)
    buffer = args.buffer
    topo = args.topo
    rate_bound = args.rate_bound
    has_win = args.has_win
    enforce_win = args.enforce_win
    self_define_win = args.self_define_win
    self_win_bytes = args.self_win_bytes
    my_switch_total_drop_rate = args.my_switch_total_drop_rate
    cdf = args.cdf
    flowgen_start_time = FLOWGEN_DEFAULT_TIME  # default: 2.0
    flowgen_stop_time = flowgen_start_time + float(args.simul_time)  # default: 2.0
    sw_monitoring_interval = int(args.sw_monitoring_interval)
    omni_mon_interval = int(args.omni_mon_interval)

    # get over-subscription ratio from topoogy name

    netload = args.netload
    print(topo)
    topo_suffix = topo.replace("\n", "").split("OS")[-1].replace(".txt", "")
    oversub_str = ""
    for ch in topo_suffix:
        if ch.isdigit():
            oversub_str += ch
        else:
            break
    if not oversub_str:
        raise ValueError(f"Cannot parse oversubscription ratio from topology name: {topo}")
    oversub = int(oversub_str)
    assert (int(args.netload) % oversub == 0)
    hostload = int(args.netload) / oversub
    assert (hostload > 0)

    if enabled_irn == 1:
        base_config_id = "irn"
    
    if enabled_omnidma == 1:
        base_config_id = "omnidma"

    # Sanity checks
    if (args.cc == "timely" or args.cc == "hpcc") and args.lb == "conweave":
        raise Exception(
            "CONFIG ERROR : ConWeave currently does not support RTT-based protocols. Plz modify its logic accordingly.")
    if enabled_irn == 1 and enabled_pfc == 1:
        raise Exception(
            "CONFIG ERROR : If IRN is turn-on, then you should turn off PFC (for better perforamnce).")
    if enabled_irn == 0 and enabled_pfc == 0 and enabled_omnidma == 0:
        raise Exception(
            "CONFIG ERROR : When OmniDMA is not used, Either IRN or PFC should be true (at least one).")
    if float(args.simul_time) < 0.005:
        raise Exception("CONFIG ERROR : Runtime must be larger than 5ms (= warmup interval).")

    if enabled_pfc + enabled_irn + enabled_omnidma > 1:
        raise Exception("CONFIG ERROR : Only one of PFC, IRN, and OmniDMA can be enabled.")
    if enabled_omnidma_cubic == 1 and enabled_omnidma != 1:
        raise Exception("CONFIG ERROR : OmniDMA CUBIC requires --omnidma 1.")
    if omnidma_bitmap_size <= 0 or omnidma_bitmap_size > 256:
        raise Exception("CONFIG ERROR : --omnidma_bitmap_size must be in [1, 256] (header encodes bitmap in 4x uint64).")
    
    # sniff number of servers
    with open("config/{topo}.txt".format(topo=args.topo), 'r') as f_topo:
        line = f_topo.readline().split(" ")
        n_host = int(line[0]) - int(line[1])

    # assert (hostload >= 0 and hostload < 100)
    flow = "omniDMA_flow"

    topo_tag = topo.replace("/", "-")
    flow_tag = flow.replace("/", "-")
    drop_rate_tag = str(my_switch_total_drop_rate)
    config_ID = f"{base_config_id}_{topo_tag}_{flow_tag}_drop{drop_rate_tag}_pfc{enabled_pfc}_irn{enabled_irn}"

    # check the file exists
    if (exists(os.getcwd() + "/config/" + flow + ".txt")):
        print("Input traffic file {file} already exists".format(file=flow + ".txt"))
    else:
        print("Input traffic file {file} does not exist".format(file=flow + ".txt"))
        return

    ##################################################################
    ##########              ConWeave parameters             ##########
    ##################################################################
    if (lb_mode == 9):
        cwh_extra_reply_deadline = 4  # 4us, NOTE: this is "extra" term to base RTT
        cwh_path_pause_time = 16  # 8us (K_min) or 16us

        if "leaf_spine" in topo:  # 2-tier
            cwh_extra_voq_flush_time = 16
            cwh_default_voq_waiting_time = 200
            cwh_tx_expiry_time = 300  # 300us
        elif "fat" in topo and enabled_pfc == 0 and enabled_irn == 1:  # 3-tier, IRN
            cwh_extra_voq_flush_time = 16
            cwh_default_voq_waiting_time = 300
            cwh_tx_expiry_time = 1000  # 1ms
        elif "fat" in topo and enabled_pfc == 1 and enabled_irn == 0:  # 3-tier, Lossless
            cwh_extra_voq_flush_time = 64
            cwh_default_voq_waiting_time = 600
            cwh_tx_expiry_time = 1000  # 1ms
        else:
            raise Exception(
                "Unsupported ConWeave Parameter Setup")
    else:
        #### CONWEAVE PARAMETERS (DUMMY) ####
        cwh_extra_reply_deadline = 4
        cwh_path_pause_time = 16
        cwh_extra_voq_flush_time = 64
        cwh_default_voq_waiting_time = 400
        cwh_tx_expiry_time = 1000

    ##################################################################

    # make directory if not exists
    isExist = os.path.exists(os.getcwd() + "/mix/output/" + config_ID + "/")
    if not isExist:
        os.makedirs(os.getcwd() + "/mix/output/" + config_ID + "/")
        print("The new directory is created  - {}".format(os.getcwd() +
            "/mix/output/" + config_ID + "/"))
        
    config_name = os.getcwd() + "/mix/output/" + config_ID + "/config.txt"
    print("Config filename:{}".format(config_name))

    # By default, DCQCN uses no window (rate-based).
    var_win = 0
    if (cc_mode == 3 or cc_mode == 8 or enforce_win == 1):  # HPCC or DCTCP or enforcement
        has_win = 1
        var_win = 1
        if enforce_win == 1:
            print("### INFO: Enforced to use window scheme! ###")

    # record to history
    simulday = datetime.now().strftime("%m/%d/%y")
    with open("./mix/.history", "a") as history:
        history.write("{simulday},{config_ID},{cc_mode},{lb_mode},{cwh_tx_expiry_time},{cwh_extra_reply_deadline},{cwh_path_pause_time},{cwh_extra_voq_flush_time},{cwh_default_voq_waiting_time},{pfc},{irn},{has_win},{var_win},{topo},{bw},{cdf},{load},{time},{self_define_win},{self_win_bytes}\n".format(
            simulday=simulday,
            config_ID=config_ID,
            cc_mode=cc_mode,
            lb_mode=lb_mode,
            cwh_tx_expiry_time=cwh_tx_expiry_time,
            cwh_extra_reply_deadline=cwh_extra_reply_deadline,
            cwh_path_pause_time=cwh_path_pause_time,
            cwh_extra_voq_flush_time=cwh_extra_voq_flush_time,
            cwh_default_voq_waiting_time=cwh_default_voq_waiting_time,
            pfc=enabled_pfc,
            irn=enabled_irn,
            has_win=has_win,
            var_win=var_win,
            self_win_bytes=self_win_bytes, 
            self_define_win=self_define_win,
            topo=topo,
            bw=bw,
            cdf=cdf,
            load=netload,
            time=args.simul_time,
        ))

    # 1 BDP calculation
    topo_bdp_key = topo
    if topo_bdp_key not in topo2bdp:
        # Support topology variants such as topo_simple_dumbbell_OS2_1us
        for base_key in topo2bdp:
            if topo.startswith(base_key + "_"):
                topo_bdp_key = base_key
                break
    if topo_bdp_key not in topo2bdp:
        print("ERROR - topology is not registered in run.py!!", flush=True)
        return
    bdp = int(topo2bdp[topo_bdp_key])
    print("1BDP = {}".format(bdp))

    # DCQCN parameters (NOTE: HPCC's 400KB/1600KB is too large, although used in Microsoft)
    kmax_map = "6 %d %d %d %d %d %d %d %d %d %d %d %d" % (
        bw*200000000, 400, bw*500000000, 400, bw*1000000000, 400, bw*2*1000000000, 400, bw*2500000000, 400, bw*4*1000000000, 400)
    kmin_map = "6 %d %d %d %d %d %d %d %d %d %d %d %d" % (
        bw*200000000, 100, bw*500000000, 100, bw*1000000000, 100, bw*2*1000000000, 100, bw*2500000000, 100, bw*4*1000000000, 100)
    pmax_map = "6 %d %d %d %d %d %.2f %d %.2f %d %.2f %d %.2f" % (
        bw*200000000, 0.2, bw*500000000, 0.2, bw*1000000000, 0.2, bw*2*1000000000, 0.2, bw*2500000000, 0.2, bw*4*1000000000, 0.2)

    # queue monitoring
    qlen_mon_start = flowgen_start_time
    qlen_mon_end = flowgen_stop_time

    if (cc_mode == 1):  # DCQCN
        ai = 10 * bw / 25
        hai = 25 * bw / 25
        dctcp_ai = 1000
        fast_react = 0
        mi = 0
        int_multi = 1
        ewma_gain = 0.00390625

        config = config_template.format(id=config_ID, topo=topo, flow=flow,
                                        qlen_mon_start=qlen_mon_start, qlen_mon_end=qlen_mon_end, flowgen_start_time=flowgen_start_time,
                                        flowgen_stop_time=flowgen_stop_time, sw_monitoring_interval=sw_monitoring_interval,
                                        omni_mon_interval=omni_mon_interval,
                                        load=netload, buffer_size=buffer, lb_mode=lb_mode, cwh_tx_expiry_time=cwh_tx_expiry_time,
                                        cwh_extra_reply_deadline=cwh_extra_reply_deadline, cwh_default_voq_waiting_time=cwh_default_voq_waiting_time,
                                        cwh_path_pause_time=cwh_path_pause_time, cwh_extra_voq_flush_time=cwh_extra_voq_flush_time,
                                        enabled_pfc=enabled_pfc, enabled_irn=enabled_irn,
                                        cc_mode=cc_mode,
                                        ai=ai, hai=hai, dctcp_ai=dctcp_ai,
                                        has_win=has_win, var_win=var_win,
                                        enabled_omnidma=enabled_omnidma,
                                        enabled_omnidma_cubic=enabled_omnidma_cubic,
                                        omnidma_bitmap_size=omnidma_bitmap_size,
                                        my_switch_total_drop_rate=my_switch_total_drop_rate,
                                        self_win_bytes=self_win_bytes, self_define_win=self_define_win,
                                        rate_bound=rate_bound,
                                        fast_react=fast_react, mi=mi, int_multi=int_multi, ewma_gain=ewma_gain,
                                        kmax_map=kmax_map, kmin_map=kmin_map, pmax_map=pmax_map)
    else:
        print("unknown cc:{}".format(args.cc))

    with open(config_name, "w") as file:
        file.write(config)

    # run program
    print("Running simulation...")
    output_log = config_name.replace(".txt", ".log")
    run_command = "./waf --run 'scratch/network-load-balance {config_name}' > {output_log} 2>&1".format(
        config_name=config_name, output_log=output_log)
    with open("./mix/.history", "a") as history:
        history.write(run_command + "\n")
        history.write(
            "./waf --run 'scratch/network-load-balance' --command-template='gdb --args %s {config_name}'\n".format(
                config_name=config_name)
        )
        history.write("\n")

    print(run_command)
    os.system("./waf --run 'scratch/network-load-balance {config_name}'  > {output_log} ".format(config_name=config_name, output_log=output_log))
    
    # os.system("./waf --run 'scratch/network-load-balance {config_name}'".format(config_name=config_name))

    ####################################################
    #                 Analyze the output FCT           #
    ####################################################
    # NOTE: collect data except warm-up and cold-finish period
    fct_analysis_time_limit_begin = int(
        flowgen_start_time * 1e9) + int(0.005 * 1e9)  # warmup
    fct_analysistime_limit_end = int(
        flowgen_stop_time * 1e9) + int(0.05 * 1e9)  # extra term

    print("Analyzing output FCT...")
    print("python3 fctAnalysis.py -id {config_ID} -dir {dir} -bdp {bdp} -sT {fct_analysis_time_limit_begin} -fT {fct_analysistime_limit_end} > /dev/null 2>&1".format(
        config_ID=config_ID, dir=os.getcwd(), bdp=bdp, fct_analysis_time_limit_begin=fct_analysis_time_limit_begin, fct_analysistime_limit_end=fct_analysistime_limit_end))
    os.system("python3 fctAnalysis.py -id {config_ID} -dir {dir} -bdp {bdp} -sT {fct_analysis_time_limit_begin} -fT {fct_analysistime_limit_end} > /dev/null 2>&1".format(
        config_ID=config_ID, dir=os.getcwd(), bdp=bdp, fct_analysis_time_limit_begin=fct_analysis_time_limit_begin, fct_analysistime_limit_end=fct_analysistime_limit_end))

    if lb_mode == 9: # ConWeave Logging
        ################################################################
        #             Analyze hardware resource of ConWeave            #
        ################################################################
        # NOTE: collect data except warm-up and cold-finish period
        queue_analysis_time_limit_begin = int(
            flowgen_start_time * 1e9) + int(0.005 * 1e9)  # warmup
        queue_analysistime_limit_end = int(flowgen_stop_time * 1e9)
        print("Analyzing output Queue...")
        print("python3 queueAnalysis.py -id {config_ID} -dir {dir} -sT {queue_analysis_time_limit_begin} -fT {queue_analysistime_limit_end} > /dev/null 2>&1".format(
            config_ID=config_ID, dir=os.getcwd(), queue_analysis_time_limit_begin=queue_analysis_time_limit_begin, queue_analysistime_limit_end=queue_analysistime_limit_end))
        os.system("python3 queueAnalysis.py -id {config_ID} -dir {dir} -sT {queue_analysis_time_limit_begin} -fT {queue_analysistime_limit_end} > /dev/null 2>&1".format(
            config_ID=config_ID, dir=os.getcwd(), queue_analysis_time_limit_begin=queue_analysis_time_limit_begin, queue_analysistime_limit_end=queue_analysistime_limit_end,
            monitoringInterval=sw_monitoring_interval))  # TODO: parameterize

    print("\n\n============== Done ============== ")

    return config_ID


def save_first_xxx_lines(lines, input_file):
    output_file = input_file + '_' + str(lines) + '.txt'
    input_file = input_file + '.txt'
    try:
        with open(input_file, 'r', encoding='utf-8') as infile:
            # 读取前10000行
            lines = [next(infile) for _ in range(lines)]
        
        with open(output_file, 'w', encoding='utf-8') as outfile:
            # 将读取到的行写入新文件
            outfile.writelines(lines)
        
        print(f"Successfully saved the first 10000 lines to {output_file}")
    
    except FileNotFoundError:
        print(f"Error: {input_file} not found.")
    except StopIteration:
        print(f"Warning: {input_file} has less than 10000 lines. Saved available lines to {output_file}.")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    config_ID = main()

    print(f"config_ID: {config_ID}")
    # 调用函数
    if config_ID is not None:
        # save_first_xxx_lines(100, f'mix/output/{config_ID}/snd_rcv_record_file')
        pass
