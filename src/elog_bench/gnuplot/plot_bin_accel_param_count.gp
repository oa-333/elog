#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./bin_accel_param_count.png"
set key top left

set xlabel "Parameter Count"
set ylabel "Throughput (Msg/Sec)"

set format y "%'.0f"

set title "Binary Logging Acceleration (Parameter Count)"

plot "./bench_data/elog_bench_bin_accel_param_count_normal_msg.csv" using 1:2 title "Normal" with linespoints, \
     "./bench_data/elog_bench_bin_accel_param_count_fmt_msg.csv" using 1:2 title "fmtlib" with linespoints, \
     "./bench_data/elog_bench_bin_accel_param_count_bin_msg.csv" using 1:2 title "Binary" with linespoints, \
     "./bench_data/elog_bench_bin_accel_param_count_bin_cache_msg.csv" using 1:2 title "Binary Auto-Cached" with linespoints, \
     "./bench_data/elog_bench_bin_accel_param_count_bin_pre_cache_msg.csv" using 1:2 title "Binary Pre-Cached" with linespoints
