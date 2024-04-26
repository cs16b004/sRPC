sudo ../build/apps/hello_world/h_server -f ../apps/hello_world/config_files/srpc-server.yml -f ../apps/hello_world/config_files/host_brooklyn.yml  -- -f ../apps/hello_world/config_files/app.yml


#perf record .--event=ref-cycles,cpu/cache-misses/ --call-graph fp -b -C 0-10 --delay=10000 -T -d -j any_call /run_s.sh  