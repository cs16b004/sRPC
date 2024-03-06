sudo ../build/apps/rpc_test/server -f ../apps/rpc_test/config_files/cpu.yml  -f ../apps/rpc_test/config_files/dpdk.yml -f ../apps/rpc_test/config_files/host_brooklyn.yml -f ../apps/rpc_test/config_files/network_brooklyn.yml -f ../apps/rpc_test/config_files/benchmarks.yml


#perf record .--event=ref-cycles,cpu/cache-misses/ --call-graph fp -b -C 0-10 --delay=10000 -T -d -j any_call /run_s.sh  