#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./flush_size.png"

set xlabel "#Threads"
set ylabel "Throughput (Msg/Sec)"

set format y "%'.0f"

set title "Flush Size-Policy Message Throughput"

plot "./bench_data/elog_bench_size64_msg.csv" using 1:2 title "Flush Size=64" with linespoints, \
     "./bench_data/elog_bench_size_1kb_msg.csv" using 1:2 title "Flush Size=1kb" with linespoints, \
     "./bench_data/elog_bench_size_4kb_msg.csv" using 1:2 title "Flush Size=4kb" with linespoints, \
     "./bench_data/elog_bench_size_64kb_msg.csv" using 1:2 title "Flush Size=64kb" with linespoints, \
     "./bench_data/elog_bench_size_1mb_msg.csv" using 1:2 title "Flush Size=1mb" with linespoints
