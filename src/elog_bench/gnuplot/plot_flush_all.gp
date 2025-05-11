#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./flush.png"

set xlabel "#Threads"
set ylabel "Throughput (Msg/Sec)"

set format y '%.0f'

set title "Flush Message Throughput"

plot "./bench_data/elog_bench_flush_immediate_msg.csv" using 1:2 title "Flush Immediate" with linespoints, \
     "./bench_data/elog_bench_flush_never_msg.csv" using 1:2 title "Flush Never" with linespoints, \
     "./bench_data/elog_bench_count4096_msg.csv" using 1:2 title "Flush Count=4096" with linespoints, \
     "./bench_data/elog_bench_size_1mb_msg.csv" using 1:2 title "Flush Size=1MB" with linespoints, \
     "./bench_data/elog_bench_time_200ms_msg.csv" using 1:2 title "Flush Time=200ms" with linespoints
