# PIR szenzor node (MeshCore v1.18.0 + PIR)

Forrás: github.com/Meanius/MeshCore, `pir-sensor` ág, env `Xiao_S3_WIO_pir_sensor`
Hardver: Seeed XIAO ESP32-S3 + Wio-SX1262, PIRMINI-3 mozgásérzékelő
Kész bin: a GitHub Release-ben (`*-merged.bin`)

## Bekötés
- PIRMINI-3: 1 = 3V3, 2 (OUT) = D0, 3 = GND. A XIAO 3V3 lábáról táplálva, ne az akkuról.
- Külső lehúzó ellenállás nem kell: bekötve a PIR maga húzza le a kimenetét.
  Ha mégis legyen biztosíték egy kilazuló vezeték ellen: 1 MOhm a D0 és a GND közé.
  100 kOhm vagy kisebb TILOS: a PIR kimenete gyenge (kb. 37 kOhm), az ESP32 akkor nem érzékeli a jelet.
- TP4056: B+/B- a 18650-re, OUT+/OUT- a XIAO BAT+/BAT- pontjaira, kapcsoló az OUT+ ágban.
- Antenna MINDIG legyen fent, mielőtt áramot kap.

## Flashelés
- Web flasher (flasher.meshcore.io), custom firmware, a `*-merged.bin`.
- Az ELSŐ flashnél, ha a lapon más firmware fut: a XIAO "B" gombját nyomva tartva dugd be az USB-t.
  A port neve ekkor "USB JTAG/serial debug unit". Ha a mi firmware-ünk már fut, gomb nélkül is megy.
- Erase device NÉLKÜL flashelj, különben elvész a rádió-beállítás, a jelszó, a csatorna és a belépések.
- "Serial data stream stopped" hibánál elég egy Retry.

## Első beállítás (soros konzol, 115200 baud, vagy belépés után az app parancssora)
1. Rádió a kliensével egyezzen. A "Hungary" profil: `set radio 869.618,62.5,7,5`, `set path.hash.mode 1`, `reboot`.
   Ellenőrzés: `get radio`. Eltérő SF-fel a két node nem hallja egymást.
2. A node induláskor hirdeti magát; ha a kliensen nem jelenik meg: `advert`.
3. Belépés a kliens appjából (a PIR kontaktra koppintva), gyári jelszó: `password`. Utána azonnal `password <új jelszó>`.
4. Privát riasztási csatorna: az appban hozz létre egy privát csatornát, és a kulcsát add meg a node-nak:
   `pir channel <kulcs>` (32 hexa karakter vagy base64). A válaszban egy kétjegyű hash áll; a `pir channel` parancs később is kiírja.
   A kulcsot csak annak add oda, aki a riasztásokat olvashatja.

## Működés
- Mozgáskor "PIR: Mozgás érzékelve!" üzenet megy a privát csatornára. Ezt az app megjeleníti és értesít róla.
- Mellette közvetlen riasztás is megy minden belépett, feliratkozott kliensnek. Az app ezt szenzor-feladótól
  NEM jeleníti meg (nincs hozzá beszélgetés-nézet), ezért a csatorna a valódi értesítési út.
- Két riasztás között legalább `suspend` mp telik el (alap 60). Bekapcsolás után az első 30 mp-ben nincs riasztás.
- A node-on nincs elemes óra: áramtalanítás után az óra 2026. március 1-re áll vissza, és a riasztások régi dátummal jönnek,
  amíg valaki adminként be nem lép. A belépés a kliens idejére állítja az órát. Kézzel: `clock sync` (belépve), lekérdezés: `clock`.
- A node repeaterként is továbbít (kikapcsolható, lásd lent).

## Parancsok
| Parancs | Hatás |
|---|---|
| `pir channel <kulcs>` | privát riasztási csatorna beállítása (megmarad) |
| `pir channel off` / `pir channel` | csatorna kikapcsolása / állapota |
| `pir on` / `pir off` / `pir` | a parancsot kiadó kliens közvetlen riasztásának be/ki/állapota (csak belépve) |
| `suspend` / `suspend <mp>` | a riasztások közti minimum (0-86400, alap 60; megmarad) |
| `pir test` | azonnal küld egy riasztást mozgás nélkül |
| `pir pin` | a D0 szintje (nyugalomban LOW) és az eddigi jelek száma |
| `clock` / `clock sync` | óra lekérdezése / a belépett kliens idejére állítása |
| `get radio` / `set radio f,bw,sf,cr` | rádió (beállítás után `reboot`) |
| `password <új>` | admin jelszó |
| `set name <név>` | node neve |
| `set repeat off` / `set repeat on` | csomagtovábbítás ki/be |
| `advert` | azonnali hirdetés |
| `ver` | firmware verzió |
| `reboot` | újraindítás |

## Hibakeresés
- Soros konzolon minden mozgás-jel egy sort ír: `PIR edge #N: alert queued` / `suppressed (suspend, X s left)` / `ignored (warm-up)`.
- Ha mozgás nélkül özönlenek a jelek: a D0 lebeg (kiesett a PIR vezetéke).
- Ha mozgásra nincs `PIR edge` sor: `pir pin` mozgás közben HIGH-ra kell váltson; mérd a PIR OUT feszültségét (bekötve legalább kb. 2,5 V kell).
