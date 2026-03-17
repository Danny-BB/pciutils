/*
 *	The PCI Utilities -- PCIe Diagnostic Mode
 *
 *	Copyright (c) 2024 Danny <Danny1996@foxmail.com>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include "lspci.h"

/* MPS values in bytes based on the 3-bit field */
static const int mps_values[] = {128, 256, 512, 1024, 2048, 4096, 0, 0};

static const char *
link_speed_str(int speed)
{
  switch (speed)
    {
    case 1: return "2.5 GT/s";
    case 2: return "5 GT/s";
    case 3: return "8 GT/s";
    case 4: return "16 GT/s";
    case 5: return "32 GT/s";
    case 6: return "64 GT/s";
    default: return "Unknown";
    }
}

static const char *
dev_type_str(int type)
{
  switch (type)
    {
    case PCI_EXP_TYPE_ENDPOINT:      return "Endpoint";
    case PCI_EXP_TYPE_LEG_END:       return "Legacy Endpoint";
    case PCI_EXP_TYPE_ROOT_PORT:     return "Root Port";
    case PCI_EXP_TYPE_UPSTREAM:      return "Upstream Port";
    case PCI_EXP_TYPE_DOWNSTREAM:    return "Downstream Port";
    case PCI_EXP_TYPE_PCI_BRIDGE:    return "PCIe-PCI Bridge";
    case PCI_EXP_TYPE_PCIE_BRIDGE:   return "PCI-PCIe Bridge";
    case PCI_EXP_TYPE_ROOT_INT_EP:   return "Root Complex IE";
    case PCI_EXP_TYPE_ROOT_EC:       return "Root Complex EC";
    default:                         return "Unknown";
    }
}

static int
is_bridge_type(int type)
{
  return (type == PCI_EXP_TYPE_ROOT_PORT ||
          type == PCI_EXP_TYPE_UPSTREAM ||
          type == PCI_EXP_TYPE_DOWNSTREAM ||
          type == PCI_EXP_TYPE_PCI_BRIDGE ||
          type == PCI_EXP_TYPE_PCIE_BRIDGE);
}

/* PCIe device diagnostic information */
struct diag_info
{
  struct device *dev;
  int pcie_cap;
  int type;

  /* Device Capabilities */
  int max_mps;
  int cur_mps;
  int relaxed_ordering;
  int extended_tag;
  int no_snoop;
  int max_read_req;

  /* Link Capabilities */
  int max_link_speed;
  int max_link_width;
  int aspm_support;

  /* Link Control */
  int aspm_enabled;
  int common_clock;

  /* Link Status */
  int neg_link_speed;
  int neg_link_width;
  int link_training;

  /* Link Quality */
  int equalization_complete;
  int equalization_phase1;
  int equalization_phase2;
  int equalization_phase3;
  int equalization_req;
  int retimer_detected;
  int retimer_supported;

  /* Secondary PCIe */
  int has_sec_pcie;
  u32 lane_err_status;

  /* AER */
  int has_aer;
  u32 uncor_status;
  u32 cor_status;

  /* Power Management */
  int has_pm;
  int pm_version;
  int pme_support;
  int pm_current_state;
};

static inline int
get_mps_size(int raw)
{
  return mps_values[raw & 0x7];
}

static void
print_header(const char *title)
{
  printf("\n============================================================\n");
  printf("  %s\n", title);
  printf("============================================================\n");
}

static void
print_device(struct diag_info *info)
{
  struct pci_dev *p = info->dev->dev;
  printf("  [%04x:%02x:%02x.%d] %s\n",
         p->domain, p->bus, p->dev, p->func,
         dev_type_str(info->type));
}

static void
print_error(const char *issue, const char *fix)
{
  printf("\n  *** ERROR: %s\n", issue);
  if (fix)
    printf("      -> %s\n", fix);
}

static void
print_warning(const char *issue, const char *fix)
{
  printf("\n  *** WARNING: %s\n", issue);
  if (fix)
    printf("      -> %s\n", fix);
}

static void
print_info(const char *issue, const char *fix)
{
  printf("\n  *** INFO: %s\n", issue);
  if (fix)
    printf("      -> %s\n", fix);
}

static void
print_ok(const char *msg)
{
  printf("\n  [OK] %s\n", msg);
}

/* Fetch PCIe capability information */
static int
fetch_pcie_info(struct device *d, struct diag_info *info)
{
  u16 status;
  int where = 0;
  int cap_id;

  if (!d || !d->config)
    return 0;

  memset(info, 0, sizeof(*info));

  status = get_conf_word(d, PCI_STATUS);
  if (!(status & PCI_STATUS_CAP_LIST))
    return 0;

  cap_id = PCI_CAPABILITY_LIST;
  if (get_conf_byte(d, PCI_HEADER_TYPE) & 0x80)
    cap_id = PCI_CB_CAPABILITY_LIST;

  where = get_conf_byte(d, cap_id);
  while (where)
    {
      u8 id = get_conf_byte(d, where + PCI_CAP_LIST_ID);

      if (id == PCI_CAP_ID_EXP)
        {
          u16 flags = get_conf_word(d, where + PCI_EXP_FLAGS);
          u32 devcap = get_conf_long(d, where + PCI_EXP_DEVCAP);
          u16 devctl = get_conf_word(d, where + PCI_EXP_DEVCTL);
          u32 lnkcap = get_conf_long(d, where + PCI_EXP_LNKCAP);
          u16 lnkctl = get_conf_word(d, where + PCI_EXP_LNKCTL);
          u16 lnksta = get_conf_word(d, where + PCI_EXP_LNKSTA);

          info->dev = d;
          info->pcie_cap = where;
          info->type = (flags & PCI_EXP_FLAGS_TYPE) >> 4;

          /* Device info */
          info->max_mps = get_mps_size(devcap & PCI_EXP_DEVCAP_PAYLOAD);
          info->cur_mps = get_mps_size((devctl & PCI_EXP_DEVCTL_PAYLOAD) >> 5);
          info->relaxed_ordering = !!(devctl & PCI_EXP_DEVCTL_RELAXED);
          info->extended_tag = !!(devctl & PCI_EXP_DEVCTL_EXT_TAG);
          info->no_snoop = !!(devctl & PCI_EXP_DEVCTL_NOSNOOP);
          info->max_read_req = get_mps_size((devctl & PCI_EXP_DEVCTL_READRQ) >> 12);

          /* Link info */
          info->max_link_speed = lnkcap & PCI_EXP_LNKCAP_SPEED;
          info->max_link_width = (lnkcap & PCI_EXP_LNKCAP_WIDTH) >> 4;
          info->aspm_support = (lnkcap & PCI_EXP_LNKCAP_ASPM) >> 10;
          info->aspm_enabled = lnkctl & PCI_EXP_LNKCTL_ASPM;
          info->common_clock = !!(lnkctl & PCI_EXP_LNKCTL_CLOCK);
          info->neg_link_speed = lnksta & PCI_EXP_LNKSTA_SPEED;
          info->neg_link_width = (lnksta & PCI_EXP_LNKSTA_WIDTH) >> 4;
          info->link_training = !!(lnksta & PCI_EXP_LNKSTA_TRAIN);

          /* Link Quality (Gen4+) - PCI_EXP_FLAGS_VER2 */
          if (flags & 0x20)
            {
              u32 lnkcap2 = get_conf_long(d, where + PCI_EXP_LNKCAP2);
              u16 lnksta2 = get_conf_word(d, where + PCI_EXP_LNKSTA2);

              info->retimer_supported = !!(lnkcap2 & PCI_EXP_LNKCAP2_RETIMER);
              info->equalization_complete = !!(lnksta2 & PCI_EXP_LINKSTA2_EQU_COMP);
              info->equalization_phase1 = !!(lnksta2 & PCI_EXP_LINKSTA2_EQU_PHASE1);
              info->equalization_phase2 = !!(lnksta2 & PCI_EXP_LINKSTA2_EQU_PHASE2);
              info->equalization_phase3 = !!(lnksta2 & PCI_EXP_LINKSTA2_EQU_PHASE3);
              info->equalization_req = !!(lnksta2 & PCI_EXP_LINKSTA2_EQU_REQ);
              info->retimer_detected = !!(lnksta2 & PCI_EXP_LINKSTA2_RETIMER);
            }

          return 1;
        }

      where = get_conf_byte(d, where + PCI_CAP_LIST_NEXT);
    }

  return 0;
}

/* Fetch Secondary PCIe info */
static void
fetch_sec_pcie(struct device *d, struct diag_info *info)
{
  u32 header;
  int pos = 0x100;
  int cap_id, next_cap;

  while (pos >= 0x100 && pos < 0x1000)
    {
      header = get_conf_long(d, pos);
      cap_id = header & 0xFFFF;
      next_cap = (header >> 20) & 0xFFC;

      if (cap_id == PCI_EXT_CAP_ID_SECPCI)
        {
          info->has_sec_pcie = 1;
          if (config_fetch(d, pos + PCI_SEC_LANE_ERR, 4))
            info->lane_err_status = get_conf_long(d, pos + PCI_SEC_LANE_ERR);
          return;
        }

      if (next_cap == 0)
        break;
      pos = next_cap;
    }
}

/* Fetch AER info */
static void
fetch_aer(struct device *d, struct diag_info *info)
{
  u32 header;
  int pos = 0x100;
  int cap_id, next_cap;

  while (pos >= 0x100 && pos < 0x1000)
    {
      header = get_conf_long(d, pos);
      cap_id = header & 0xFFFF;
      next_cap = (header >> 20) & 0xFFC;

      if (cap_id == PCI_EXT_CAP_ID_AER)
        {
          info->has_aer = 1;
          info->uncor_status = get_conf_long(d, pos + PCI_ERR_UNCOR_STATUS);
          info->cor_status = get_conf_long(d, pos + PCI_ERR_COR_STATUS);
          return;
        }

      if (next_cap == 0)
        break;
      pos = next_cap;
    }
}

/* Fetch PM info */
static void
fetch_pm(struct device *d, struct diag_info *info)
{
  int where = 0;
  int cap_id;
  u16 status;

  status = get_conf_word(d, PCI_STATUS);
  if (!(status & PCI_STATUS_CAP_LIST))
    return;

  cap_id = PCI_CAPABILITY_LIST;
  if (get_conf_byte(d, PCI_HEADER_TYPE) & 0x80)
    cap_id = PCI_CB_CAPABILITY_LIST;

  where = get_conf_byte(d, cap_id);
  while (where)
    {
      u8 id = get_conf_byte(d, where + PCI_CAP_LIST_ID);

      if (id == PCI_CAP_ID_PM)
        {
          u16 pmc = get_conf_word(d, where + 2);
          u16 pmcsr = get_conf_word(d, where + PCI_PM_CTRL);

          info->has_pm = 1;
          info->pm_version = pmc & PCI_PM_CAP_VER_MASK;
          info->pme_support = (pmc >> 11) & 0x1F;
          info->pm_current_state = pmcsr & PCI_PM_CTRL_STATE_MASK;
          return;
        }

      where = get_conf_byte(d, where + PCI_CAP_LIST_NEXT);
    }
}

/* Find link partner */
static struct device *
find_partner(struct device *d, struct diag_info *info)
{
  struct device *candidate;

  if (info->type == PCI_EXP_TYPE_ROOT_PORT ||
      info->type == PCI_EXP_TYPE_DOWNSTREAM)
    {
      if (d->bridge)
        {
          unsigned int sec_bus = d->bridge->secondary;
          for (candidate = first_dev; candidate; candidate = candidate->next)
            {
              if (candidate->dev->domain == (int)info->dev->dev->domain &&
                  candidate->dev->bus == sec_bus)
                {
                  struct diag_info ci;
                  if (fetch_pcie_info(candidate, &ci) && !is_bridge_type(ci.type))
                    return candidate;
                }
            }
        }
    }
  else if (info->type == PCI_EXP_TYPE_UPSTREAM ||
           info->type == PCI_EXP_TYPE_ENDPOINT ||
           info->type == PCI_EXP_TYPE_LEG_END)
    {
      for (candidate = first_dev; candidate; candidate = candidate->next)
        {
          if (candidate->bridge)
            {
              struct bus *bus = candidate->bridge->first_bus;
              while (bus)
                {
                  if (bus->domain == (unsigned int)info->dev->dev->domain &&
                      bus->number == info->dev->dev->bus)
                    return candidate;
                  bus = bus->sibling;
                }
            }
        }
    }

  return NULL;
}

/* Calculate bandwidth in GB/s */
static double
calc_bandwidth(int speed, int width)
{
  double gt = (speed == 1) ? 2.5 : (speed == 2) ? 5.0 : (speed == 3) ? 8.0 :
              (speed == 4) ? 16.0 : (speed == 5) ? 32.0 : (speed == 6) ? 64.0 : 0;
  double eff;

  if (speed <= 2)
    eff = 0.8;                    /* Gen1/2: 8b/10b encoding */
  else
    eff = 128.0 / 130.0;          /* Gen3+: 128b/130b encoding */

  return gt * width * eff / 8.0;
}

/* Check MPS */
static int
check_mps(struct diag_info *a, struct diag_info *b)
{
  int issues = 0;
  int min_cap = (a->max_mps < b->max_mps) ? a->max_mps : b->max_mps;

  print_header("MPS Analysis");
  print_device(a);
  printf("      Cap: %d bytes, Cur: %d bytes\n", a->max_mps, a->cur_mps);
  print_device(b);
  printf("      Cap: %d bytes, Cur: %d bytes\n", b->max_mps, b->cur_mps);

  if (a->cur_mps != b->cur_mps)
    {
      print_error("MPS mismatch", "Use same MPS on both ends");
      issues++;
    }
  if (a->cur_mps != min_cap || b->cur_mps != min_cap)
    {
      print_warning("Suboptimal MPS", "Set to minimum capability");
      issues++;
    }

  if (issues == 0)
    print_ok("MPS optimal");

  return issues;
}

/* Check Link - comprehensive */
static int
check_link(struct diag_info *a, struct diag_info *b)
{
  int issues = 0;
  double bw_max_a, bw_max_b, bw_cur_a, bw_cur_b;
  int has_eq = (a->max_link_speed >= 4 && a->equalization_complete) ||
               (b->max_link_speed >= 4 && b->equalization_complete);

  bw_max_a = calc_bandwidth(a->max_link_speed, a->max_link_width);
  bw_max_b = calc_bandwidth(b->max_link_speed, b->max_link_width);
  bw_cur_a = calc_bandwidth(a->neg_link_speed, a->neg_link_width);
  bw_cur_b = calc_bandwidth(b->neg_link_speed, b->neg_link_width);

  print_header("Link Analysis");

  /* Speed/Width/Bandwidth */
  print_device(a);
  printf("      Max: %s x%d (%.2f GB/s)\n",
         link_speed_str(a->max_link_speed), a->max_link_width, bw_max_a);
  printf("      Cur: %s x%d (%.2f GB/s)\n",
         link_speed_str(a->neg_link_speed), a->neg_link_width, bw_cur_a);

  print_device(b);
  printf("      Max: %s x%d (%.2f GB/s)\n",
         link_speed_str(b->max_link_speed), b->max_link_width, bw_max_b);
  printf("      Cur: %s x%d (%.2f GB/s)\n",
         link_speed_str(b->neg_link_speed), b->neg_link_width, bw_cur_b);

  /* Issues */
  if (a->neg_link_speed < a->max_link_speed ||
      b->neg_link_speed < b->max_link_speed)
    {
      print_warning("Reduced speed", "Check signal integrity");
      issues++;
    }
  if (a->neg_link_width < a->max_link_width ||
      b->neg_link_width < b->max_link_width)
    {
      printf("\n  Bandwidth Loss: %.2f GB/s (A), %.2f GB/s (B)\n",
             bw_max_a - bw_cur_a, bw_max_b - bw_cur_b);
      print_warning("Reduced width", "Check physical connection");
      issues++;
    }
  if (a->neg_link_speed != b->neg_link_speed)
    {
      print_error("Speed mismatch", "Configuration error");
      issues++;
    }

  /* Training - always show */
  printf("\n  Training: A=%s, B=%s",
         a->link_training ? "InProgress" : "Done",
         b->link_training ? "InProgress" : "Done");
  if (a->has_sec_pcie || b->has_sec_pcie)
    printf(", LaneErr: A=0x%x, B=0x%x", a->lane_err_status, b->lane_err_status);
  printf("\n");

  if (a->link_training || b->link_training)
    {
      print_warning("Training in progress", "Wait for completion");
      issues++;
    }
  else
    {
      print_ok("Training complete");
    }

  if (a->lane_err_status || b->lane_err_status)
    {
      print_error("Lane errors detected", "Check connection");
      issues++;
    }

  /* Quality (Gen4+) - always show if applicable */
  if (has_eq)
    {
      printf("\n  Quality: A=EQ%s", a->equalization_complete ? "" : "-incomplete");
      if (a->equalization_complete)
        printf("(%s%s%s)",
               a->equalization_phase1 ? "P1" : "",
               a->equalization_phase2 ? "P2" : "",
               a->equalization_phase3 ? "P3" : "");

      printf(", B=EQ%s", b->equalization_complete ? "" : "-incomplete");
      if (b->equalization_complete)
        printf("(%s%s%s)",
               b->equalization_phase1 ? "P1" : "",
               b->equalization_phase2 ? "P2" : "",
               b->equalization_phase3 ? "P3" : "");
      printf("\n");

      if (!a->equalization_complete || !b->equalization_complete)
        {
          print_warning("EQ incomplete", "Check cable");
          issues++;
        }
      else
        {
          print_ok("EQ complete");
        }
    }

  /* Performance - always show */
  printf("\n  Performance: ");
  if (a->cur_mps < 512 || b->cur_mps < 512)
    printf("MPS-small ");
  else
    printf("MPS-ok ");

  if (!a->relaxed_ordering || !b->relaxed_ordering)
    printf("NoRelaxedOrd ");
  else
    printf("RelaxedOrd ");

  if (!a->extended_tag || !b->extended_tag)
    printf("NoExtTag ");
  else
    printf("ExtTag ");
  printf("\n");

  if (issues == 0)
    print_ok("Link optimal");

  return issues;
}

/* Check ASPM */
static int
check_aspm(struct diag_info *a, struct diag_info *b)
{
  int issues = 0;
  const char *aspm_str[] = {"Off", "L0s", "L1", "L0s+L1"};

  print_header("ASPM");
  print_device(a);
  printf("      Sup: %s, En: %s\n",
         aspm_str[a->aspm_support & 0x3], aspm_str[a->aspm_enabled & 0x3]);
  print_device(b);
  printf("      Sup: %s, En: %s\n",
         aspm_str[b->aspm_support & 0x3], aspm_str[b->aspm_enabled & 0x3]);

  if (a->aspm_enabled != b->aspm_enabled)
    {
      print_warning("ASPM mismatch", "Use same mode");
      issues++;
    }
  if ((a->aspm_enabled & ~a->aspm_support) ||
      (b->aspm_enabled & ~b->aspm_support))
    {
      print_error("ASPM unsupported", "Disable invalid mode");
      issues++;
    }

  if (issues == 0)
    print_ok("ASPM consistent");

  return issues;
}

/* Check AER */
static int
check_aer(struct diag_info *info)
{
  int issues = 0;

  if (!info->has_aer)
    return 0;

  print_header("AER");
  print_device(info);

  if (info->cor_status)
    {
      printf("  Correctable:");
      if (info->cor_status & 0x00000001) printf(" RxErr");
      if (info->cor_status & 0x00000040) printf(" BadTLP");
      if (info->cor_status & 0x00000080) printf(" BadDLLP");
      if (info->cor_status & 0x00000100) printf(" Rollover");
      if (info->cor_status & 0x00001000) printf(" Timeout");
      printf("\n");
      print_warning("Correctable errors", "Monitor for trends");
      issues++;
    }

  if (info->uncor_status)
    {
      printf("  Uncorrectable:");
      if (info->uncor_status & 0x00000010) printf(" DLP");
      if (info->uncor_status & 0x00000020) printf(" SDES");
      if (info->uncor_status & 0x00001000) printf(" TLP");
      if (info->uncor_status & 0x00004000) printf(" CplTimeout");
      if (info->uncor_status & 0x00008000) printf(" CplAbort");
      if (info->uncor_status & 0x00010000) printf(" UnxCpl");
      printf("\n");
      print_error("Uncorrectable errors", "Hardware issue");
      issues++;
    }

  if (issues == 0)
    print_ok("No errors");

  return issues;
}

/* Check PM */
static int
check_pm(struct diag_info *info)
{
  const char *state_str[] = {"D0", "D1", "D2", "D3hot"};

  if (!info->has_pm)
    return 0;

  print_header("Power Management");
  print_device(info);
  printf("      Ver: %d.%d, State: %s\n",
         info->pm_version >> 4, info->pm_version & 0xF,
         state_str[info->pm_current_state & 0x3]);

  if (info->pme_support)
    {
      printf("      PME:");
      if (info->pme_support & 0x01) printf(" D0");
      if (info->pme_support & 0x02) printf(" D1");
      if (info->pme_support & 0x04) printf(" D2");
      if (info->pme_support & 0x08) printf(" D3hot");
      if (info->pme_support & 0x10) printf(" D3cold");
      printf("\n");
    }

  if (info->pm_current_state != 0)
    print_info("Low power state", "May impact performance");

  return 0;
}

/* Check single device */
static int
check_device(struct diag_info *info)
{
  int issues = 0;

  print_header("Device Config");
  print_device(info);

  printf("      MPS: %d/%d, RO: %s, ET: %s, NS: %s\n",
         info->cur_mps, info->max_mps,
         info->relaxed_ordering ? "Y" : "N",
         info->extended_tag ? "Y" : "N",
         info->no_snoop ? "Y" : "N");

  if (!info->common_clock)
    {
      print_warning("No common clock", "Enable if shared refclk");
      issues++;
    }

  return issues;
}

/* Main diagnostic function */
void
pcie_diagnose(void)
{
  struct device *d;
  struct diag_info info;
  int total_links = 0;
  int total_issues = 0;

  printf("\n============================================================\n");
  printf("  PCIe Diagnostic Mode\n");
  printf("============================================================\n");

  /* Check links */
  for (d = first_dev; d; d = d->next)
    {
      if (!fetch_pcie_info(d, &info))
        continue;

      fetch_sec_pcie(d, &info);
      fetch_aer(d, &info);
      fetch_pm(d, &info);

      if (info.type != PCI_EXP_TYPE_ROOT_PORT &&
          info.type != PCI_EXP_TYPE_DOWNSTREAM)
        continue;

      struct device *partner = find_partner(d, &info);
      if (!partner)
        continue;

      struct diag_info pi;
      if (!fetch_pcie_info(partner, &pi))
        continue;

      fetch_sec_pcie(partner, &pi);
      fetch_aer(partner, &pi);
      fetch_pm(partner, &pi);

      printf("\n------------------------------------------------------------\n");
      printf("  Link %d: %04x:%02x:%02x.%d <-> %04x:%02x:%02x.%d\n",
             ++total_links,
             info.dev->dev->domain, info.dev->dev->bus,
             info.dev->dev->dev, info.dev->dev->func,
             pi.dev->dev->domain, pi.dev->dev->bus,
             pi.dev->dev->dev, pi.dev->dev->func);
      printf("------------------------------------------------------------");

      total_issues += check_mps(&info, &pi);
      total_issues += check_link(&info, &pi);
      total_issues += check_aspm(&info, &pi);
      total_issues += check_aer(&info);
      total_issues += check_aer(&pi);
      total_issues += check_pm(&info);
    }

  /* Check standalone endpoints */
  for (d = first_dev; d; d = d->next)
    {
      if (!fetch_pcie_info(d, &info))
        continue;

      if (info.type != PCI_EXP_TYPE_ENDPOINT &&
          info.type != PCI_EXP_TYPE_LEG_END)
        continue;

      struct device *partner = find_partner(d, &info);
      if (partner)
        {
          struct diag_info pi;
          if (fetch_pcie_info(partner, &pi) &&
              (pi.type == PCI_EXP_TYPE_ROOT_PORT ||
               pi.type == PCI_EXP_TYPE_DOWNSTREAM))
            continue;
        }

      fetch_aer(d, &info);
      fetch_pm(d, &info);

      total_issues += check_device(&info);
      total_issues += check_aer(&info);
      total_issues += check_pm(&info);
    }

  printf("\n============================================================\n");
  printf("  Summary: %d links, %d issues\n", total_links, total_issues);
  printf("  %s\n", total_issues == 0 ? "All links optimal." : "Review issues above.");
  printf("============================================================\n\n");
}
