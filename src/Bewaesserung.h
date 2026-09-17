#pragma once

#include <stdint.h>

// ============================================================================
// ET0Calculation – Referenzverdunstung (Hargreaves-Samani, FAO-56)
//
// Ersetzt die bisherige Berechnung im OFM-LogicModule (R1-R7) durch native
// Firmware-Logik in GardenControl. Nutzt:
//   - ParamBASE_Latitude (bereits vorhanden aus OGM-Common "Gerätestandort")
//   - openknx.time (Tag-des-Jahres, Mitternachts-Callback)
//
// Ablauf:
//   1) Laufende Außentemperatur kommt per KO rein  -> ET0_processInputKo()
//   2) Firmware sammelt fortlaufend Tmin/Tmax des aktuellen Tages
//   3) Bei Tageswechsel (00:00 lokale Zeit):
//        a) gestrige Tmin/Tmax/Tmean einfrieren
//        b) ET0 aus den gestrigen Werten berechnen (Formel bezieht sich
//           bewusst auf "gestern", siehe Referenz-Dokument Abschnitt 4)
//        c) Aggregation für den neuen Tag zurücksetzen
//   4) ET0 + Diagnosewerte werden per KO gesendet (Diagnose-KOs optional,
//      per Parameter "Diag_KO_ET0_enable" schaltbar)
// ============================================================================

void Bewaesserung_setup();
void Bewaesserung_loop();

// Eingänge vom Bus
void process_Temperatur_Wetterstation(float aktuelleTemperatur);
void process_Regenmenge_Wetterstation(float regenmengeHeuteMm);

// Wird aus GardenControlDevice::loop() gerufen, erkennt den Tageswechsel
// per Vergleich (keine Callback-Registrierung nötig)
void process_Bewaesserungsberechnung(void);

// Persistenz (siehe Einbindung in GardenControlDevice::readFlash/writeFlash)
void Bewaesserung_readFlash(const uint8_t* buffer, const uint16_t size);
void Bewaesserung_writeFlash();
uint16_t Bewaesserung_flashSize();


// Konsolen-Diagnose ("et0" eingeben)
bool ET0_processCommand(const std::string cmd, bool debugKo);

uint16_t getYearDay(void); // liefert J, 1-basiert (1..366)


float get_Geographische_Breite_Radiant();

struct RaResult
{
    float ra_mm;    // Ra bereits in mm/Tag (inkl. 0.408-Faktor)
    float dr;       // Diagnose
    float delta;    // Diagnose 
    float omegaS;   // Diagnose 
    float phi;      // Diagnose
    uint16_t Kalendertag_des_Jahres_J;  // Diagnose
};
RaResult calc_Ra(uint16_t J);

float calc_ET0(float T_mean, float T_max, float T_min, float Ra_mm);
float calc_ETc(float ET0, float Kc);
float calc_Bodenwasserkonto(float konto_alt, float niederschlag_mm, float ETc, float nFK);
float calc_Schwellwert(float p, float nFK);
bool calc_bedarf(float bodenwasserkonto_neu, float p, float nFK);
float calc_laufzeit_sek(float fehlmenge_mm, float niederschlagsrate_mm_h);
float calc_Zugefuehrte_Wassermenge(float laufzeit_sek, float niederschlagsrate_mm_h);
float calc_Bodenwasserkonto_final(float bodenwasserkonto_neu, float bewaesserung_mm, float nFK);
float calc_NutzbareFeldkapazitaet(float FK, float PWP);

// float calc_Ra();
float get_Geographische_Breite_Radiant();
void Tageswechsel_Werte_speichern(uint16_t gestern);