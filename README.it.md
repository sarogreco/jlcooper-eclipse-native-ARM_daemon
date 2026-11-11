# Demone per il pannello JLCooper Eclipse e DaVinci Resolve — versione nativa Apple Silicon (non ufficiale)

*[Read in English](README.md)*

DaVinci Resolve comunica con il pannello **JLCooper Eclipse** (Ethernet) tramite un piccolo programma di
supporto, `JLCooperPanelDaemon`, contenuto nell'app di Resolve. Quel programma è **solo x86_64**: su Apple
Silicon gira tramite Rosetta 2, e Apple sta eliminando Rosetta dalle prossime versioni di macOS. Senza Rosetta
il pannello non viene più rilevato ("bad CPU type in executable").

Questo progetto è un **sostituto nativo** di quel programma. Parla gli stessi due protocolli (quello TCP del
pannello e quello su socket locale di Resolve), accetta le stesse opzioni da riga di comando e si compila come
normale eseguibile macOS arm64 (o x86_64).

> **Non ufficiale.** Non è prodotto, approvato né supportato da Blackmagic Design o JLCooper. Sostituire un file
> dentro l'app di Resolve è una tua scelta e a tuo rischio: vedi l'[avviso legale](#avviso-legale) e la
> [licenza](LICENSE). Se Blackmagic Design rilascia una versione nativa del suo programma, usa quella.

## Stato

Provato dal manutentore su una sola configurazione:

| | |
|---|---|
| Pannello | JLCooper Eclipse, Ethernet (risposta ID del pannello `03 0b 18 00 00 02`) |
| Resolve | DaVinci Resolve Studio 21.1 |
| macOS | 27.2, Apple Silicon |

Con Resolve che usa il sostituto (senza Rosetta) tutto ciò che è stato provato funziona: trackball e anelli,
manopole, jog e shuttle, tasti, tasti di modalità con LED ed etichette LCD. Su quel pannello è stato usato anche
il [file di mappatura](docs/MAPPING.md) facoltativo: con SHIFT, le manopole 1–3 della pagina Offset comandano
Color Temp, Tint e Highlights in Resolve (poi è stata aggiunta una quarta, Shadows) mentre è aperta una palette
colore (Color Wheels, Curves, Color Slice, Color Warper): Resolve lega quegli ID alla palette aperta, vedi le
[note](docs/MAPPING.md#these-ids-depend-on-the-palette-that-is-open) (in inglese).

Su quel pannello sono state fatte funzionare anche le tre luci delle trackball e il display Time Code (che
l'helper originale non pilota mai), vedi [docs/TIMECODE.md](docs/TIMECODE.md) (in inglese).

**Non verificato su hardware reale:** il simbolo DF (drop frame) del display Time Code (non si è riusciti ad
accenderlo), gli encoder 7–11, le combinazioni con SHIFT diverse da quelle dei test, altre revisioni
hardware/firmware dell'Eclipse, altre versioni di Resolve o macOS. Il comportamento è coperto da 157 controlli automatici (pannello e Resolve
simulati) e da prove con sanitizer e fuzzing, ma un simulatore dimostra solo che il codice fa ciò che
l'autore *crede* faccia l'originale. **I rapporti di prova sono molto graditi**: vedi [Contribuire](#contribuire).

## Scaricare una versione già pronta (senza Xcode)

Solo Apple Silicon. Scarica lo zip dall'[ultima release](https://github.com/sarogreco/jlcooper-eclipse-native-ARM_daemon/releases/latest).
È compilato da GitHub Actions a partire dal sorgente di questo repository e provato con gli stessi test
automatici prima della pubblicazione; è firmato solo ad hoc, non notarizzato. Decomprimilo, apri il Terminale
nella cartella decompressa e controllalo prima di usarlo:

```sh
xattr -dr com.apple.quarantine .      # toglie il flag "scaricato da internet"
file JLCooperPanelDaemon              # deve dire: Mach-O 64-bit executable arm64
shasum -a 256 JLCooperPanelDaemon     # deve coincidere con la riga di SHA256SUMS nella pagina della release
```

Poi prosegui con [Installarlo in Resolve](#installarlo-in-resolve), usando questo file come binario. Stai per
eseguirlo con `sudo` dentro l'app di Resolve: se preferisci non fidarti di un file già compilato, compilalo
tu qui sotto.

## Compilare

Servono gli strumenti da riga di comando di Xcode (`xcode-select --install`).

Scarica il repository (pulsante verde *Code* → *Download ZIP*, oppure `git clone`), apri il Terminale nella
sua cartella ed esegui:

```sh
make
file build/JLCooperPanelDaemon      # Mach-O 64-bit executable arm64   (x86_64 su un Mac Intel)
```

oppure, senza `make`:

```sh
clang++ -std=c++17 -O2 -Wall -o JLCooperPanelDaemon src/JLCooperPanelDaemon.cpp
```

## Provarlo senza Resolve (non modifica nulla)

Chiudi Resolve e ferma il programma originale: il pannello accetta una sola connessione.

```sh
pkill -x JLCooperPanelDaemon
build/JLCooperPanelDaemon -V 192.168.254.101:23      # usa l'indirizzo del tuo pannello
```

Dopo circa un secondo il pannello deve spegnere tutti i LED, accendere il LED della modalità 0, mostrare le
etichette sugli LCD e `--------` sul display a 7 segmenti. Tasti e manopole scrivono righe di log
(`Received button 1 press`, `Rotor [1] has been turned by [5]`, …). `Ctrl‑C` per uscire. Il Mac e il pannello
devono avere indirizzi diversi nella stessa sottorete (macOS avvisa se un altro dispositivo usa l'IP del Mac).

## Installarlo in Resolve

Resolve avvia da solo il programma dall'interno del proprio pacchetto. Fai una copia dell'originale, copia sopra
il nuovo binario e firmalo ad hoc:

```sh
APP="/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/Applications"

# 1. copia dell'originale — solo se non esiste già, così rieseguire il comando non sovrascrive
#    mai l'originale con il binario nuovo
[ -e ~/JLCooperPanelDaemon.original ] || sudo cp -p "$APP/JLCooperPanelDaemon" ~/JLCooperPanelDaemon.original

# 2. installa e firma (BIN: il file che hai compilato, o quello dello zip della release)
BIN=build/JLCooperPanelDaemon
sudo cp "$BIN" "$APP/JLCooperPanelDaemon"
sudo codesign --force --sign - "$APP/JLCooperPanelDaemon"
```

macOS può chiedere al Terminale il permesso *Gestione app* (Impostazioni di Sistema → Privacy e sicurezza).
Avvia Resolve e controlla quale programma sta girando:

```sh
ps -axo pid,ppid,args | grep -i "[J]LCooper"
file "$APP/JLCooperPanelDaemon"
```

Resolve 21.1 lo lancia come `JLCooperPanelDaemon -s <fd> -l <…/logs/panel.log> <ip-pannello>:23`. In Monitoraggio
Attività la colonna *Tipo* mostra **Apple** per un processo nativo e **Intel** per uno sotto Rosetta.

**Tornare indietro**, in qualsiasi momento:

```sh
sudo cp -p ~/JLCooperPanelDaemon.original "$APP/JLCooperPanelDaemon"      # l'originale richiede Rosetta
```

**Gli aggiornamenti di Resolve rimettono il programma originale** x86_64. Dopo un aggiornamento esegui il
comando `file` qui sopra: se dice `x86_64`, ripeti il passo 2 (conserva il binario compilato in un posto sicuro).

## Risoluzione dei problemi

* Non succede nulla in Resolve: chiudi Resolve, avvia il demone a mano con `-V -l ~/jlc.log <ip>:23` come sopra
  e osserva il pannello e il log.
* Il `panel.log` di una sessione di Resolve con questa versione è normalmente vuoto: gli eventi vengono scritti
  solo con `-V`, che Resolve non passa. È normale, non è un errore.
* Per avere un log dettagliato *dentro* una sessione di Resolve si può mettere un piccolo script al posto del
  demone (non verificato con Resolve, ma `exec` conserva il descrittore che Resolve passa con `-s`):

  ```sh
  cd "$APP"
  sudo mv JLCooperPanelDaemon JLCooperPanelDaemon.native
  printf '#!/bin/sh\nexec "$(dirname "$0")/JLCooperPanelDaemon.native" -V "$@"\n' | sudo tee JLCooperPanelDaemon >/dev/null
  sudo chmod +x JLCooperPanelDaemon
  ```

  Per ripristinare: `sudo mv JLCooperPanelDaemon.native JLCooperPanelDaemon`.

## Opzioni

Come il programma originale: `JLCooperPanelDaemon [-hvVdr] [-s fd] [-l file] [host[:porta]]`

| opzione | significato |
|---|---|
| `-s fd` | usa il socket già connesso `fd` per parlare con Resolve (come lo avvia Resolve) |
| `-l file` | scrive errori e log su `file` invece che su stderr |
| `-c file` | [file di mappatura](docs/MAPPING.md) opzionale (predefinito `~/Library/Application Support/JLCooperPanelDaemon/mapping.conf`) |
| `-V` | log dettagliato |
| `-d` | mostra l'ultimo tasto premuto sul display a 7 segmenti |
| `-r` | accettata per compatibilità; questa versione riprova sempre la connessione al pannello |
| `-v`, `-h` | versione, aiuto |

Senza `-s` il demone ascolta su `/var/tmp/davinci_socket` come l'originale. Indirizzo predefinito del pannello:
`192.168.254.101:23`.

## Differenze dall'originale

* Si riconnette da solo al pannello se la connessione cade o se il pannello è spento all'avvio.
* Non si blocca durante la pausa di un secondo dopo la connessione.
* Controlla le lunghezze dei pacchetti ricevuti da Resolve.
* Il debug sugli LCD (`-d`) è ridotto all'indicazione sul display a 7 segmenti.
* `-v` stampa la stessa riga di versione (`… version 7.1`) ma una seconda riga diversa.
* **In più, facoltativo:** un [file di mappatura](docs/MAPPING.md) (in inglese) può dare alle manopole un livello
  SHIFT (per esempio Color Temp, Tint e Highlights, che la mappatura originale non usa) e ha una modalità di
  scansione per trovare gli ID dei controlli di Resolve. Senza il file il comportamento è quello originale.
* **In più, facoltativo:** le tre luci delle trackball possono essere accese nel colore che vuoi (`ballcolors` nel
  file di mappatura), i tasti possono essere illuminati per la sala buia (`keylights`) e il display Time Code può mostrare il timecode della testina di Resolve, passato tramite un
  piccolo file di testo da uno script che può partire al login. Vedi [docs/TIMECODE.md](docs/TIMECODE.md) (in
  inglese).

## Come funziona

Il pannello parla con frame `F0 15 25 … F7` su TCP porta 23; Resolve con pacchetti "Steinway" da 12 byte di
intestazione su un socket unix locale. Il demone traduce tra i due ed esegue la logica di modalità/SHIFT/manopole
del pannello e l'emulazione dello shuttle. Dettagli in [docs/PROTOCOL.md](docs/PROTOCOL.md) (in inglese).

## Test

```sh
make test        # pannello e Resolve simulati su socketpair (come l'avvio con -s di Resolve)
make test-uds    # idem, più la modalità con socket in ascolto su /var/tmp/davinci_socket
make asan        # build con sanitizer, test, una prova di fuzzing e una del parser del file di mappatura
```

I test dipendono dai tempi: eseguili su una macchina inattiva. Non toccano `/var/tmp/davinci_socket` se
qualcosa (per esempio un Resolve aperto) è già in ascolto.

## Contribuire

I rapporti da altri pannelli e da altre versioni di Resolve/macOS sono ciò che serve di più adesso: apri una
segnalazione con il modello *Test report* e allega un log `-V`. Le pull request sono benvenute; mantieni verde
`make test` e non aggiungere file estratti da software di Blackmagic Design o JLCooper.

## Sostegno

Il software è gratuito (MIT). Se ti ha fatto risparmiare tempo, puoi [offrirmi un caffè](https://buymeacoffee.com/sarogreco):
è del tutto facoltativo e non dà diritto ad assistenza.

## Avviso legale

* "DaVinci Resolve", "Blackmagic Design" e "JLCooper" sono marchi dei rispettivi proprietari, usati qui solo per
  indicare con quali prodotti funziona il software.
* Questo repository non contiene software, firmware o documentazione di Blackmagic Design o JLCooper. Il demone
  è una reimplementazione indipendente, scritta a partire dall'analisi del comportamento osservabile e dei
  protocolli del programma originale, a fini di interoperabilità. Le tabelle nel sorgente (ID dei LED, mappe di
  tasti e manopole, etichette LCD) descrivono ciò che fa l'originale e sono state ricavate da esso.
* Se la modifica di file dentro l'app di Resolve sia consentita dal tuo contratto di licenza, e che cosa dica la
  legge del tuo paese sull'interoperabilità, sta a te verificarlo. Modificare l'app può influire
  sull'assistenza.
* Se rappresenti Blackmagic Design o JLCooper e hai un'obiezione su questo progetto, apri una segnalazione.

## Crediti

Manutenuto da Sebastiano Greco, che ha eseguito le prove sull'hardware reale e diretto il lavoro. L'analisi dei
protocolli, l'implementazione e la suite di test sono state sviluppate con l'aiuto di Claude (Anthropic).

## Licenza

[MIT](LICENSE). Nessuna garanzia.
