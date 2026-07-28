#!/usr/bin/env python3
"""Snapshot a cada 15s por 5 minutos via adb + roadcastctl."""
import subprocess
import time
import re
import csv
from datetime import datetime

ADB = "adb -s 10.10.10.116:5555"
CTL = "/data/local/tmp/roadcastctl"
OUTPUT = "/tmp/roadcast_log.csv"
DURATION = 300
INTERVAL = 15

def parse(output, pattern):
    for line in output.splitlines():
        if pattern in line and line.startswith("signal:"):
            m = re.search(r'value=(-?[\d.]+)', line)
            if m:
                return m.group(1)
    return ""

def main():
    print(f"Coletando {DURATION//INTERVAL} snapshots a cada {INTERVAL}s...")
    with open(OUTPUT, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["timestamp", "BattCurr_raw", "ChrgPwr_raw", "HVBusVolt", "stMode", "BattSOC", "P_est_kW"])
        
        start = time.time()
        row = 0
        while time.time() - start < DURATION:
            row += 1
            ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            
            out_bms = subprocess.run(
                f"{ADB} shell '{CTL} --seconds 2 --signal BMSH'",
                shell=True, capture_output=True, text=True, timeout=15).stdout
            out_pwr = subprocess.run(
                f"{ADB} shell '{CTL} --seconds 2 --signal ChrgDischrgPwr'",
                shell=True, capture_output=True, text=True, timeout=15).stdout
            
            batt = parse(out_bms, "BMSH_BattCurr")
            pwr = parse(out_pwr, "ChrgDischrgPwrAct")
            hv = parse(out_bms, "BMSH_HVBusVolt")
            st = parse(out_bms, "BMSH_stMode")
            soc = parse(out_bms, "BMSH_BattSOC")
            
            # P_est = V * (5005 - raw_BattCurr) * 0.2
            if batt and hv:
                p_est = float(hv) * (5005 - float(batt)) * 0.2 / 1000.0
            else:
                p_est = ""
            
            writer.writerow([ts, batt, pwr, hv, st, soc, p_est])
            f.flush()
            print(f"  {row}: Batt={batt} Pwr={pwr} HV={hv} SOC={soc} P_est={p_est}kW")
            time.sleep(INTERVAL)
    
    print(f"\nPronto! {row} snapshots em {OUTPUT}")

if __name__ == "__main__":
    main()
