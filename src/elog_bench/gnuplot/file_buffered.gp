#!/ucrt64/bin/gnuplot

reset
set terminal png
set output "./file_buffered.png"

set xlabel "#Threads"
set ylabel "Throughput (Msg/Sec)"

set format y "%'.0f"

set title "Buffered File Target Message Throughput"

plot "./bench_data/elog_bench_buffered512_msg.csv" using 1:2 title "Buffer Size = 512 bytes" with linespoints, \
     "./bench_data/elog_bench_buffered4kb_msg.csv" using 1:2 title "Buffer Size = 4kb" with linespoints, \
     "./bench_data/elog_bench_buffered64kb_msg.csv" using 1:2 title "Buffer Size = 64kb" with linespoints, \
     "./bench_data/elog_bench_buffered1mb_msg.csv" using 1:2 title "Buffer Size = 1mb" with linespoints, \
     "./bench_data/elog_bench_buffered4mb_msg.csv" using 1:2 title "Buffer Size = 4mb" with linespoints
