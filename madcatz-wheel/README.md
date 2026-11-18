# MadCatz Wireless Force Feedback Racing Wheel (Xbox 360) su Windows 11

Driver in user-mode che rende il volante utilizzabile nei giochi di guida su Windows 11:
**assi separati** per sterzo, acceleratore e freno, **tutti i pulsanti** mappati e
**force feedback** inoltrato dal gioco ai motori del volante.

## Il problema

Il volante comunica tramite il **ricevitore wireless Xbox 360 per Windows**. Windows 11 lo
gestisce con il driver Microsoft `xusb22` come dispositivo **XInput** (sottotipo *Wheel*).
I giochi che usano DirectInput (la maggior parte dei simulatori) però lo vedono così:

- acceleratore e freno **combinati su un solo asse**: non puoi premerli insieme e la corsa si dimezza
- **nessun force feedback** tramite DirectInput
- tasto Guide non disponibile

## La soluzione

`madcatz_wheel.exe` legge il volante via XInput e lo pubblica come periferica DirectInput
virtuale tramite **vJoy** (driver firmato, installabile su Windows 11 senza modalità test).
Il gioco invia il force feedback a vJoy, il programma calcola la forza risultante e la invia
ai motori del volante.

```
Volante ──radio──> Ricevitore Xbox 360 ──xusb22──> XInput ──> madcatz_wheel.exe ──> vJoy (DirectInput) ──> Gioco
                                                    ^                   │                       │
                                                    └── motori FFB ─────┘<──── effetti FFB ─────┘
```

Non serve un driver kernel nuovo: un driver kernel non firmato non si può installare su
Windows 11 senza disattivare Secure Boot o attivare la modalità test, mentre vJoy è già firmato.

## Mappatura

### Assi

| Comando fisico         | XInput          | vJoy / DirectInput         | Range              |
|------------------------|-----------------|----------------------------|--------------------|
| Volante (sterzo)       | `sThumbLX`      | **X**                      | sx 0 → centro 16384 → dx 32767 |
| Pedale acceleratore    | `bRightTrigger` | **Y** (separato)           | rilasciato 0 → a fondo 32767 |
| Pedale freno           | `bLeftTrigger`  | **Z** (separato)           | rilasciato 0 → a fondo 32767 |
| (asse secondario)      | `sThumbLY`      | Rx                         | normalmente al centro |
| (asse secondario)      | `sThumbRX`      | Ry                         | normalmente al centro |
| (asse secondario)      | `sThumbRY`      | Rz                         | normalmente al centro |

Con `mode = combined` in `[pedals]` si ottiene un asse unico (Y) per i giochi vecchi che lo richiedono.

### Pulsanti

| Pulsante sul volante | XInput            | Pulsante vJoy |
|----------------------|-------------------|---------------|
| A                    | `A`               | 1  |
| B                    | `B`               | 2  |
| X                    | `X`               | 3  |
| Y                    | `Y`               | 4  |
| LB                   | `LEFT_SHOULDER`   | 5  |
| RB                   | `RIGHT_SHOULDER`  | 6  |
| Back                 | `BACK`            | 7  |
| Start                | `START`           | 8  |
| LS (clic levetta sx) | `LEFT_THUMB`      | 9  |
| RS (clic levetta dx) | `RIGHT_THUMB`     | 10 |
| Guide (logo Xbox)    | `GUIDE`           | 11 |
| Croce Su             | `DPAD_UP`         | 12 + POV 0° |
| Croce Giù            | `DPAD_DOWN`       | 13 + POV 180° |
| Croce Sinistra       | `DPAD_LEFT`       | 14 + POV 270° |
| Croce Destra         | `DPAD_RIGHT`      | 15 + POV 90° |

La croce è anche un POV continuo con le diagonali (45°, 135°, 225°, 315°).

> **Paddle del cambio.** Il volante invia i paddle dietro la corona come normali pulsanti
> XInput. Per sapere esattamente a quali corrispondono sul tuo esemplare, avvia
> `madcatz_wheel.exe --monitor` e premi ogni paddle: il nome compare nella colonna "Tasti".
> Se vuoi, puoi assegnarli a numeri vJoy diversi nella sezione `[buttons]` del file `.ini`.
> Ogni pulsante si può spostare o disattivare (`= 0`).

### Force feedback

Effetti gestiti: Constant, Ramp, Sine, Square, Triangle, Sawtooth Up/Down, Spring, Damper,
Friction, Inertia, con guadagno per effetto e guadagno del dispositivo, Start / Solo / Stop,
loop, durata, Stop All / Reset / Pause / Continue.

Il volante ha un motore comandato dai due canali "rumble" di XInput. In modalità
`directional` la forza verso sinistra va al motore sinistro e quella verso destra al motore
destro. Usa `--ffb-test` per verificare il comportamento del tuo volante e regolare `mode`,
`invert` e `min_force`.

## Installazione

### 1. Ricevitore e volante

1. Collega il **Xbox 360 Wireless Receiver for Windows** a una porta USB.
   Windows 11 installa da solo il driver (Windows Update).
   - **Ricevitore compatibile (clone) non riconosciuto** ("Unknown device"): *Gestione dispositivi* →
     clic destro sul dispositivo → *Aggiorna driver* → *Cerca il software del driver nel computer* →
     *Scegli da un elenco di driver disponibili* → **Periferiche Xbox 360** →
     **Ricevitore wireless Xbox 360 per Windows** → conferma l'avviso.
2. Inserisci le batterie (o il pacco batteria) nel volante e accendilo con il tasto Guide.
3. Premi il pulsante di connessione sul ricevitore, poi quello sul volante: la spia si stabilizza.
4. Verifica: `Win+R` → `joy.cpl` deve elencare il controller.

### 2. vJoy

1. Installa **vJoy 2.1.9.1** (build firmata per Windows 10/11, repository GitHub `jshafer817/vJoy`, file `vJoySetup.exe`).
2. Apri **Configure vJoy** e imposta il dispositivo **1** così:
   - Assi: **X, Y, Z, Rx, Ry, Rz**
   - Pulsanti: **16**
   - POV: **1**, tipo **Continuous**
   - **Enable Effects** spuntato e tutti gli effetti selezionati
   - *Apply*

### 3. Programma

1. Copia in una cartella (es. `C:\MadCatzWheel\`):
   - `madcatz_wheel.exe` (da `dist\` o compilato da te)
   - `madcatz_wheel.ini`
2. Avvia `madcatz_wheel.exe --list`: il volante deve risultare **"Volante (Wheel)"**.
3. Avvia `madcatz_wheel.exe --ffb-test` e regola la sezione `[ffb]` come indicato a schermo.
4. Avvia `madcatz_wheel.exe` e lascialo aperto mentre giochi.
   `joy.cpl` → *vJoy Device* → *Proprietà* mostra assi e pulsanti in tempo reale.
5. (Facoltativo) `avvio_automatico.bat` lo avvia a ogni accesso a Windows.

### 4. Nei giochi

- Seleziona **vJoy Device** come volante/controller e assegna i comandi.
- Se il gioco vede sia il volante originale sia vJoy e i comandi arrivano due volte,
  nascondi il dispositivo originale ai giochi DirectInput con **HidHide**
  (repository GitHub `nefarius/HidHide`) oppure disattivalo nelle impostazioni del gioco.
  Il programma continua a leggere il volante tramite XInput anche quando è nascosto.

## Comandi

| Comando                        | Funzione |
|--------------------------------|----------|
| `madcatz_wheel.exe`            | avvio normale |
| `madcatz_wheel.exe --list`     | elenca i controller XInput e il loro tipo |
| `madcatz_wheel.exe --monitor`  | valori grezzi di assi e pulsanti in tempo reale (non richiede vJoy) |
| `madcatz_wheel.exe --ffb-test` | prova i motori per calibrare il force feedback |

## Compilazione

- **Visual Studio 2022 / Build Tools**: apri "x64 Native Tools Command Prompt" ed esegui `build.bat`.
- **MinGW-w64**:
  `x86_64-w64-mingw32-g++ -O2 -std=c++17 -municode -static src/main.cpp -o madcatz_wheel.exe -lwinmm`

Non serve l'SDK di vJoy: `vJoyInterface.dll` viene caricata a runtime dalla cartella del
programma o da `C:\Program Files\vJoy\x64\`.

## Risoluzione problemi

| Sintomo | Soluzione |
|---|---|
| `vJoyInterface.dll non trovata` | Installa vJoy oppure copia `vJoyInterface.dll` (x64) accanto all'exe |
| `asse ... non abilitato` | Abilita X, Y, Z (e Rx, Ry, Rz) in *Configure vJoy* |
| `FFB disattivato` | Spunta *Enable Effects* in *Configure vJoy* |
| Il volante tira dal lato sbagliato | `[ffb] invert = 1` |
| FFB debole / il volante non si muove con forze piccole | aumenta `min_force` (10-25) e/o `gain` |
| Volante solo vibrante | `[ffb] mode = rumble` |
| Il centro non è a zero | `[steering] center_offset` |
| `pkt 0` nello stato durante il gioco | il gioco non sta inviando FFB a vJoy: abilita il force feedback nelle opzioni del gioco |
| Windows SmartScreen blocca l'exe | *Ulteriori informazioni* → *Esegui comunque* (l'eseguibile non è firmato) |
