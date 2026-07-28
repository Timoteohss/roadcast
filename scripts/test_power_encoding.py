#!/usr/bin/env python3
"""
Coleta sinais do Roadcast via ADB e testa hipóteses de codificação de
VCU_ChrgDischrgPwrAct.

Hipóteses comparadas:
  1. offset binary:       P = (u8 - 128) * 0.1
  2. sinal + magnitude:   bit7=direção, low7=magnitude
  3. complemento de dois: P = int8(u8) * 0.1

Referência física:
  I_batt = (5005 - raw_BattCurr) * 0.1
  P_batt = BattVolt * I_batt / 1000

Uso:
  python3 scripts/test_power_encoding.py
  python3 scripts/test_power_encoding.py --duration 300 --interval 15
  python3 scripts/test_power_encoding.py --device 10.10.10.116:5555

Saída:
  /tmp/roadcast_power_encoding.csv
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

DEFAULT_DEVICE = "10.10.10.116:5555"
DEFAULT_ROADCASTCTL = "/data/local/tmp/roadcastctl"
DEFAULT_OUTPUT = "/tmp/roadcast_power_encoding.csv"

SIGNALS = {
    "batt_curr": "BattCurr",
    "charge_power": "ChrgDischrgPwr",
    "hv_bus_volt": "HVBusVolt",
    "batt_volt": "BattVolt",
    "batt_soc": "BattSOC",
    "state_mode": "stMode",
    "charge_enable": "ChargeEnable",
}

NUMBER_RE = re.compile(r"[-+]?(?:0x[0-9a-fA-F]+|\d+(?:\.\d+)?)")
KV_RE = re.compile(
    r"\b(?P<key>[A-Za-z_][A-Za-z0-9_]*)=(?P<value>[-+]?(?:0x[0-9a-fA-F]+|\d+(?:\.\d+)?))"
)


@dataclass
class SignalReading:
    query: str
    name: str = ""
    raw: Optional[float] = None
    value: Optional[float] = None
    line: str = ""
    error: str = ""


def parse_number(text: str) -> float:
    text = text.strip()
    if text.lower().startswith("0x"):
        return float(int(text, 16))
    return float(text)


def run_roadcastctl(
    device: str,
    roadcastctl: str,
    signal_query: str,
    seconds: int,
    timeout: int,
) -> SignalReading:
    remote_cmd = f"{roadcastctl} --seconds {seconds} --signal {signal_query}"
    cmd = ["adb", "-s", device, "shell", remote_cmd]

    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return SignalReading(query=signal_query, error="timeout")
    except FileNotFoundError:
        return SignalReading(query=signal_query, error="adb não encontrado")

    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    signal_lines = [line for line in lines if line.startswith("signal:")]

    if not signal_lines:
        tail = " | ".join(lines[-3:])
        return SignalReading(
            query=signal_query,
            error=f"sem linha signal; rc={proc.returncode}; saída={tail}",
        )

    # Em caso de stream, usamos a última leitura disponível.
    line = signal_lines[-1]
    fields = {m.group("key").lower(): m.group("value") for m in KV_RE.finditer(line)}

    reading = SignalReading(query=signal_query, line=line)

    for key in ("name", "signal"):
        if key in fields:
            reading.name = fields[key]
            break

    # Formatos esperados: raw=..., value=...
    # Também tolera aliases comuns.
    for key in ("raw", "raw_value", "rawvalue"):
        if key in fields:
            reading.raw = parse_number(fields[key])
            break

    for key in ("value", "physical", "phys"):
        if key in fields:
            reading.value = parse_number(fields[key])
            break

    # Fallback: procura tokens textuais quando o parser key=value não bastar.
    if reading.raw is None:
        match = re.search(r"\braw(?:_value)?[=:]\s*([-+]?(?:0x[0-9a-fA-F]+|\d+(?:\.\d+)?))", line)
        if match:
            reading.raw = parse_number(match.group(1))

    if reading.value is None:
        match = re.search(r"\bvalue[=:]\s*([-+]?(?:0x[0-9a-fA-F]+|\d+(?:\.\d+)?))", line)
        if match:
            reading.value = parse_number(match.group(1))

    if reading.raw is None and reading.value is None:
        reading.error = f"não foi possível extrair raw/value: {line}"

    return reading


def int8_from_u8(u8: int) -> int:
    return u8 - 256 if u8 >= 128 else u8


def infer_u8(raw: Optional[float], displayed_value: Optional[float]) -> tuple[Optional[int], str]:
    """
    Obtém o byte real.

    Prioridade:
      1. raw entre 128 e 255: usa diretamente.
      2. value compatível com int8*0.1: reconstrói o byte.
      3. raw entre 0 e 127: assume que o Roadcast removeu bit7 e usa raw|0x80.
         Esta terceira opção é marcada como inferida.
    """
    if raw is not None:
        raw_i = int(round(raw))
        if 128 <= raw_i <= 255:
            return raw_i, "raw_u8"
        if not 0 <= raw_i <= 255:
            return None, "raw_fora_de_8_bits"

    if displayed_value is not None and math.isfinite(displayed_value):
        signed_raw = int(round(displayed_value / 0.1))
        if -128 <= signed_raw <= 127:
            u8 = signed_raw & 0xFF
            return u8, "reconstruído_de_value"

    if raw is not None:
        raw_i = int(round(raw))
        if 0 <= raw_i <= 127:
            return raw_i | 0x80, "raw_low7_mais_bit7"

    return None, "indisponível"


def format_optional(value: Optional[float], digits: int = 3) -> str:
    if value is None or not math.isfinite(value):
        return ""
    return f"{value:.{digits}f}"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Testa a codificação de VCU_ChrgDischrgPwrAct via Roadcast."
    )
    parser.add_argument("--device", default=DEFAULT_DEVICE)
    parser.add_argument("--roadcastctl", default=DEFAULT_ROADCASTCTL)
    parser.add_argument("--duration", type=int, default=300, help="duração total em segundos")
    parser.add_argument("--interval", type=float, default=15.0, help="intervalo entre snapshots")
    parser.add_argument("--seconds", type=int, default=2, help="janela de cada roadcastctl")
    parser.add_argument("--timeout", type=int, default=8, help="timeout de cada comando ADB")
    parser.add_argument("--output", default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--signmag-bit7-positive",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="na hipótese sinal+magnitude, bit7=1 significa potência positiva",
    )
    args = parser.parse_args()

    if args.duration <= 0 or args.interval <= 0:
        parser.error("--duration e --interval devem ser positivos")

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    fieldnames = [
        "timestamp_utc",
        "elapsed_s",
        "batt_curr_raw",
        "batt_curr_value_current",
        "batt_current_est_A",
        "hv_bus_volt_V",
        "batt_volt_V",
        "batt_soc_pct",
        "state_mode",
        "charge_enable",
        "vcu_raw_reported",
        "vcu_value_reported_kW",
        "vcu_u8",
        "vcu_u8_hex",
        "vcu_u8_source",
        "vcu_bit7",
        "vcu_low7",
        "p_offset_binary_kW",
        "p_sign_magnitude_kW",
        "p_twos_complement_kW",
        "p_battery_est_kW",
        "err_offset_binary_kW",
        "err_sign_magnitude_kW",
        "err_twos_complement_kW",
        "best_hypothesis",
        "batt_curr_line",
        "vcu_line",
        "errors",
    ]

    started = time.monotonic()
    next_sample = started
    sample_no = 0

    with output_path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.DictWriter(fp, fieldnames=fieldnames)
        writer.writeheader()
        fp.flush()

        print(f"Coletando por {args.duration}s a cada {args.interval}s")
        print(f"CSV: {output_path}")
        print("Pressione Ctrl+C para encerrar.\n")

        try:
            while True:
                now = time.monotonic()
                elapsed = now - started
                if elapsed > args.duration:
                    break

                if now < next_sample:
                    time.sleep(next_sample - now)

                sample_no += 1
                sample_started = time.monotonic()

                readings = {
                    key: run_roadcastctl(
                        args.device,
                        args.roadcastctl,
                        query,
                        args.seconds,
                        args.timeout,
                    )
                    for key, query in SIGNALS.items()
                }

                batt_curr = readings["batt_curr"]
                vcu = readings["charge_power"]
                hv_bus = readings["hv_bus_volt"]
                batt_volt = readings["batt_volt"]
                batt_soc = readings["batt_soc"]
                state_mode = readings["state_mode"]
                charge_enable = readings["charge_enable"]

                batt_raw = batt_curr.raw
                batt_current_est = None
                if batt_raw is not None:
                    batt_current_est = (5005.0 - batt_raw) * 0.1

                voltage_for_power = batt_volt.value
                if voltage_for_power is None:
                    voltage_for_power = batt_volt.raw
                if voltage_for_power is None:
                    voltage_for_power = hv_bus.value
                if voltage_for_power is None:
                    voltage_for_power = hv_bus.raw

                p_battery = None
                if batt_current_est is not None and voltage_for_power is not None:
                    p_battery = voltage_for_power * batt_current_est / 1000.0

                u8, u8_source = infer_u8(vcu.raw, vcu.value)

                p_offset = p_signmag = p_twos = None
                bit7 = low7 = None
                if u8 is not None:
                    bit7 = (u8 >> 7) & 1
                    low7 = u8 & 0x7F
                    p_offset = (u8 - 128) * 0.1
                    sign = 1 if bit7 else -1
                    if not args.signmag_bit7_positive:
                        sign *= -1
                    p_signmag = sign * low7 * 0.1
                    p_twos = int8_from_u8(u8) * 0.1

                candidates = {
                    "offset_binary": p_offset,
                    "sign_magnitude": p_signmag,
                    "twos_complement": p_twos,
                }

                errors_abs = {}
                if p_battery is not None:
                    for name, value in candidates.items():
                        if value is not None:
                            errors_abs[name] = abs(value - p_battery)

                best = min(errors_abs, key=errors_abs.get) if errors_abs else ""

                def signed_error(value: Optional[float]) -> Optional[float]:
                    if value is None or p_battery is None:
                        return None
                    return value - p_battery

                errors = "; ".join(
                    f"{key}:{reading.error}"
                    for key, reading in readings.items()
                    if reading.error
                )

                row = {
                    "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                    "elapsed_s": f"{time.monotonic() - started:.3f}",
                    "batt_curr_raw": format_optional(batt_raw, 3),
                    "batt_curr_value_current": format_optional(batt_curr.value, 3),
                    "batt_current_est_A": format_optional(batt_current_est, 3),
                    "hv_bus_volt_V": format_optional(hv_bus.value or hv_bus.raw, 3),
                    "batt_volt_V": format_optional(batt_volt.value or batt_volt.raw, 3),
                    "batt_soc_pct": format_optional(batt_soc.value or batt_soc.raw, 3),
                    "state_mode": format_optional(state_mode.value or state_mode.raw, 3),
                    "charge_enable": format_optional(charge_enable.value or charge_enable.raw, 3),
                    "vcu_raw_reported": format_optional(vcu.raw, 3),
                    "vcu_value_reported_kW": format_optional(vcu.value, 3),
                    "vcu_u8": "" if u8 is None else str(u8),
                    "vcu_u8_hex": "" if u8 is None else f"0x{u8:02X}",
                    "vcu_u8_source": u8_source,
                    "vcu_bit7": "" if bit7 is None else str(bit7),
                    "vcu_low7": "" if low7 is None else str(low7),
                    "p_offset_binary_kW": format_optional(p_offset, 3),
                    "p_sign_magnitude_kW": format_optional(p_signmag, 3),
                    "p_twos_complement_kW": format_optional(p_twos, 3),
                    "p_battery_est_kW": format_optional(p_battery, 3),
                    "err_offset_binary_kW": format_optional(signed_error(p_offset), 3),
                    "err_sign_magnitude_kW": format_optional(signed_error(p_signmag), 3),
                    "err_twos_complement_kW": format_optional(signed_error(p_twos), 3),
                    "best_hypothesis": best,
                    "batt_curr_line": batt_curr.line,
                    "vcu_line": vcu.line,
                    "errors": errors,
                }

                writer.writerow(row)
                fp.flush()

                print(
                    f"[{sample_no:03d}] "
                    f"BattRaw={format_optional(batt_raw, 1) or '?'} "
                    f"I={format_optional(batt_current_est, 2) or '?'}A "
                    f"Pbat={format_optional(p_battery, 2) or '?'}kW "
                    f"VCU={'' if u8 is None else f'0x{u8:02X}'} "
                    f"off={format_optional(p_offset, 2) or '?'} "
                    f"sm={format_optional(p_signmag, 2) or '?'} "
                    f"tc={format_optional(p_twos, 2) or '?'} "
                    f"best={best or '?'}"
                )
                if errors:
                    print(f"      avisos: {errors}")

                next_sample = sample_started + args.interval

        except KeyboardInterrupt:
            print("\nColeta interrompida pelo usuário.")

    print(f"\nArquivo salvo em: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
