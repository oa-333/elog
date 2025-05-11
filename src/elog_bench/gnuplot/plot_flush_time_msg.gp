#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./flush_time.png"

set xlabel "#Threads"
set ylabel "Throughput (Msg/Sec)"

set format y '%.0f'

set title "Flush Time-Policy Message Throughput"

plot "./bench_data/elog_bench_time_100ms_msg.csv" using 1:2 title "Flush Time=100ms" with linespoints, \
     "./bench_data/elog_bench_time_200ms_msg.csv" using 1:2 title "Flush Time=200ms" with linespoints, \
     "./bench_data/elog_bench_time_500ms_msg.csv" using 1:2 title "Flush Time=500ms" with linespoints, \
     "./bench_data/elog_bench_time_1000ms_msg.csv" using 1:2 title "Flush Time=1000ms" with linespoints
