#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./file_rotating.png"

set xlabel "#Threads"
set ylabel "Throughput (Msg/Sec)"

set format y "%'.0f"

set title "Rotating File Target Message Throughput"

plot "./bench_data/elog_bench_rotating_1mb_msg.csv" using 1:2 title "Segment Size = 1 MB" with linespoints, \
     "./bench_data/elog_bench_rotating_2mb_msg.csv" using 1:2 title "Segment Size = 2 MB" with linespoints, \
     "./bench_data/elog_bench_rotating_4mb_msg.csv" using 1:2 title "Segment Size = 4 MB" with linespoints
