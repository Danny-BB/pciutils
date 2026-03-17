# PR: Add PCIe Diagnostic Mode to lspci

## Summary

This PR introduces a comprehensive **PCIe Diagnostic Mode** (`--diagnose`) to pciutils that performs extensive health checks on PCIe configurations. This mode detects common issues including MPS mismatches, link problems, AER errors, power management configuration, and performance-related settings.

## Problem Statement

PCIe devices have numerous configurable parameters that affect performance, stability, and power consumption. Common issues include:

1. **MPS Mismatches**: Different Max Payload Size settings between link partners
2. **Link Degradation**: Running at reduced speed/width due to signal issues
3. **Link Training Issues**: Training failures, lane errors
4. **Link Quality Issues**: Incomplete equalization (Gen4+)
5. **ASPM Misconfiguration**: Power management causing latency or compatibility issues
6. **AER Errors**: Hardware errors going unnoticed
7. **Power Management**: Suboptimal power states affecting performance

These issues often go unnoticed because they're not easily visible to system administrators.

## Solution

Added `--diagnose` option that performs comprehensive checks:

### Diagnostic Checks

| Check | Description | Severity |
|-------|-------------|----------|
| **MPS** | MPS mismatch and suboptimal settings | ERROR/WARNING |
| **Link** | Speed/width, training, quality, performance | ERROR/WARNING/INFO |
| **ASPM** | Configuration consistency | WARNING |
| **AER** | Correctable/uncorrectable errors | ERROR/WARNING |
| **PM** | Power state and PME support | INFO |

## Usage

```bash
# Run full diagnostic
sudo lspci --diagnose

# Combined with verbose output
sudo lspci -vv --diagnose

# Show help
lspci --help | grep diagnose
```

## Example Output

```
============================================================
  PCIe Diagnostic Mode
============================================================

------------------------------------------------------------
  Link 1: 0000:00:01.0 <-> 0000:01:00.0
------------------------------------------------------------

============================================================
  MPS Analysis
============================================================
  [0000:00:01.0] Root Port
      Cap: 512 bytes, Cur: 256 bytes
  [0000:01:00.0] Endpoint
      Cap: 256 bytes, Cur: 128 bytes

  *** ERROR: MPS mismatch
      -> Use same MPS on both ends

============================================================
  Link Analysis
============================================================
  [0000:00:01.0] Root Port
      Max: 16 GT/s x16 (31.51 GB/s)
      Cur: 16 GT/s x16 (31.51 GB/s)
  [0000:01:00.0] Endpoint
      Max: 16 GT/s x16 (31.51 GB/s)
      Cur: 16 GT/s x8 (15.75 GB/s)

  Bandwidth Loss: 0.00 GB/s (A), 15.75 GB/s (B)
  *** WARNING: Reduced width
      -> Check physical connection

  Training: A=Done, B=Done, LaneErr: A=0x0, B=0x0

  Quality: A=EQ(P1P2P3), B=EQ-incomplete()
  *** WARNING: EQ incomplete
      -> Check cable

  Performance: MPS-small NoRelaxedOrd

============================================================
  ASPM
============================================================
  [0000:00:01.0] Root Port
      Sup: L0s+L1, En: L1
  [0000:01:00.0] Endpoint
      Sup: L0s+L1, En: L1

  [OK] ASPM consistent

============================================================
  AER
============================================================
  [0000:00:01.0] Root Port
  Correctable: BadTLP Timeout
  *** WARNING: Correctable errors
      -> Monitor for trends

============================================================
  Power Management
============================================================
  [0000:00:01.0] Root Port
      Ver: 1.2, State: D0
      PME: D0 D3hot D3cold

============================================================
  Summary: 1 links, 4 issues
  Review issues above.
============================================================
```

## Files Changed

### New Files
- `ls-diag.c` - PCIe diagnostic logic (600 lines)

### Modified Files
- `lspci.h` - Added `pcie_diagnose()` declaration
- `lspci.c` - Added `--diagnose` option
- `ls-caps.c` - Added MPS warning in verbose output
- `Makefile` - Added `ls-diag.o` to build

## Implementation Details

### Relationship to Existing Code

| Feature | Existing Code | This PR |
|---------|--------------|---------|
| **Function** | Display raw capabilities | Analyze configuration health |
| **AER** | Show error registers | Interpret and recommend |
| **Link** | Show individual state | Compare partners, detect mismatches |
| **Output** | Raw values | Human-readable diagnosis |

### Code Structure

Following pciutils design patterns:
- Self-contained module (`ls-diag.c`)
- Single exported function (`pcie_diagnose()`)
- Static internal functions
- Uses shared infrastructure from `lspci.h`

### Code Style

- 2-space indentation, K&R brace style
- Lowercase with underscores naming
- Function return type on separate line
- Static functions for internal use

### Architecture

```
pcie_diagnose()
    ├── fetch_pcie_info() - Get PCIe capabilities
    ├── fetch_sec_pcie() - Get Secondary PCIe info
    ├── fetch_aer() - Get AER status
    ├── fetch_pm() - Get PM info
    ├── find_partner() - Find link partner
    ├── check_mps() - MPS analysis
    ├── check_link() - Comprehensive link analysis
    ├── check_aspm() - ASPM analysis
    ├── check_aer() - Error analysis
    ├── check_pm() - Power management
    └── check_device() - Single device config
```

### Key Functions

**`check_link()` - Comprehensive Link Analysis:**
- Speed/width status with bandwidth calculation
- Link training status and lane errors
- Link quality (equalization, retimer) for Gen4+
- Performance optimization hints

### Data Structure

```c
struct diag_info
{
  struct device *dev;
  int type;
  
  /* Device */
  int max_mps, cur_mps;
  int relaxed_ordering, extended_tag;
  
  /* Link */
  int max_link_speed, max_link_width;
  int neg_link_speed, neg_link_width;
  int link_training;
  
  /* Quality */
  int equalization_complete;
  int equalization_phase[3];
  int retimer_detected;
  
  /* Errors */
  int has_aer;
  u32 uncor_status, cor_status;
  
  /* Power */
  int has_pm;
  int pm_current_state;
};
```

## Testing

### Build Test
```bash
make clean && make
# Successfully builds with no new warnings
```

### Help Text
```bash
./lspci --help | grep diagnose
# Output: --diagnose  Enable PCIe diagnostic mode...
```

### Runtime Test (requires root on Linux)
```bash
sudo ./lspci --diagnose
```

## Backwards Compatibility

- New feature is opt-in via `--diagnose` flag
- No changes to existing behavior
- No API changes to libpci
- Clean compile with no new warnings

## References

- PCI Express Base Specification, Rev. 6.0
- Section 7.8: PCI Express Capability Structure
- Section 7.10: Advanced Error Reporting Capability

---

**Signed-off-by**: Danny <Danny1996@foxmail.com>
