from pathlib import Path
import pandas as pd
import streamlit as st

st.set_page_config(
    page_title="Panel 3 | GNC Hardware Telemetry",
    page_icon="📡",
    layout="wide"
)

CSV_FILE = Path(__file__).with_name("rtos_wcet_traces_fresh_run.csv")

st.title("Panel 3 – Hardware Telemetry Dashboard")
st.caption("SAARTHI | Real-Time GNC Loop Under Emulation")
st.info("Data source: Fresh Renode ARM Cortex-M4 / FreeRTOS emulation measurements.")

if not CSV_FILE.exists():
    st.error(f"Measurement CSV not found: {CSV_FILE}")
    st.stop()

df = pd.read_csv(CSV_FILE)

required = [
    "task", "nominal_period_ms", "period_min_ms", "period_max_ms",
    "period_peak_to_peak_jitter_us", "execution_min_us",
    "execution_max_us", "measurement_sample_count",
    "activation_period_violations", "deadline_misses"
]

missing = [col for col in required if col not in df.columns]
if missing:
    st.error(f"Missing required CSV columns: {missing}")
    st.stop()

df = df.copy()
df["task"] = df["task"].astype(str)

st.subheader("Task execution overview")

cols = st.columns(len(df))
for col, (_, row) in zip(cols, df.iterrows()):
    with col:
        st.metric(
            label=f"{row['task']} – Max execution",
            value=f"{row['execution_max_us']:.1f} µs",
            delta=f"{row['measurement_sample_count']} samples",
            delta_color="off"
        )

st.divider()

left, right = st.columns(2)

with left:
    st.subheader("Execution latency")
    latency = df.set_index("task")[
        ["execution_min_us", "execution_max_us"]
    ]
    latency.columns = ["Minimum (µs)", "Maximum (µs)"]
    st.bar_chart(latency)
    st.caption("Observed minimum and maximum task execution times.")

with right:
    st.subheader("Task period timing")
    periods = df.set_index("task")[
        ["period_min_ms", "period_max_ms"]
    ]
    periods.columns = ["Minimum period (ms)", "Maximum period (ms)"]
    st.bar_chart(periods)
    st.caption("Measured task activation periods.")

st.divider()

left, right = st.columns(2)

with left:
    st.subheader("Period jitter")
    jitter = df.set_index("task")[["period_peak_to_peak_jitter_us"]]
    jitter.columns = ["Peak-to-peak jitter (µs)"]
    st.bar_chart(jitter)

with right:
    st.subheader("Measurement sample counts")
    samples = df.set_index("task")[["measurement_sample_count"]]
    samples.columns = ["Samples"]
    st.bar_chart(samples)

st.divider()

st.subheader("Task timing and deadline status")

status = df[
    [
        "task", "nominal_period_ms", "period_min_ms", "period_max_ms",
        "period_peak_to_peak_jitter_us", "execution_min_us",
        "execution_max_us", "measurement_sample_count",
        "activation_period_violations", "deadline_misses"
    ]
].copy()

status.columns = [
    "Task", "Target period (ms)", "Minimum period (ms)",
    "Maximum period (ms)", "Jitter (µs)", "Min execution (µs)",
    "Max execution (µs)", "Samples", "Period violations",
    "Deadline misses"
]

st.dataframe(status, use_container_width=True, hide_index=True)

violations = int(df["activation_period_violations"].sum())
misses = int(df["deadline_misses"].sum())

a, b, c = st.columns(3)
a.metric("Total period violations", violations)
b.metric("Total deadline misses", misses)
c.metric("Tasks monitored", len(df))

if violations == 0 and misses == 0:
    st.success("No activation-period violations or deadline misses were recorded in this measurement run.")
else:
    st.warning("One or more timing violations were recorded. Review the task table.")

st.divider()

st.subheader("Measurement notes")
st.write(
    "These values are observed execution and timing measurements from Renode "
    "emulation. Maximum observed execution time is not a formal mathematical WCET proof."
)
st.write(
    "Sensing and Telemetry use placeholder workloads. Communications consumes "
    "GNC telemetry, but its remaining workload is a placeholder."
)
st.write(
    "The timing measurements use a 10 MHz timer (0.1 µs per count) and a "
    "1 kHz FreeRTOS tick (1 ms per tick)."
)

with st.expander("View source measurement data"):
    st.dataframe(df, use_container_width=True, hide_index=True)

st.caption("Panel 3 | SAARTHI Real-Time GNC Project")
