#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./bin_accel_msg_len.png"
set key bottom left

set xlabel "Message Length"
set ylabel "Throughput (Msg/Sec)"

set format y "%'.0f"

set title "Binary Logging Acceleration (Message Length)"

plot "./bench_data/elog_bench_bin_accel_msg_len_normal_msg.csv" using 1:2 title "Normal" with linespoints, \
     "./bench_data/elog_bench_bin_accel_msg_len_fmt_msg.csv" using 1:2 title "fmtlib" with linespoints, \
     "./bench_data/elog_bench_bin_accel_msg_len_bin_msg.csv" using 1:2 title "Binary" with linespoints, \
     "./bench_data/elog_bench_bin_accel_msg_len_bin_cache_msg.csv" using 1:2 title "Binary Auto-Cached" with linespoints, \
     "./bench_data/elog_bench_bin_accel_msg_len_bin_pre_cache_msg.csv" using 1:2 title "Binary Pre-Cached" with linespoints
