#include "minime.h"
#include <esp_partition.h>
#include <esp_err.h>
#include <esp_core_dump.h>
#include <string.h>
#include <stdio.h>

// Last panic from flash coredump partition (partitions.csv: 0xFD0000 / 0x30000).

static bool partitionLooksBlank(const esp_partition_t* part) {
  uint8_t hdr[32];
  if (!part) return true;
  if (esp_partition_read(part, 0, hdr, sizeof(hdr)) != ESP_OK) return true;
  for (size_t i = 0; i < sizeof(hdr); i++) {
    if (hdr[i] != 0xFF) return false;
  }
  return true;
}

static void appendHex32(String& s, uint32_t v) {
  char buf[12];
  snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)v);
  s += buf;
}

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH

static const char* xtensaCauseName(uint32_t cause) {
  // Common Xtensa ExcCause values (ESP32/S3). Unknown -> nullptr (caller prints number).
  switch (cause) {
    case 0: return "IllegalInstruction";
    case 1: return "Syscall";
    case 2: return "InstructionFetchError";
    case 3: return "LoadStoreError";
    case 4: return "Level1Interrupt";
    case 5: return "Alloca";
    case 6: return "IntegerDivideByZero";
    case 8: return "Privileged";
    case 9: return "LoadStoreAlignment";
    case 12: return "InstrPIFDataError";
    case 13: return "LoadStorePIFDataError";
    case 14: return "InstrPIFAddrError";
    case 15: return "LoadStorePIFAddrError";
    case 16: return "InstTLBMiss";
    case 17: return "InstTLBMultiHit";
    case 18: return "InstFetchPrivilege";
    case 20: return "InstFetchProhibited";
    case 24: return "LoadStoreTLBMiss";
    case 25: return "LoadStoreTLBMultiHit";
    case 26: return "LoadStorePrivilege";
    case 28: return "LoadProhibited";
    case 29: return "StoreProhibited";
    default: return nullptr;
  }
}

static bool formatSummaryReport(String& out) {
  esp_err_t chk = esp_core_dump_image_check();
  if (chk == ESP_ERR_NOT_FOUND) {
    out = "**Coredump:** no image stored.";
    return false;
  }
  if (chk != ESP_OK) {
    out = String("**Coredump:** image check failed (`") + esp_err_to_name(chk) + "`).";
    return false;
  }

  esp_core_dump_summary_t* sum =
      (esp_core_dump_summary_t*)heap_caps_malloc(sizeof(esp_core_dump_summary_t),
                                                 MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  if (!sum) {
    out = "**Coredump:** out of memory for summary.";
    return false;
  }
  memset(sum, 0, sizeof(*sum));
  esp_err_t err = esp_core_dump_get_summary(sum);
  if (err != ESP_OK) {
    heap_caps_free(sum);
    // Panic reason string is often enough even when full summary fails.
    char reason[160];
    reason[0] = '\0';
    if (esp_core_dump_get_panic_reason(reason, sizeof(reason)) == ESP_OK && reason[0]) {
      out = String("**Coredump:** summary failed (`") + esp_err_to_name(err) +
            "`)\n• **Panic:** " + reason;
      return true;
    }
    out = String("**Coredump:** summary failed (`") + esp_err_to_name(err) + "`).";
    return false;
  }

  out.reserve(900);
  out = "**Coredump** (last panic)\n";
  out += "• **Task:** `";
  out += sum->exc_task[0] ? sum->exc_task : "?";
  out += "`\n• **PC:** ";
  appendHex32(out, sum->exc_pc);
  out += "\n• **TCB:** ";
  appendHex32(out, sum->exc_tcb);

  char reason[160];
  reason[0] = '\0';
  if (esp_core_dump_get_panic_reason(reason, sizeof(reason)) == ESP_OK && reason[0]) {
    out += "\n• **Panic:** ";
    out += reason;
  }

#if CONFIG_IDF_TARGET_ARCH_XTENSA || defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)
  {
    uint32_t cause = sum->ex_info.exc_cause;
    const char* cname = xtensaCauseName(cause);
    out += "\n• **ExcCause:** ";
    if (cname) {
      out += cname;
      out += " (";
      out += String(cause);
      out += ")";
    } else {
      out += String(cause);
    }
    out += "\n• **ExcVAddr:** ";
    appendHex32(out, sum->ex_info.exc_vaddr);
  }
#endif

  if (sum->app_elf_sha256[0]) {
    out += "\n• **App SHA:** `";
    // Truncate for Discord; SHA field is already hex string + NUL in IDF.
    char sha[17];
    strncpy(sha, (const char*)sum->app_elf_sha256, 16);
    sha[16] = '\0';
    out += sha;
    out += "...`";
  }

  const esp_core_dump_bt_info_t& bt = sum->exc_bt_info;
  if (bt.depth > 0) {
    out += "\n• **BT";
    if (bt.corrupted) out += " (corrupted)";
    out += ":** ";
    uint32_t n = bt.depth;
    if (n > 8) n = 8; // keep under Discord limit
    for (uint32_t i = 0; i < n; i++) {
      if (i) out += " ";
      appendHex32(out, bt.bt[i]);
    }
    if (bt.depth > n) out += " ...";
  }

  size_t addr = 0, sz = 0;
  if (esp_core_dump_image_get(&addr, &sz) == ESP_OK) {
    out += "\n• **Flash:** ";
    appendHex32(out, (uint32_t)addr);
    out += " / ";
    out += String((unsigned long)sz);
    out += " B";
  }
  out += "\n_Owner: `!coredump clear` erases after review._";

  heap_caps_free(sum);
  if ((int)out.length() > DISCORD_CONTENT_MAX) {
    out = truncateText(out, DISCORD_CONTENT_MAX);
  }
  return true;
}

#endif // CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH

bool formatCoreDumpReport(String& outReport) {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
  if (!part) {
    outReport = "**Coredump:** no `coredump` partition in table (expected @ 0xFD0000).";
    return false;
  }

  char partLine[96];
  snprintf(partLine, sizeof(partLine),
           "**Partition:** `%s` @ 0x%06lX size 0x%lX",
           part->label ? part->label : "coredump",
           (unsigned long)part->address, (unsigned long)part->size);

#if !CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
  outReport = String(partLine) +
              "\n**Firmware:** core dump-to-flash is **disabled** in this ESP32 Arduino "
              "sdkconfig (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=0`). "
              "Partition exists but panics will not write a dump; summary API unavailable.";
  if (!partitionLooksBlank(part)) {
    outReport += "\n_Note: partition is not blank — raw dump bytes present but unreadable "
                 "without flash coredump support in the core build._";
  }
  return false;
#else
  if (partitionLooksBlank(part)) {
    outReport = String(partLine) +
                "\n**Coredump:** blank (no panic recorded since erase / first flash).";
    return false;
  }
  String body;
  bool ok = formatSummaryReport(body);
  outReport = String(partLine) + "\n" + body;
  return ok;
#endif
}

bool clearCoreDumpImage(String& outReport) {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
  if (!part) {
    outReport = "**Coredump:** no partition to clear.";
    return false;
  }
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
  esp_err_t err = esp_core_dump_image_erase();
  if (err != ESP_OK) {
    outReport = String("**Coredump clear failed:** `") + esp_err_to_name(err) + "`";
    return false;
  }
  outReport = "**Coredump:** erased. Next panic can write a fresh dump.";
  return true;
#else
  // Fallback: erase the data partition so "!coredump" sees blank again.
  esp_err_t err = esp_partition_erase_range(part, 0, part->size);
  if (err != ESP_OK) {
    outReport = String("**Coredump erase failed:** `") + esp_err_to_name(err) + "`";
    return false;
  }
  outReport = "**Coredump:** partition erased (dump-to-flash still disabled in sdkconfig).";
  return true;
#endif
}
