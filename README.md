# Spinnerino Plus
Un giro nel passato ora anche con Laser disc e Neo geo oltre tanti altri arcade.

## 📘 Descrizione

Mini arcade dedicato ai titoli con controllo a manopola con grandi classici come Arkanoid. Un concentrato di retro-gaming e DIY in un formato unico.
In questa nuova variante ora si può giocare a tutti i giochi anche con joystick.
Un progetto di Marco Prunca basato su ESP32-P4. 

<img width="1365" height="768" alt="IMDd_WONGaLjo copia" src="https://github.com/user-attachments/assets/ba6a95f5-6b7e-46bb-9fc2-a197fe884598" />
<img width="1365" height="768" alt="IMMfozfteeKR8" src="https://github.com/user-attachments/assets/22dfa851-767b-458c-8899-96cc7aa844d3" />


🎥 Video tutorial: https://youtu.be/VE9SN8RUckk



---
    COMANDI:
 
    Gioca:            -Ruota l'encoder/paddle/spinner. 
                      -Premi manopola (COIN) encoder per inserire credito. 
                      -Premi pulsante FIRE per avviare il gioco.
 
    REGOLA VOLUME:    Tieni premuto FIRE per ~3 secondi
                      -> compare la barra volume verde. Poi RUOTA l'encoder:
                      orario (destra) = alza, antiorario (sinistra) = abbassa.
                      RILASCIA FIRE per confermare e salvare (NVS). Durante la
                      regolazione il paddle resta fermo.
 
    ESCI DAL GIOCO:   tieni premuto (COIN) per ~3 secondi -> torna al menu giochi.
 
    IMPOSTAZIONI:     nel menu giochi, dopo l'ultimo gioco c'e' la voce
                      IMPOSTAZIONI per regolare la sensibilita' dell'encoder
                      (spinner) per ogni gioco.
    
    TEST:             E' presente un menù per il test hardware di tutti i pulsanti e oystick

    FILE MANAGER:     Attraverso questa funzione è possibile tramite il WIFI colegarsi allo spinnerino per aggiornare i file sulla SD-Interna
 
 
 Una volta recuperati tutti i file ZIP inseriteli nella cartella SPINNERINO\ROMS
 
 * Per convertire le ROM:
 - WINDOWS: Lanciare il file convert_all.bat nella cartella \ROM_CONVERTER (serve avere installato python)

---
## 🎛️ Schema pratico di assemblaggio

[SpinnerinoPlus.pdf](https://github.com/user-attachments/files/33302311/SpinnerinoPlus.pdf)

---
## 🎛️ Materiali

* Display con encoder 2.4": https://amzn.to/4j3imzh
* ESP32 P4 WIFI: https://amzn.to/3VtRGxO
* Joystick 10x10x9mm: https://s.click.aliexpress.com/e/_c3f7PQub
* Pulsanti 4x4x4.3mm: https://s.click.aliexpress.com/e/_c35iN1q1
* BMS FM5324: https://s.click.aliexpress.com/e/_c3Ug7Bln
* BMS IP5306: https://s.click.aliexpress.com/e/_c35Euvxf
* BMS CD42: https://s.click.aliexpress.com/e/_c4L3E7op

* Altoparlante 4230 4ohm 3W: https://s.click.aliexpress.com/e/_c3zBnIV3
* Batteria: https://amzn.to/4omnK15
* CAVO USBC 2P: https://amzn.to/4itQRi6
* Interruttore a bilanciere 10x15: https://amzn.to/4yXhAsz

---
# 🎛️ Programmazione ESP32P4

Arduino IDE settings (esp32 core >= 3.3.8):
*   Board:                 ESP32P4 Dev Module
*   Flash Size:            32MB
*   PSRAM:                 Enabled
*   Partition Scheme:      SPINNERINO (da creare con apposito batch file)
*   USB Mode:              Hardware CDC and JTAG
*   USB CDC On Boot:       Disabled
*   Upload Speed:          921600

---
# 📝 Revisioni

 * V1.0 - 09/26 - First Release
 * V1.1 - 10/26 - Added WIFI File manager (C6 V1.1 needed), added gapless, fixed: Moon Patrol, Time Pilot, Race USA
 * V1.2 - 10/26 - Increased file transfer speed (C6 V1.2 needed)
 * V1.3 - 10/26 - Fixed Fantasy, Super Mario. Added View Point and Pulstar

---
## 🧾 Licenza

Questo progetto è distribuito con licenza
**Creative Commons – Attribuzione – Non Commerciale 4.0 Internazionale (CC BY-NC 4.0)**.

Puoi condividerlo e modificarlo liberamente, **citando l’autore**
(Davide Gatti / [survivalhacking](https://github.com/survivalhacking)) e **senza scopi commerciali**.

🔗 [https://creativecommons.org/licenses/by-nc/4.0/](https://creativecommons.org/licenses/by-nc/4.0/)

