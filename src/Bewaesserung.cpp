
#include "OpenKNX.h"

#include <ctime>
#include <math.h>

#include "Bewaesserung.h"

// ============================================================================
// TODO (ETS-Integration): Diese KO-/Parameter-Nummern sind Platzhalter und
// müssen nach Anlage der echten ComObjects/Parameter in
// GardenControl.share.xml / GardenControl.templ.xml durch die von
// OpenKNXproducer generierten Werte aus knxprod.h ersetzt werden
// (Namensschema wie bei den bestehenden BEM-Device-KOs, siehe
// BEM_Ko_Set_5V_relais etc. in knxprod.h).
//
// Benötigte neue, NICHT kanalgebundene BEM-ComObjects (device-level, wie
// ext5VRelais):
//   BEM_Ko_ET0_Temperatur_IN      Eingang  DPT 9.001  "Aktuelle Außentemperatur"
//   BEM_Ko_ET0_Aktuell            Ausgang  DPT 9.026  "ET0 [mm/Tag]"
//   BEM_Ko_ET0_Tmax_gestern       Ausgang  DPT 9.001  Diagnose
//   BEM_Ko_ET0_Tmin_gestern       Ausgang  DPT 9.001  Diagnose
//   BEM_Ko_ET0_Tmean_gestern      Ausgang  DPT 9.001  Diagnose
//   BEM_Ko_ET0_Ra_mm              Ausgang  DPT 9.xxx  Diagnose (Ra in mm/Tag)
//   BEM_Ko_ET0_dr                Ausgang  DPT 9.xxx  Diagnose (dimensionslos)
//   BEM_Ko_ET0_delta              Ausgang  DPT 9.xxx  Diagnose (Radiant)
//   BEM_Ko_ET0_omega_s            Ausgang  DPT 9.xxx  Diagnose (Radiant)
//
// Neuer Parameter (1 Bit, device-level, freies Bit/Byte in der BEM-Union
// suchen -> NICHT das bereits mehrfach belegte Offset=0/BitOffset=3 aus
// ext5VRelaisStartState/Diag_KO_PWT_enable wiederverwenden!):
//   BEM_Diag_KO_ET0_enable        "Diagnose-KOs für ET0-Berechnung senden?"
// ============================================================================

// #ifndef BEM_Ko_ET0_Temperatur_IN
//     #warning "ET0Calculation: Platzhalter-KO-Nummern aktiv - bitte knxprod.h nach ETS-Anlage aktualisieren"
//     #define BEM_Ko_ET0_Temperatur_IN 90
//     #define BEM_Ko_ET0_Aktuell 91
//     #define BEM_Ko_ET0_Tmax_gestern 92
//     #define BEM_Ko_ET0_Tmin_gestern 93
//     #define BEM_Ko_ET0_Tmean_gestern 94
//     #define BEM_Ko_ET0_Ra_mm 95
//     #define BEM_Ko_ET0_dr 96
//     #define BEM_Ko_ET0_delta 97
//     #define BEM_Ko_ET0_omega_s 98
// #endif
// #ifndef BEM_Diag_KO_ET0_enable
//     #define ParamBEM_Diag_KO_ET0_enable 1 // Platzhalter: bis ETS-Parameter existiert, immer aktiv
// #else
//     #define ParamBEM_Diag_KO_ET0_enable ((bool)(knx.paramByte(BEM_Diag_KO_ET0_enable) & BEM_Diag_KO_ET0_enableMask))
// #endif
// ---- Rasenzone-Konstanten (aus Referenz-Dokument, bis ETS-Parameter existieren) ----
namespace
{
    constexpr float RASEN_KC = 0.8f;                 // als ETS-Parameter/GA veränderlich
    constexpr float RASEN_NFK_MM = 25.0f;             // nutzbare Feldkapazität [mm]
    constexpr float RASEN_SCHWELLWERT_P = 0.5f;       // 50 %
    constexpr float RASEN_NIEDERSCHLAGSRATE = 10.0f;  // mm/h (MP-Rotator)
    constexpr uint8_t BEWAESSERUNG_START_STUNDE = 4;  // 4:00 Uhr, TODO: ETS-Parameter (4 oder 5)
}
float letzteEmpfangeneTemperatur = 0;
float letzteRegenmengeHeute = 0.0f;
int16_t letzterBekannterTag = -1; // -1 = "noch nie gesehen"
int16_t letzterBewaesserungsTag = -1;


// heutige Werte
float Temperatur_max_heute = -42.0;
float Temperatur_min_heute = -42.0;
float Temperatur_Durchschnitt_heute = -42.0;
bool gueltigeWerte_heute = false;

// Gestrige Werte
float Temperatur_max_gestern = -42.0;
float Temperatur_min_gestern = -42.0;
float Temperatur_Durchschnitt_gestern = -42.0;
float Regenmenge_gestern = 0.0f;

// Letztes Berechnungsergebnis (für Diagnose/Konsole/Getter)
float LastEt0 = 0.0f;
float LastDr = 0.0f;
float LastDelta = 0.0f;
float LastOmegaS = 0.0f;
float LastRaMm = 0.0f;
int16_t LastDayOfYear = -1;
float ET0_gestern = 0;

    // Wasserbilanz Bewaesserungszone - MUSS über Neustarts hinweg erhalten bleiben,
    // sonst geht der Kontostand bei jedem Reset verloren.
    float BewaesserungszoneKonto = RASEN_NFK_MM; // Start optimistisch: Konto voll

    // Ergebnis der letzten Tagesberechnung, wird von der Zeitsteuerung genutzt
    bool BewaesserungszoneBedarf = false;
    float BewaesserungszoneGeplanteLaufzeitSek = 0.0f;

    // Zustand der laufenden Bewässerung (falls Ventil gerade offen ist)
    bool BewaesserungszoneVentilOffen = false;
    uint32_t BewaesserungszoneVentilStartMillis = 0;

// Phi aus dem Standort-Parameter ableiten
float get_Geographische_Breite_Radiant()
{
    return (float)(ParamBASE_Latitude * PI / 180.0);
}

// ============================================================
// 1. Extraterrestrische Strahlung Ra
// ============================================================
RaResult calc_Ra(uint16_t Kalendertag_des_Jahres_J)
{
    

    float_t geografischeBreiteRadiant_Phi = get_Geographische_Breite_Radiant();

    // Solarkonstante [MJ/(m²·min)]
    const float_t Solarkonstante_Gsc = 0.0820;
    // Inverse relative Erde-Sonne-Distanz
    float_t relativeErdeSonneDistanz_dr = 1.0 + 0.033 * cos((2.0 * M_PI / 365.0) * Kalendertag_des_Jahres_J);
    // Solare Deklination [rad]
    float_t Solare_Deklination_delta = 0.409 * sin((2.0 * M_PI / 365.0) * Kalendertag_des_Jahres_J - 1.39);

    // Sonnenuntergangswinkel [rad]
    float_t Sonnenuntergangswinkel_omega_s = acos(-tan(geografischeBreiteRadiant_Phi) * tan(Solare_Deklination_delta));

    // Extraterrestrische Strahlung [MJ/(m²·Tag)]
    float_t Extraterrestrische_Strahlung_Ra = (24.0 * 60.0 / M_PI) * Solarkonstante_Gsc * relativeErdeSonneDistanz_dr * (Sonnenuntergangswinkel_omega_s * sin(geografischeBreiteRadiant_Phi) * sin(Solare_Deklination_delta) + cos(geografischeBreiteRadiant_Phi) * cos(Solare_Deklination_delta) * sin(Sonnenuntergangswinkel_omega_s));

    //return Extraterrestrische_Strahlung_Ra;

    RaResult result;
    result.Kalendertag_des_Jahres_J = Kalendertag_des_Jahres_J;
    result.phi = geografischeBreiteRadiant_Phi;
    result.ra_mm = Extraterrestrische_Strahlung_Ra * 0.408f; // MJ/(m²·Tag) -> mm/Tag
    result.dr = relativeErdeSonneDistanz_dr;
    result.delta = Solare_Deklination_delta;
    result.omegaS = Sonnenuntergangswinkel_omega_s;
    return result;
}

// ============================================================
// 2. Hargreaves-Samani: Referenz-Evapotranspiration ET0
// ============================================================
float calc_ET0(float T_mean, float T_max, float T_min, float Ra_mm)
{
    float_t ET0 = 0;
    float deltaT = T_max - T_min;
    if (deltaT < 0) deltaT = 0; // Schutz vor NaN durch fehlerhafte/vertauschte Sensordaten

    // ET0 [mm/Tag]
    ET0 = 0.0023 * (T_mean + 17.8) * sqrt(deltaT) * Ra_mm;
    if (ET0 < 0) ET0 = 0; // ET0  nie negativ - abfangen

    return ET0;
}
// ============================================================
// 3. Kultur-/Zonen-Evapotranspiration ETc
// ============================================================
float calc_ETc(float ET0, float Kc)
{
    // ETc [mm/Tag]
    float_t ETc = ET0 * Kc;
    return ETc;
}

// ============================================================
// 4. Bodenwasserkonto
// ============================================================
float calc_Bodenwasserkonto(float konto_alt, float niederschlag_mm ,float ETc, float nFK)
{
    // Neues Bodenwasserkonto [mm]
    float_t Bodenwasserkonto = konto_alt + niederschlag_mm - ETc;

    // Begrenzung auf 0 ... nFK
    Bodenwasserkonto = MAX(0.0, MIN(Bodenwasserkonto, nFK));
    return Bodenwasserkonto;
}
// ============================================================
// 5. Bewässerungsschwelle
// ============================================================
float calc_Schwellwert (float p, float nFK)
{
    // Schwellwert [mm]
    float_t schwellwert = p * nFK;
    return schwellwert;
}
// ============================================================
// 6. Bewässerungsbedarf
// ============================================================

// true  = Bewässerung erforderlich
// false = keine Bewässerung erforderlich
bool calc_bedarf (float Bodenwasserkonto_neu, float p, float nFK)
{
    bool bedarf = Bodenwasserkonto_neu < (p * nFK);
    return bedarf;
}
// ============================================================
// 7. Freigabe
// ============================================================

// // Bewässerung nur wenn Bedarf UND Basisfreigabe
// bool freigabe = bedarf && basis_freigabe;

// ============================================================
// 9. Bewässerungs-Laufzeit
// ============================================================

float calc_laufzeit_sek (float fehlmenge_mm, float niederschlagsrate_mm_h)
{
    // Niederschlagsrate [mm/h]
    // Laufzeit [Sekunden]
    float_t laufzeit_sek = (fehlmenge_mm / niederschlagsrate_mm_h) * 3600.0;
    return laufzeit_sek;
}
// ============================================================
// 10. Rückbuchung der Bewässerung
// ============================================================
float calc_Zugefuehrte_Wassermenge (float laufzeit_sek, float niederschlagsrate_mm_h)
{
    // Zugeführte Wassermenge [mm]
    float_t bewaesserung_mm = (laufzeit_sek / 3600.0) * niederschlagsrate_mm_h;
    return bewaesserung_mm;
}


float calc_Bodenwasserkonto_final(float Bodenwasserkonto_neu, float bewaesserung_mm, float nFK )
{
    // Bodenwasserkonto nach Bewässerung [mm]
    float_t Bodenwasserkonto_final = Bodenwasserkonto_neu + bewaesserung_mm;

    // Sicherheitshalber wieder auf 0 ... nFK begrenzen
    Bodenwasserkonto_final = MAX(0.0, MIN(Bodenwasserkonto_final, nFK));
    return Bodenwasserkonto_final;
}
// ============================================================
// 11. Nutzbare Feldkapazität
// ============================================================
float calc_NutzbareFeldkapazität_nFK (float FK, float PWP)
{
    // nFK = FK - PWP
    float nFK = FK - PWP;
    return nFK;
}

uint16_t getYearDay(void)
{
    // use current time
    tm tmNow;
    openknx.time.getLocalTime().toTm(tmNow);
    return (uint16_t)(tmNow.tm_yday + 1); // +1 damit es Tagbasiert (1..366) 1.1. ist dann der 1. Tag
}

// TODO: echte Freigabe (Rainclick/Füllstand/Zeitfenster) noch nicht verdrahtet
bool ermittleFreigabe()
{
    return true;
}


void calculateEt0ForYesterday(uint16_t TagdesJahres)
{
    RaResult RaErgebnis;
    RaErgebnis = calc_Ra(TagdesJahres);

    ET0_gestern = calc_ET0(Temperatur_Durchschnitt_gestern, Temperatur_max_gestern, Temperatur_min_gestern, RaErgebnis.ra_mm);

    // ---- KO-Ausgabe ----
     KoBEW_Berechnung_ET0.value(ET0_gestern, DPT_Value_Temp);
}

// ---- Tageswechsel: gestern einfrieren, ET0 rechnen, heute zurücksetzen --
void Tageswechsel_Werte_speichern(uint16_t gestern)
{
    if (!gueltigeWerte_heute)
    {
        SERIAL_DEBUG.println("Bewaesserung: Tageswechsel ohne Temperaturdaten - überspringe");
    }
    else
    {
        Temperatur_max_gestern = Temperatur_max_heute;
        Temperatur_min_gestern = Temperatur_min_heute;
        Temperatur_Durchschnitt_gestern = Temperatur_Durchschnitt_heute;
    }
    Regenmenge_gestern = letzteRegenmengeHeute;

    calculateEt0ForYesterday(gestern);

    // "heute" zurücksetzen
    Temperatur_max_heute = letzteEmpfangeneTemperatur;
    Temperatur_min_heute = letzteEmpfangeneTemperatur;
    Temperatur_Durchschnitt_heute = letzteEmpfangeneTemperatur;
    gueltigeWerte_heute = false;

    letzteRegenmengeHeute = 0.0f; // Annahme: Regenmesser resettet ebenfalls täglich

    // min max Durchschnitswerte heute senden
    KoBEW_TDurchschnittGestern.value(Temperatur_Durchschnitt_gestern, DPT_Value_Temp);
    KoBEW_TMaxGestern.value(Temperatur_max_gestern, DPT_Value_Temp);
    KoBEW_TMinGestern.value(Temperatur_min_gestern, DPT_Value_Temp);
}

// ============================================================================
// Öffentliche Schnittstelle
// ============================================================================

void Bewaesserung_setup()
{
}

void Bewaesserung_loop()
{
    if (!openknx.time.isValid()) return;

    process_Bewaesserungsberechnung();

    // ---- Ventil-Timer: läuft die Bewässerung gerade, ist sie fertig? ----
    if (BewaesserungszoneVentilOffen)
    {
        uint32_t laufSek = (millis() - BewaesserungszoneVentilStartMillis) / 1000;
        if ((float)laufSek >= BewaesserungszoneGeplanteLaufzeitSek)
        {
            set_Ventil_State(RASENZONE_VENTIL_INDEX, false);
            BewaesserungszoneVentilOffen = false;

            // ---- Rückbuchung ----
            float bewaessert_mm = calc_Zugefuehrte_Wassermenge(BewaesserungszoneGeplanteLaufzeitSek, RASEN_NIEDERSCHLAGSRATE);
            BewaesserungszoneKonto = calc_Bodenwasserkonto_final(BewaesserungszoneKonto, bewaessert_mm, RASEN_NFK_MM);
            BewaesserungszoneGeplanteLaufzeitSek = 0.0f;

            SERIAL_DEBUG.print("WB Rasenzone: Bewässerung beendet, Konto final=");
            SERIAL_DEBUG.println(BewaesserungszoneKonto);
                // ---- KO-Ausgabe ----
            KoBEW__Wasserbilanzkonto.value(BewaesserungszoneKonto, DPT_Value_Temp);
         
        }
    }


}


void process_Temperatur_Wetterstation (float aktuelleTemperatur)
{

    // Sinnvolle Plausigrenzen für Außentemperatur (Sensorfehler abfangen)
    if (aktuelleTemperatur < -40.0f || aktuelleTemperatur > 60.0f)
    {
        SERIAL_DEBUG.print("Temperaturwert außerhalb Plausibereich verworfen: ");
        SERIAL_DEBUG.println(aktuelleTemperatur);
        return;
    }
    letzteEmpfangeneTemperatur = aktuelleTemperatur;

    if (!gueltigeWerte_heute)
    {
        Temperatur_max_heute = aktuelleTemperatur;
        Temperatur_min_heute = aktuelleTemperatur;
        Temperatur_Durchschnitt_heute = aktuelleTemperatur;
        gueltigeWerte_heute = true;
    }
    else
    {
        if (aktuelleTemperatur > Temperatur_max_heute) Temperatur_max_heute = aktuelleTemperatur;
        if (aktuelleTemperatur < Temperatur_min_heute) Temperatur_min_heute = aktuelleTemperatur;
    }

    // Mittelwert bilden.
    Temperatur_Durchschnitt_heute = (aktuelleTemperatur + Temperatur_Durchschnitt_heute) / 2;

    // min max Durchschnitswerte heute senden
    KoBEW_TDurchschnittHeute.value(Temperatur_Durchschnitt_heute, DPT_Value_Temp);
    KoBEW_TMaxHeute.value(Temperatur_max_heute, DPT_Value_Temp);
    KoBEW_TMinHeute.value(Temperatur_min_heute, DPT_Value_Temp);
}

void process_Regenmenge_Wetterstation (uint16_t iKoNumber, float regenmengeHeuteMm)
{
    if (regenmengeHeuteMm < 0.0f)
    {
        SERIAL_DEBUG.print("Regenmenge negativ verworfen: ");
        SERIAL_DEBUG.println(regenmengeHeuteMm);
        return;
    }
    letzteRegenmengeHeute = regenmengeHeuteMm;
}


bool ET0_processCommand(const std::string cmd, bool debugKo)
{
    if (cmd == "et0" && !debugKo)
    {
        SERIAL_DEBUG.print("ET0 (Tag ");
        SERIAL_DEBUG.print(LastDayOfYear);
        SERIAL_DEBUG.print("): ");
        SERIAL_DEBUG.println(LastEt0);
        SERIAL_DEBUG.print("  Tmax/Tmin/Tmean gestern: ");
        SERIAL_DEBUG.print(Temperatur_max_gestern);
        SERIAL_DEBUG.print(" / ");
        SERIAL_DEBUG.print(Temperatur_min_gestern);
        SERIAL_DEBUG.print(" / ");
        SERIAL_DEBUG.println(Temperatur_Durchschnitt_gestern);
        SERIAL_DEBUG.print("  dr/delta/omegaS/Ra_mm: ");
        SERIAL_DEBUG.print(LastDr);
        SERIAL_DEBUG.print(" / ");
        SERIAL_DEBUG.print(LastDelta);
        SERIAL_DEBUG.print(" / ");
        SERIAL_DEBUG.print(LastOmegaS);
        SERIAL_DEBUG.print(" / ");
        SERIAL_DEBUG.println(LastRaMm);
        SERIAL_DEBUG.print("  Rasenzone Konto/Bedarf/Laufzeit: ");
        SERIAL_DEBUG.print(BewaesserungszoneKonto);
        SERIAL_DEBUG.print(" / ");
        SERIAL_DEBUG.print(BewaesserungszoneBedarf);
        SERIAL_DEBUG.print(" / ");
        SERIAL_DEBUG.println(BewaesserungszoneGeplanteLaufzeitSek);
        return true;
    }
    return false;
}

// ============================================================================
// Werte müssen über einen Neustart hinweg erhalten bleiben,
// sonst geht bei jedem Reset mitten am Tag die bisherigen Tageswerte
// verloren (z.B. Neustart um 14 Uhr -> Tmax/Tmin ab 14 Uhr statt ab
// 00:00). Einbindung in GardenControlDevice::readFlash/writeFlash/flashSize
//
// Byte-Layout EXAKT in Schreibreihenfolge (siehe writeFlash)
// ============================================================================
static constexpr uint16_t BEWAESSERUNG_FLASH_SIZE = 4 + 4 + 4 + 1 + 4 + 4 + 4 + 4 + 4;
// magicYday(4) tmaxToday(4) tminToday(4) todayHasData(1)
// tmaxYesterday(4) tminYesterday(4) tmeanYesterday(4) regenGestern(4) konto(4)

uint16_t Bewaesserung_flashSize()
{
    return BEWAESSERUNG_FLASH_SIZE;
}

void Bewaesserung_writeFlash()
{
    uint32_t currentYday = openknx.time.isValid() ? getYearDay() : 0;

    openknx.flash.writeInt(currentYday);
    openknx.flash.writeFloat(Temperatur_max_heute);
    openknx.flash.writeFloat(Temperatur_min_heute);
    openknx.flash.writeByte(gueltigeWerte_heute ? 1 : 0);
    openknx.flash.writeFloat(Temperatur_max_gestern);
    openknx.flash.writeFloat(Temperatur_min_gestern);
    openknx.flash.writeFloat(Temperatur_Durchschnitt_gestern);
    openknx.flash.writeFloat(Regenmenge_gestern);
    openknx.flash.writeFloat(BewaesserungszoneKonto); // MUSS über Neustart erhalten bleiben!
}


void Bewaesserung_readFlash(const uint8_t* buffer, const uint16_t size)
{
    if (size < BEWAESSERUNG_FLASH_SIZE) return;

    uint32_t magicYday;
    float tmaxToday, tminToday, tmaxYesterday, tminYesterday, tmeanYesterday, regenGestern, konto;
    uint8_t todayHasData;

    uint16_t o = 0;
    memcpy(&magicYday, buffer + o, 4); o += 4;
    memcpy(&tmaxToday, buffer + o, 4); o += 4;
    memcpy(&tminToday, buffer + o, 4); o += 4;
    memcpy(&todayHasData, buffer + o, 1); o += 1;
    memcpy(&tmaxYesterday, buffer + o, 4); o += 4;
    memcpy(&tminYesterday, buffer + o, 4); o += 4;
    memcpy(&tmeanYesterday, buffer + o, 4); o += 4;
    memcpy(&regenGestern, buffer + o, 4); o += 4;
    memcpy(&konto, buffer + o, 4);

    // gestrige Werte gelten unabhängig vom Tag weiterhin als gültige Basis
    Temperatur_max_gestern = tmaxYesterday;
    Temperatur_min_gestern = tminYesterday;
    Temperatur_Durchschnitt_gestern = tmeanYesterday;

    Regenmenge_gestern = regenGestern;
    BewaesserungszoneKonto = konto; // <- der wichtige Teil: Kontostand übersteht den Neustart

    // heutige gespeicherte Werte nur übernehmen, wenn sie tatsächlich von HEUTE sind
    if (openknx.time.isValid() && (uint32_t)getYearDay() == magicYday)
    {
        Temperatur_max_heute = tmaxToday;
        Temperatur_min_heute = tminToday;
        gueltigeWerte_heute = (todayHasData != 0);
        return;
    }

    // Zeit noch nicht gültig ODER Tag hat sich seit dem letzten Speichern
    // geändert -> heutige Aggregation lieber frisch beginnen, statt
    // veraltete/falsche Werte weiterzuschleppen.
    Temperatur_max_heute = -4.2;
    Temperatur_min_heute = -4.2;
    gueltigeWerte_heute = false;
}


void process_Bewaesserungsberechnung(void)
{
    if (!openknx.time.isValid())
    {
        return; // Uhr hat noch kein gültiges Datum vom Bus
    }
    else
    {
        uint16_t heute = getYearDay();

        if (letzterBekannterTag == -1)
        {
            // erster gültiger Aufruf nach Neustart - nur merken, NICHT als
            // Tageswechsel werten (sonst würde beim Boot sofort "gestern"
            // mit leeren Werten überschrieben)
            letzterBekannterTag = heute;
            return;
        }

        else if (heute != letzterBekannterTag)
        {
            // plausi, letzerbekannter tag sollte heute -1 sein, bzw 366 zu 1
            Tageswechsel_Werte_speichern((uint16_t)letzterBekannterTag);
            letzterBekannterTag = heute;
        }
    
            // ---- Bewässerungsstart-Trigger (einmal pro Tag) ----
        if (BewaesserungszoneBedarf && !BewaesserungszoneVentilOffen && letzterBewaesserungsTag != heute &&
            tmNow.tm_hour == BEWAESSERUNG_START_STUNDE && tmNow.tm_min == 0)
        {
            set_Ventil_State(RASENZONE_VENTIL_INDEX, true);
            BewaesserungszoneVentilOffen = true;
            BewaesserungszoneVentilStartMillis = millis();
            letzterBewaesserungsTag = heute;
            SERIAL_DEBUG.print("WB Rasenzone: Bewässerung gestartet, Laufzeit[s]=");
            SERIAL_DEBUG.println(BewaesserungszoneGeplanteLaufzeitSek);
        }
    }
}
