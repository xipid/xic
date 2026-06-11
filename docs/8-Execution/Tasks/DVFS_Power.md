# Dynamic Voltage and Frequency Scaling (DVFS) and Power Governance

Energy efficiency and thermal management are critical in embedded and resource-constrained environments. The xic scheduler includes a proactive power governance mechanism that leverages Dynamic Voltage and Frequency Scaling (DVFS).

## Proactive vs. Reactive Scaling

Traditional operating systems use reactive frequency scaling: they sample CPU utilization over a historical window (e.g., the last 100ms) and scale the frequency up if utilization exceeds a threshold. This approach introduces latency and can cause performance degradation during sudden compute spikes.

xic uses a proactive scheduling model. Because tasks have defined execution quotas per scheduling period, the scheduler knows the exact compute demand of the active run queue *before* execution begins. This allows the scheduler to adjust CPU frequency and voltage limits immediately.

## Frequency Calculation and Proposing Logic

Every CPU core tracks its active scheduling state via the `CoreState` structure. This includes:
- `totalQuotaUs`: The cumulative sum of execution quotas of all tasks assigned to the core.
- `minFreq` and `maxFreq`: The frequency limits configured for the core.
- `currentProposedFreq`: The last proposed frequency.

When the scheduler runs, it evaluates the active demand against the scheduling period (typically 10,000 microseconds):

1. **Zero Demand**: If there are no tasks in the core's run queue (idle state), the proposed frequency is immediately scaled down to the core's minimum limit:
   $$f_{\text{proposed}} = f_{\text{min}}$$

2. **Full Demand**: If the cumulative task quotas (`totalQuotaUs`) meet or exceed the scheduling period duration, the core is running at maximum load. The proposed frequency is set to the maximum limit:
   $$f_{\text{proposed}} = f_{\text{max}}$$

3. **Interpolated Demand**: If the demand is between zero and full capacity, the scheduler linearly interpolates the required frequency:
   $$f_{\text{proposed}} = f_{\text{min}} + \frac{D \times (f_{\text{max}} - f_{\text{min}})}{P}$$
   Where:
   - $D$ is the cumulative demand (`totalQuotaUs`) in microseconds.
   - $P$ is the scheduling period duration in microseconds.
   - $f_{\text{min}}$ and $f_{\text{max}}$ are the frequency bounds.

## Hardware Governor Integration

Once the new frequency is proposed, the scheduler checks if it differs from `currentProposedFreq`. If a change is required, the scheduler invokes the registered `onChangeFrequency` callback. 

This callback interface bridges the scheduler and the target hardware platform's power management drivers:
- **On Linux**: The callback can interface with the `/sys/devices/system/cpu/cpu*/cpufreq/` interfaces to set the scaling governor or frequencies.
- **On ESP32**: The callback interfaces with the Power Management (PM) lock and clock configuration APIs (`rtc_clk_cpu_freq_set_config`) to scale the CPU clock (e.g., between 80MHz and 240MHz) and adjust internal voltages dynamically.
