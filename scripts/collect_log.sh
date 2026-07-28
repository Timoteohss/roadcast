#!/bin/bash
ADB="adb -s 10.10.10.116:5555"
CTL="/data/local/tmp/roadcastctl"
OUTPUT="/tmp/roadcast_log.csv"
DURATION=300
INTERVAL=2

echo "timestamp,BattCurr_raw,ChrgPwr_raw,HVBusVolt,stMode,BattSOC" > "$OUTPUT"
START=$(date +%s)
END=$((START + DURATION))
echo "Coletando por $DURATION segundos..."

get_val() {
    $ADB shell "$CTL --seconds 2 --signal $1" 2>&1 | \
        grep "^signal:" | grep "$1" | head -1 | \
        awk '{for(i=1;i<=NF;i++) if($i ~ /^value=/) { split($i,a,"="); print a[2]; exit }}'
}

while [ $(date +%s) -lt $END ]; do
    TS=$(date '+%Y-%m-%d %H:%M:%S')
    echo "$TS,$(get_val BattCurr),$(get_val ChrgDischrgPwr),$(get_val HVBusVolt),$(get_val stMode),$(get_val BattSOC)" >> "$OUTPUT"
    sleep $INTERVAL
done
echo "Pronto: $OUTPUT"
tail -5 "$OUTPUT"
