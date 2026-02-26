# Traffic Generator
This folder includes the scripts for generating traffic.

## Usage

`python traffic_gen.py -h` for help.

Example:
`python traffic_gen.py -c WebSearch_distribution.txt -n 320 -l 0.3 -b 100G -t 0.1` generates traffic according to the web search flow size distribution, for 320 hosts, at 30% network load with 100Gbps host bandwidth for 0.1 seconds.

The generate traffic can be directly used by the simulation.

## Traffic format
The first line is the number of flows.

Each line after that is a flow: `<source host> <dest host> 3 <dest port number> <flow size (bytes)> <start time (seconds)>`

## OmniDMA fixed-pattern flow generator

`gen_omnidma_flows.py` generates OmniDMA experiment flow files directly into `config/` (by default), for two patterns:

- `incast`: sources `0..x-1`, destination `100`
- `dumbbell`: source `i` maps to destination `100+i`

Output file format (OmniDMA use case):

- First line: number of flows
- Each line: `<src> <dst> 3 <flow_size> <start_time_seconds>`

### Generated file names

- Incast: `flow_omni_<x>flows_incast_avg<avg>_var<var>.txt`
- Dumbbell: `flow_omni_<x>flows_dumbbell_avg<avg>_var<var>.txt`

`x` defaults to: `1,2,4,8,16,32,64,100`.

### Usage

Show help:

`python3 gen_omnidma_flows.py -h`

Generate both patterns with default settings (avg=1ms, var=1ms, flow_size=1000000000):

`python3 gen_omnidma_flows.py --mode all`

Generate only incast flows, and set a fixed random seed:

`python3 gen_omnidma_flows.py --mode incast --seed 1`

Generate dumbbell flows with custom parameters:

`python3 gen_omnidma_flows.py --mode dumbbell --avg-ms 2 --var-ms 0.5 --flow-size 4096`

Generate flows near `2.0s` (similar to `config/omniDMA_flow.txt` style):

`python3 gen_omnidma_flows.py --mode all --base-time-s 2.0`

### Parameters

- `--mode {incast,dumbbell,all}`: traffic pattern(s) to generate (default: `all`)
- `--counts`: comma-separated flow counts (default: `1,2,4,8,16,32,64,100`)
- `--avg-ms`: mean start time in ms (default: `1.0`)
- `--var-ms`: start-time variance in ms^2 (default: `1.0`)
- `--flow-size`: flow size in bytes (default: `1000000000`)
- `--base-time-s`: start-time offset in seconds (default: `0.0`)
- `--seed`: random seed (optional)
- `--output-dir`: output directory (default: repo `config/`)

Note: to allow independent control of mean and variance, start times are sampled from a Gamma distribution (instead of a pure exponential/Poisson inter-arrival process).
