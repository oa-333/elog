#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./flush_count.png"

set xlabel "#Threads"
set ylabel "Throughput (Msg/Sec)"

set format y '%.0f'

set title "Flush Count-Policy Message Throughput"

plot "./bench_data/elog_bench_count64_msg.csv" using 1:2 title "Flush Count=64" with linespoints, \
     "./bench_data/elog_bench_count256_msg.csv" using 1:2 title "Flush Count=256" with linespoints, \
     "./bench_data/elog_bench_count512_msg.csv" using 1:2 title "Flush Count=512" with linespoints, \
     "./bench_data/elog_bench_count1024_msg.csv" using 1:2 title "Flush Count=1024" with linespoints, \
     "./bench_data/elog_bench_count4096_msg.csv" using 1:2 title "Flush Count=4096" with linespoints
