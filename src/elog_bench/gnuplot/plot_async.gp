#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./async_log.png"
set key top left

set xlabel "#Threads"
set ylabel "Throughput (Msg/Sec)"

set format y "%'.0f"

set title "Asynchronous Logging Message Throughput"

plot "./bench_data/elog_bench_deferred_accum_msg.csv" using 1:2 title "Deferred" with linespoints, \
     "./bench_data/elog_bench_queued_accum_msg.csv" using 1:2 title "Queued" with linespoints, \
     "./bench_data/elog_bench_quantum_accum_msg.csv" using 1:2 title "Quantum" with linespoints, \
     "./bench_data/elog_bench_quantum_shared_accum_msg.csv" using 1:2 title "Quantum Shared" with linespoints, \
     "./bench_data/elog_bench_quantum_bin_accum_msg.csv" using 1:2 title "Quantum Binary" with linespoints, \
     "./bench_data/elog_bench_quantum_bin_auto_cache_accum_msg.csv" using 1:2 title "Quantum Binary, Auto-Cached" with linespoints, \
     "./bench_data/elog_bench_quantum_bin_pre_cache_accum_msg.csv" using 1:2 title "Quantum Binary, Pre-Cached " with linespoints
