#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./flush_group.png"

set xlabel "#Threads"
set ylabel "Throughput (Msg/Sec)"

set format y "%'.0f"

set title "Group Flush Policy Message Throughput"

plot "./bench_data/elog_bench_group_4_200ms_msg.csv" using 1:2 title "Group=4,200ms" with linespoints, \
     "./bench_data/elog_bench_group_4_500ms_msg.csv" using 1:2 title "Group=4,500ms" with linespoints, \
     "./bench_data/elog_bench_group_4_1000ms_msg.csv" using 1:2 title "Group=4,1000ms" with linespoints, \
     "./bench_data/elog_bench_group_8_200ms_msg.csv" using 1:2 title "Group=8,200ms" with linespoints, \
     "./bench_data/elog_bench_group_8_500ms_msg.csv" using 1:2 title "Group=8,500ms" with linespoints
