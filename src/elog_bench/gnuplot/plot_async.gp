#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./async_log.png"

set xlabel "#Threads"
set ylabel "Throughput (Msg/Sec)"

set format y '%.0f'

set title "Asynchronous Logging Message Throughput"

plot "./bench_data/elog_bench_deferred_accum_msg.csv" using 1:2 title "Deferred (Flush Count 4096)" with linespoints, \
     "./bench_data/elog_bench_queued_accum_msg.csv" using 1:2 title "Queued 4096 + 200ms (Flush Count 4096)" with linespoints, \
     "./bench_data/elog_bench_quantum_accum_msg.csv" using 1:2 title "Quantum 200000 (Flush Count 4096)" with linespoints, \
     "./bench_data/elog_bench_quantum_shared_accum_msg.csv" using 1:2 title "Quantum 200000 Shared (Flush Count 4096)" with linespoints
