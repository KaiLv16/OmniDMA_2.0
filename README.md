# NS-3 Simulator for OmniDMA

This is a Github repository for the unpublished paper "OmniDMA: Scalable RDMA Transport over WAN". 

We use the open-source NS3 implementation of IRN from SIGCOMM'23 paper "[Network Load Balancing with In-network Reordering Support for RDMA](https://doi.org/10.1145/3603269.3604849)".


One can run this repository by following the same method as described below. Note that we only run the code under `ubuntu:20.04`, docker is not tried yet, but it should be OK. 

We describe how to run this repository either on docker or using your local machine with `ubuntu:20.04`. 
Hint: when configuring NS3, use the `--build-profile=optimized` flag! (detailed below.)
## 1. Simulation

You can reproduce the simulation results by running the script:
```shell
./autorun.sh
```

### Topology  

The topology file used for testing is located at `config/topo_simple_dumbbell_OS2.txt`, and the traffic file is at `config/omniDMA_flow.txt`. Currently, these two files are configured for a point-to-point connection with a single switch in between, transmitting only one flow.  

### Parameter Adjustment  

You can enable OmniDMA by modifying `autorun.sh` with `--omnidma 1`. In fact, you can check how GBN, IRN, and OmniDMA are invoked in the script (the first two are commented out). Additionally, you can set the link packet loss rate using `--my_switch_total_drop_rate 0.05`.  

For OmniDMA, both `HAS_WIN` and `SELF_DEFINE_WIN` should be set to `0`, meaning no window is needed. However, for IRN, these two parameters should be set to `1`, and the window size can be configured via `SELF_WIN_BYTES`.  

### Related Files  

For traffic pattern generation, please refer to the experimental setup section in our paper. Packet loss handling at switch ports is implemented in the `src/point-to-point/model/switch-packet-dropper` class.  

The format definition of `adamap` can be found in `src/point-to-point/model/adamap.h`.  

The behavior modeling of `adamap` at both the sender and receiver ends is available in `src/point-to-point/model/adamap-sender.h/cc` and `src/point-to-point/model/adamap-receiver.h/cc`.  

Note that OmniDMA is closely related to RDMA hardware. Therefore, you should also refer to our modifications in `src/point-to-point/model/rdma-hw.h/cc`.  

We have logged various statistics for Go-back-N, IRN, and OmniDMA. You can find them in the corresponding folders under `mix/output/*`.  


## 2. Run

### Run with Docker

#### Docker Engine
For Ubuntu, following the installation guide [here](https://docs.docker.com/engine/install/ubuntu/) and make sure to apply the necessary post-install [steps](https://docs.docker.com/engine/install/linux-postinstall/).
Eventually, you should be able to launch the `hello-world` Docker container without the `sudo` command: `docker run hello-world`.

#### 0. Prerequisites
First, you do all these:

```shell
wget https://www.nsnam.org/releases/ns-allinone-3.19.tar.bz2
tar -xvf ns-allinone-3.19.tar.bz2
cd ns-allinone-3.19
rm -rf ns-3.19
git clone https://github.com/conweave-project/conweave-ns3.git ns-3.19
```

#### 1. Create a Dockerfile
Here, `ns-allinone-3.19` will be your root directory.

Create a Dockerfile at the root directory with the following:
```shell
FROM ubuntu:20.04
ARG DEBIAN_FRONTEND=noninteractive

RUN apt update && apt install -y gnuplot python python3 python3-pip build-essential libgtk-3-0 bzip2 wget git && rm -rf /var/lib/apt/lists/* && pip3 install numpy matplotlib cycler
WORKDIR /root
```

Then, you do this: 
```shell
docker build -t cw-sim:sigcomm23ae .
```

Once the container is built, do this from the root directory:
```shell
docker run -it -v $(pwd):/root cw-sim:sigcomm23ae bash -c "cd ns-3.19; ./waf configure --build-profile=optimized; ./waf"
```

This should build everything necessary for the simulator.

#### 2. Run
One can always just run the container: 
```shell
docker run -it --name cw-sim -v $(pwd):/root cw-sim:sigcomm23ae 
cd ns-3.19;
./autorun.sh
```

That will run `0.1 second` simulation of 8 experiments which are a part of Figure 12 and 13 in the paper.
In the script, you can easily change the network load (e.g., `50%`), runtime (e.g., `0.1s`), or topology (e.g., `leaf-spine`).
To plot the FCT graph, see below or refer to the script `./analysis/plot_fct.py`.
To plot the Queue Usage graph, see below or refer to the script `./analysis/plot_queue.py`.

:exclamation: **To run processes in background**, use the commands:
```shell
docker run -dit --name cw-sim -v $(pwd):/root cw-sim:sigcomm23ae 
docker exec -it cw-sim /bin/bash

root@252578ceff68:~# cd ns-3.19/
root@252578ceff68:~/ns-3.19# ./autorun.sh
Running RDMA Network Load Balancing Simulations (leaf-spine topology)

----------------------------------
TOPOLOGY: leaf_spine_128_100G_OS2
NETWORK LOAD: 50
TIME: 0.1
----------------------------------

Run Lossless RDMA experiments...
Run IRN RDMA experiments...
Runing all in parallel. Check the processors running on background!
root@252578ceff68:~/ns-3.19# exit
exit
```


---

### Run NS-3 on Ubuntu 20.04
#### 0. Prerequisites
We tested the simulator on Ubuntu 20.04, but latest versions of Ubuntu should also work.
```shell
sudo apt install build-essential python3 libgtk-3-0 bzip2
```
For plotting, we use `numpy`, `matplotlib`, and `cycler` for python3:
```shell
python3 -m pip install numpy matplotlib cycler
```


#### 1. Configure & Build
```shell
wget https://www.nsnam.org/releases/ns-allinone-3.19.tar.bz2
tar -xvf ns-allinone-3.19.tar.bz2
cd ns-allinone-3.19
rm -rf ns-3.19
git clone https://github.com/conweave-project/conweave-ns3.git ns-3.19
cd ns-3.19
./waf configure --build-profile=optimized
./waf
```

## Chinese version

##### Topology
测试的拓扑文件在`config/topo_simple_dumbbell_OS2.txt`，流量文件在`config/omniDMA_flow.txt`。当前，这两个文件配置为一个点对点的连接，中间用一个交换机相连。仅发送一条流。

##### 参数调整

您可以通过调整`autorun.sh`中的`--omnidma 1`来开启omnidma。事实上，您可以通过查看该脚本中对GBN、IRN、OmniDMA的调用方式（前二者处于注释状态）。您也可以通过调整`--my_switch_total_drop_rate 0.05`来设置链路丢包率。

对于OmniDMA，您需要将`HAS_WIN`和`SELF_DEFINE_WIN`参数都设置为0，意味着我们不需要窗口。但是对于IRN，这两项应该被设置为1，并且可以通过调整`SELF_WIN_BYTES`来设置窗口大小。


##### 相关文件

流量模式的生成请参阅我们论文中的实验设置一节，我们在交换机的端口上对数据包进行了丢包处理，详见`src/point-to-point/model/switch-packet-dropper`类。

adamap的格式定义见`src/point-to-point/model/adamap.h`

adamap在收发双端的behavior建模，请见`src/point-to-point/model/adamap-sender.h/cc`和`src/point-to-point/model/adamap-receiver.h/cc`。

注意，OmniDMA的实验和RDMA硬件紧密相关，因此，您也应该参考我们在`src/point-to-point/model/rdma-hw.h/cc`中所做的改动。

我们为Go-back-N、IRN和OmniDMA都打印了一些统计信息，请见`mix/output/*`下对应的文件夹。



##### Clean up
To clean all data of previous simulation results, you can run the command:
```shell
./cleanup.sh
```

## Credit
This code repository is based on [https://github.com/alibaba-edu/High-Precision-Congestion-Control](https://github.com/alibaba-edu/High-Precision-Congestion-Control) for Mellanox Connect-X based RDMA-enabled NIC implementation, and [https://github.com/kaist-ina/ns3-tlt-rdma-public.git](https://github.com/kaist-ina/ns3-tlt-rdma-public.git) for Broadcom switch's shared buffer model and IRN implementation.

```
MIT License

Copyright (c) 2023 National University of Singapore

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
