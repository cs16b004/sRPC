
sudo ./bin/server -f config_files/cpu.yml  -f config_files/dpdk.yml -f config_files/host_brooklyn.yml -f config_files/network_brooklyn.yml -f config_files/benchmarks.yml


#perf record .--event=ref-cycles,cpu/cache-misses/ --call-graph fp -b -C 0-10 --delay=10000 -T -d -j any_call /run_s.sh  