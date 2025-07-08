#!/ucrt64/bin/gnuplot

reset
set terminal png size 1200,700
set output "./log_st.png"

set label "Logging Method" at 100,100 center
set ylabel "Throughput (Msg/Sec)"
set margins 15,5,5,5
set border 3
set tics nomirror
set grid ytics

set format y "%'.0f"

set title "Single-threaded Logging Message Throughput"

set boxwidth 0.5
set style fill solid 0.8
plot "bench_data/st_msg.csv" using 1:3:xtic(2) with boxes notitle fillcolor rgb "#658CF5", '' u 1:3:3 with labels notitle offset character 1,1