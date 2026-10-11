// ============================================================================
// sd_card.cpp — microSD SDMMC slot 0 per SPINNERINO P4 (giochi LASERDISC)
//
//   Metodo IDF collaudato (identico a CABINATONE/sd_rom.cpp, che gira gia' su
//   hardware P4): SDMMC SLOT 0 (IO-MUX) + alimentazione card via LDO on-chip
//   canale 4. NON la libreria Arduino SD_MMC: usa lo slot 1 GPIO-matrix e non
//   pilota il pwr_ctrl, quindi sul P4 non monta.
//
//   ⚠️ SDMMC_HOST_DEFAULT() ha .slot = SDMMC_HOST_SLOT_1 (= i pin del C6!).
//      Va SEMPRE forzato a SLOT_0, vedi commento in config.h.
//
//   Slot 0 = IO-MUX: i pin (CLK 43, CMD 44, D0..D3 39..42) sono FISSI nel
//   silicio e li imposta SDMMC_SLOT_CONFIG_DEFAULT(). Non vanno assegnati a
//   mano: gli SD_PIN_* in config.h sono solo documentazione.
// ============================================================================
#include <Arduino.h>
#include <sys/stat.h>

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

#include "config.h"
#include "emulation/sd_card.h"

#ifdef USE_SDCARD

static int                   g_status = SD_ST_NONE;
static sdmmc_card_t         *s_card   = nullptr;
static sd_pwr_ctrl_handle_t  s_pwr    = nullptr;

int  sd_status(void)     { return g_status; }
bool sd_is_mounted(void) { return s_card != nullptr; }

const char *sd_status_str(void)
{
  switch (g_status) {
    case SD_ST_OK:         return "SD OK";
    case SD_ST_LDO_FAIL:   return "SD: LDO FAIL";
    case SD_ST_MOUNT_FAIL: return "SD ASSENTE O ILLEGGIBILE";
    default:               return "SD NON INIZIALIZZATA";
  }
}

static bool try_mount(int width, int freq_khz)
{
  esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
  mcfg.format_if_mount_failed = false;
  // Il player laserdisc tiene 2 cache LRU di handle aperti (video .mjpg +
  // audio .pcm). Con max_files troppo basso la fopen fallisce quando il
  // working-set supera il limite -> video fermo + audio muto per secondi.
  // Tenere >= 2*OC_N + margine (vedi SD_MAX_FILES in config.h).
  mcfg.max_files            = SD_MAX_FILES;
  mcfg.allocation_unit_size = 16 * 1024;

  sdmmc_host_t host    = SDMMC_HOST_DEFAULT();
  host.slot            = SDMMC_HOST_SLOT_0;   // IO-MUX — NON il default (slot 1)!
  host.max_freq_khz    = freq_khz;
  host.pwr_ctrl_handle = s_pwr;               // alimentazione card via LDO

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = width;
  slot.cd    = SDMMC_SLOT_NO_CD;
  slot.wp    = SDMMC_SLOT_NO_WP;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  return esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mcfg, &s_card) == ESP_OK;
}

bool sd_mount(void)
{
  if (s_card) return true;

  if (!s_pwr) {
    sd_pwr_ctrl_ldo_config_t ldo = {};
    ldo.ldo_chan_id = SD_LDO_CHANNEL;         // 4
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &s_pwr);
    if (err != ESP_OK) {
      Serial.printf("[SD] sd_pwr_ctrl_new_on_chip_ldo(chan=%d) fallita: %s (0x%x)\n",
                    SD_LDO_CHANNEL, esp_err_to_name(err), (unsigned)err);
      g_status = SD_ST_LDO_FAIL;
      return false;
    }
  }

  // degrado progressivo: 4-bit HS -> 4-bit 20MHz -> 1-bit 20MHz
  // (robustezza verso card lente o cablaggio marginale)
  if (try_mount(SD_BUS_WIDTH, 40000) ||
      try_mount(SD_BUS_WIDTH, 20000) ||
      try_mount(1, 20000)) {
    g_status = SD_ST_OK;
    return true;
  }

  g_status = SD_ST_MOUNT_FAIL;
  return false;
}

bool sd_dir_exists(const char *path)
{
  if (!sd_mount()) return false;
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

#else  // !USE_SDCARD — stub

bool        sd_mount(void)              { return false; }
bool        sd_is_mounted(void)         { return false; }
int         sd_status(void)             { return SD_ST_NONE; }
const char *sd_status_str(void)         { return "SD DISABILITATA"; }
bool        sd_dir_exists(const char *) { return false; }

#endif // USE_SDCARD
