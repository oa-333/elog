#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./multi_async_log.png"
set key top left

set xlabel "#Threads"
set ylabel "Throughput (Msg/Sec)"

set format y "%'.0f"

set title "Asynchronous Logging Message Throughput"

plot "./bench_data/elog_bench_multi_quantum_accum_msg.csv" using 1:2 title "Multi-Quantum" with linespoints, \
     "./bench_data/elog_bench_multi_quantum_bin_accum_msg.csv" using 1:2 title "Multi-Quantum Binary" with linespoints, \
     "./bench_data/elog_bench_multi_quantum_bin_auto_cache_accum_msg.csv" using 1:2 title "Multi-Quantum Binary, Auto-Cached" with linespoints, \
     "./bench_data/elog_bench_multi_quantum_bin_pre_cache_accum_msg.csv" using 1:2 title "Multi-Quantum Binary, Pre-Cached " with linespoints
