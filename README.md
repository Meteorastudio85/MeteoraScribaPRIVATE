# Scriba (nome provvisorio) - Meteora Plugins

Plugin in insert su basso / chitarra / piano / cassa / rullante. Ascolta il brano mentre suona e ne ricava:
tonalita', accordi (o note) battuta per battuta, sezioni del brano, file MIDI e - sul rullante - di quanti semitoni accordarlo.

## Scriba Pitch: la tonalità passa da sola (0.6.2)

Da questa versione Scriba comunica la tonalità e gli accordi a **Scriba Pitch** (Meteora Link,
`Source/Link/MeteoraLink.h`, lo stesso file in entrambi i plugin). Metti Scriba su chitarra,
basso o piano, fai ANALYZE: Scriba Pitch sulla voce prende quella tonalità (e gli accordi di
chitarra/piano per la sua corsia CHORDS). Se in Scriba scegli la tonalità a mano (Song key),
vale quella, anche prima di analizzare. Kick e Snare non pubblicano nulla.
Funziona anche nelle DAW che caricano i plugin in processi separati (Bitwig, AU su Logic,
REAPER "run as separate process").

## Compilare

Servono CMake 3.22+ e un compilatore C++17. JUCE 8.0.4 viene scaricato da solo.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

- Windows: Visual Studio 2022. Il VST3 va poi copiato in `C:\Program Files\Common Files\VST3`.
- macOS: Xcode. VST3 e AU copiati nelle cartelle di sistema.
- Linux: servono i pacchetti di sviluppo di JUCE (ALSA, FreeType, X11, GL).
- Esce anche la versione Standalone.

## GitHub e build automatica

Il file `.github/workflows/build.yml` compila il plugin su Windows, macOS e Linux a ogni push.
Dopo il push: scheda **Actions** del repository, apri l'ultima esecuzione e scarica gli artefatti
(`Scriba-Windows`, `Scriba-macOS`, `Scriba-Linux`): dentro trovi la cartella `VST3` con `Scriba.vst3`.

In locale, per copiare il plugin nelle cartelle di sistema dopo la build, aggiungi `-DSCRIBA_COPY_AFTER_BUILD=ON`.

## Uso

1. Metti Scriba in insert sulla traccia e scegli lo **strumento** (BASS, GUITAR, PIANO, KICK, SNARE).
2. Premi **ANALYZE** e avvia il brano. La registrazione parte con il play e si chiude con lo stop
   (l'audio passa invariato, il plugin lo ascolta soltanto).
3. A fine analisi: **SAVE TXT** salva il file di testo, **DRAG MIDI** si trascina sulla timeline (o si clicca per salvare il .mid).
4. Se cambi **strumento** o **sensibilita'** il plugin rifa l'analisi sull'audio gia' registrato, senza ripassare il brano.

Le battute del file di testo sono **quelle della DAW**: se fai partire il brano dalla battuta 17, il testo parte da "17".
I risultati (tonalita', note, colpi, accordatura) **vengono salvati nel progetto**: riaprendolo sono ancora li'.
L'audio registrato invece non si salva: per cambiare strumento o sensibilita' dopo aver riaperto il progetto, premi di nuovo ANALYZE.

## Il file di testo (Bass / Guitar / Piano)

E' pensato per essere letto da un musicista, non da un computer:

```
KEY      A minor   (likely)
TEMPO    100 BPM, 4/4

SONG MAP
  Intro .......... bar 5
  Verse 1 ........ bar 9
  Chorus 1 ....... bar 17

CHORDS, BAR BY BAR
[Verse 1]
  9-16  | Am     | F      | C      | G      |  x2
```

- Una casella = una battuta, 4 battute per riga, divise per sezione.
- **Chitarra e piano**: il nome dell'accordo (Am, F, G7, Cmaj7, Dsus4, E5...). Se l'accordo cambia dentro la battuta ci sono due nomi.
- **Basso** (o una parte a note singole, tipo un riff): le note principali di ogni battuta, nell'ordine in cui sono suonate.
- Le righe uguali una dopo l'altra non si ripetono: diventano "x2", "x3".
- Ogni singola nota con la sua durata esatta e' nel **file MIDI**, non nel testo.

## Rullante: accordatura sulla tonalita' del brano

La tonalita' **non** si ricava dal rullante (su una batteria non si puo'): Scriba la prende dalle **altre istanze di Scriba** del progetto.

1. Metti Scriba sul basso e/o sulla chitarra (o piano) e premi ANALYZE.
2. Metti un altro Scriba sul rullante, scegli **SNARE**, premi ANALYZE.
3. Scriba ti dice soltanto quanti semitoni alzare o abbassare il rullante, per esempio:

```
SONG KEY     A minor   (from Scriba on 'Bass' + 'Gtr')
SNARE NOTE   F#3 +46 cents

  TUNE THE SNARE DOWN 2 SEMITONES   (-2)
  It becomes E3, the 5th of A minor.
```

- Se ci sono piu' Scriba (basso + chitarra + piano) le loro informazioni vengono **unite**: la tonalita' e' piu' sicura.
- La nota di arrivo e' la piu' vicina tra **tonica, terza e quinta** della tonalita' (mai piu' di ~2 semitoni e mezzo).
  Se una nota della scala richiede un cambio piu' piccolo, compare anche quella come alternativa.
- Il file e la schermata si aggiornano da soli: puoi analizzare la chitarra anche **dopo** il rullante.
- Il menu **SONG KEY** (visibile in modalita' Snare) resta su *Auto*; se vuoi, li' puoi scegliere la tonalita' a mano.
- Funziona con tutte le istanze caricate nella stessa DAW (Reaper, Live, Logic, Cubase, Studio One, FL Studio, Bitwig in modalita' normale).
  Usa lo stesso formato (tutti VST3 o tutti AU) per le istanze che devono parlarsi.

## Kick e Snare: GATE in diretta = MIDI perfetto

In modalita' KICK e SNARE al posto della sensibilita' c'e' il **GATE**:

- Con **LISTEN** acceso senti la traccia attraverso il gate **in tempo reale**, mentre la DAW suona.
  La lucina accanto a GATE si accende quando il gate si apre.
- In basso vedi il livello della batteria che scorre e la **linea arancione** della soglia: puoi trascinarla
  direttamente col mouse (o usare lo slider). Sopra la linea = colpo, sotto = escluso.
- Il MIDI contiene **esattamente i colpi che apre il gate**: quello che senti e' quello che esce nel MIDI.
- Dopo l'analisi puoi ancora spostare il gate: i colpi e il MIDI si ricalcolano subito dalla stessa registrazione,
  senza ripassare il brano (le tacche in alto nel riquadro sono i colpi trovati).
- Il rilevatore del kick ascolta solo i bassi (30-120 Hz); quello dello snare ascolta da 180 Hz in su, cosi' la cassa
  non apre il gate del rullante. La velocity dei colpi dipende da quanto superano la soglia.
- LISTEN spento: senti la traccia originale (il gate lavora lo stesso per il MIDI).

Kick crea **solo il file MIDI** (nota 36, GM). Snare crea MIDI (nota 38) e file di testo.

## Snare: PITCH direttamente su Scriba

Nella schermata dello snare, sotto il righello, c'e' **SNARE PITCH**:

- **SET -2.46** (il numero cambia) porta subito il rullante alla nota consigliata, centesimi compresi.
- **-** / **+** spostano di un semitono alla volta, **0** torna all'originale.
- Senti il rullante intonato in diretta; sul righello il segno arancione mostra dove suona adesso e
  accanto a SNARE PITCH leggi la nota risultante (es. "-2.46 st -> E3").
- Il pitch e' fatto colpo per colpo: l'attacco di ogni colpo resta pulito, solo la coda viene "granulata".
  Pensato per spostamenti di qualche semitono (oltre +-5 si sentono di piu' gli artefatti).

Nota: Scriba ha una latenza fissa di 5 ms (serve al gate per non tagliare l'attacco). La DAW la compensa da sola.

## Come funziona

- **Basso**: algoritmo YIN, una nota alla volta. E' la modalita' piu' precisa.
- **Chitarra / Piano**: **Basic Pitch** (rete neurale di Spotify, licenza Apache 2.0), dentro il plugin, senza download e senza internet.
  Lo spettro (CQT) e' scritto in C++ apposta per Scriba ed e' verificato numero per numero contro il modello originale
  (differenza massima 0,0001 sulle uscite della rete). Dopo la rete, Scriba:
  - unisce le note "spezzate" (una corda che suona ancora non viene ri-attaccata quando ne pizzichi un'altra:
    resta separata solo se quella nota torna davvero piu' forte, cioe' se e' una nuova pennata);
  - riporta l'inizio di ogni nota sull'attacco reale della forma d'onda (la rete tende ad anticiparlo/ritardarlo sulle corde basse).
- **Accordatura**: Scriba misura di quanti cents lo strumento e' lontano da La = 440 Hz e compensa prima di trascrivere
  (chitarra e basso). Una chitarra calante o crescente di 30-50 cents non da' piu' note sbagliate di un semitono.
- **Tonalita'**: dalle note trascritte (senza armonici) unite allo spettro; tra maggiore e relativa minore decidono le note gravi di inizio e fine brano.
- **Accordi**: per ogni movimento si confronta l'insieme delle note con le forme di accordo; un passaggio di "levigatura" (Viterbi)
  evita che una nota di passaggio diventi un accordo nuovo (cambiare accordo costa meno sul primo movimento della battuta).
- Distorsioni e riverberi lunghi peggiorano molto il risultato: meglio il segnale pulito (prima degli effetti).

## Struttura

- `Source/KeyDetector.h` - tonalita' (spettro e cromagramma)
- `Source/NoteTranscriber.h` - trascrizione delle note
- `Source/ChordChart.h` - accordi / note per battuta, tonalita' dalle note
- `Source/SnareTuning.h` - di quanti semitoni spostare il rullante
- `Source/ChordTheory.h` - accordi della tonalita', voicing e stili di esecuzione
- `Source/ChordPreview.h` - piccolo piano interno per ascoltare gli accordi
- `Source/SharedKey.h` - tonalita', accordi e sezioni condivisi tra le istanze di Scriba
- `Source/LyricsTranscriber.*` - testo del brano con whisper.cpp
- `Source/VoiceModel.h` - posizione e download del modello vocale
- `Source/ReportBuilder.h` - file di testo
- `Source/MidiExport.h` - file MIDI
- `Source/DrumAnalyzer.h` - colpi di kick e snare, intonazione dello snare
- `Source/DrumGate.h` - gate (in diretta e sulla registrazione) e pitch shifter dello snare
- `Source/Structure.h` - sezioni del brano (Intro, Verse, Chorus, Bridge, Outro)
- `Source/PluginProcessor.*` - registrazione, analisi in un thread a parte, salvataggio nel progetto
- `Source/PluginEditor.*` - interfaccia grafica

## Note

- La registrazione e' mono a ~22 kHz, massimo 10 minuti. La memoria (~50 MB) si occupa solo dopo il primo ANALYZE.
- Interfaccia e file di testo sono in inglese. Note con i nomi inglesi (C, C#, D ...) e diesis; C4 = Do centrale (nota MIDI 60).

## VOCALS: testo del brano (con gli accordi sopra le parole)

Metti Scriba sulla traccia della voce, scegli **VOCALS**, premi ANALYZE e fai suonare il brano.

- **SAVE TXT** salva il testo diviso per sezioni, con il numero di battuta della DAW su ogni riga.
- Se c'e' un altro Scriba su **chitarra o piano** (o almeno sul basso) gia' analizzato, i suoi **accordi vengono scritti sopra
  le parole**, nel punto esatto in cui cambiano, e le parti strumentali tra le strofe mostrano i loro accordi:

```
[Chorus 1]
  17    F
        Ma tu sei qui con me

  20    Am            F
        e non finisce mai
```

- **DRAG MIDI** da' la **melodia della voce** in MIDI.
- **LANGUAGE**: Auto (sceglie tra italiano e inglese), Italiano, English.
- Allineamento: voce e chitarra possono essere analizzate in passate diverse, Scriba usa la posizione sulla timeline della DAW
  (serve che la DAW comunichi tempo e posizione, come fanno tutte le DAW principali).

### Il modello vocale (una volta sola)

La trascrizione usa **Whisper large-v3-turbo** (whisper.cpp, licenza MIT) e funziona **sul computer, senza internet**.
Il modello (circa 550 MB) non e' dentro il plugin: la prima volta, in modalita' VOCALS, premi **DOWNLOAD VOICE MODEL**.
Viene salvato una volta per tutti gli Scriba e tutti i progetti in:

- macOS: `~/Library/Application Support/Meteora Plugins/Scriba/Models`
- Windows: `%APPDATA%\Meteora Plugins\Scriba\Models`

Se il download non parte (firewall, rete aziendale) scarica a mano `ggml-large-v3-turbo-q5_0.bin` da
https://huggingface.co/ggerganov/whisper.cpp/tree/main e mettilo in quella cartella (pulsante **OPEN FOLDER**).
Funziona anche con un altro modello whisper.cpp `ggml-*.bin` messo li' (es. `ggml-medium.bin`).

### Cosa aspettarsi

- E' una prima stesura da rileggere: sul cantato Whisper sbaglia piu' che sul parlato (parole allungate, biascicate).
- Meglio la voce asciutta: Scriba **prima** di riverbero, delay, doppie voci. Cori e autotune spinto peggiorano il risultato.
- Scriba ascolta solo dove c'e' voce (gate interno) ed elimina le frasi che Whisper a volte inventa nei silenzi.
- Tempo: da qualche secondo (Mac Apple Silicon, usa la GPU) a un paio di minuti (PC senza GPU) per un brano di 3-4 minuti.
  Se cambi solo la sensibilita' il testo non viene ricalcolato (cambia solo la melodia MIDI).
- Requisiti CPU su Windows / Intel: processore con AVX2 (praticamente tutti dal 2013 in poi).
- Il testo viene salvato nel progetto: riaprendolo e' ancora li'.

## ACCORDI: i pulsanti degli accordi della tonalita' (drag & drop MIDI)

Il riquadro in basso ha due schede: **WAVE** (forma d'onda / note, o il gate per kick e snare) e **CHORDS**.
Appena Scriba trova la tonalita' si apre da solo su CHORDS e mostra gli accordi della tonalita':
I ii iii IV V vi vii (in minore: i ii III iv v VI VII, piu' il V maggiore, es. E in La minore).
La tonalita' e' quella di questa traccia; su kick, snare e voce e' quella delle altre istanze di Scriba
(o quella scelta a mano in SONG KEY).

- **Clic** su un accordo: lo senti (piccolo piano interno, solo ascolto: non finisce nella registrazione).
- **Clic tenuto e trascina** sulla traccia della DAW: rilasci il MIDI di quell'accordo. Ideale per costruire un giro di piano.
- **Pallino arancione**: l'accordo e' suonato nel brano (trovato dalla chitarra / piano / basso).
- **DRAG WHOLE SONG**: tutti gli accordi trovati nel brano, nello stile scelto, con le parti (Intro, Strofa...) come marker.
  Rilascialo alla battuta 1 del progetto: e' gia' allineato al brano. Gli accordi si collegano con il voicing piu' vicino.

Varianti di esecuzione:
- **TYPE**: triadi, settime, sus2, sus4, add9
- **PLAY**: tenuto, pennata (strum), quarti, ottavi, arpeggio su, arpeggio su-giu', basso albertino, ballad, e le parti di
  pianoforte a due mani (mano sinistra con basso, ottave e passaggi verso l'accordo successivo; mano destra con ritmo,
  rivolti, acciaccature, rolli e scale; dinamica e tempo leggermente "umani"):
  - **Piano: Pop** - ottave e quinte alla sinistra, accordi sincopati alla destra (predefinito)
  - **Piano: Ballad arpeggio** - arpeggio fluido alla sinistra con pedale, accordo arpeggiato e melodia alla destra
  - **Piano: Gospel** - ottave, acciaccature sulla terza e scala in risalita sul quarto tempo
  - **Piano: Driving pop** - ottavi incalzanti con accenti 3+3+2
  - **Piano: Cinematic** - basso tenuto e arpeggio in sedicesimi su due ottave
  - **Piano: Neo soul** - accordi ricchi (3a, 5a, 7a, 9a) arpeggiati e in levare
- **VOICING**: stretto, 1o rivolto, 2o rivolto, aperto, con il basso
- **LENGTH**: 1/2 battuta, 1, 2 o 4 battute (al tempo della DAW)

Nota: per sentire il clic sugli accordi la DAW deve far passare l'audio nella traccia (in Logic seleziona la traccia).

## Novita' della versione 0.6.1
- Sei nuovi stili di pianoforte a due mani, molto piu' articolati, con passaggi del basso verso l'accordo successivo.
- DRAG WHOLE SONG: ignora gli accordi che durano meno di un tempo (dettagli del riconoscimento).

## Novita' della versione 0.6
- Pulsanti degli accordi della tonalita' con drag & drop MIDI, ascolto e varianti di esecuzione.
- DRAG WHOLE SONG: la progressione di tutto il brano in MIDI.
- Corretto: aprire la finestra del plugin rifaceva l'analisi ogni volta.

## Novita' della versione 0.5

- GUITAR e PIANO usano Basic Pitch (rete neurale) al posto del vecchio algoritmo. Su chitarra simulata con corde pizzicate
  (Karplus-Strong), percentuale di note giuste (F1): riff 0.56 -> 0.92, arpeggi 0.41 -> 0.83, accordi 0.51 -> 0.60;
  con la chitarra scordata di 35-40 cents il vecchio scendeva a 0.24-0.38, il nuovo resta 0.70-0.92.
- Compensazione automatica dell'accordatura (chitarra e basso).

## Novita' della versione 0.4

- Nuova modalita' **VOCALS**: testo del brano con Whisper (italiano / inglese), accordi sopra le parole presi dagli altri Scriba,
  sezioni e battute della DAW, melodia della voce in MIDI.
- Bundle ID corretto (`com.meteoraplugins.scriba`): prima conteneva uno spazio, che su Mac puo' dare problemi all'AU.

## Novita' della versione 0.3.1

- Font con gli angoli piu' smussati (stesso disegno di prima, punte e angoli piu' morbidi).
  Lo script che lo genera e' in `tools/round_font_corners.py`: si puo' rifare con un raggio diverso, es.
  `python tools/round_font_corners.py vecchio.ttf nuovo.ttf 60 26` (raggio angoli esterni / interni, in unita' del font).
- Logo: linee leggermente piu' spesse (leggibile anche piccolo) e ridimensionato una volta sola ad alta qualita'
  alla dimensione esatta dello schermo (prima veniva rimpicciolito al volo e le linee si spezzavano).
  L'originale resta in `Resources/logo_source.png`.

## Novita' della versione 0.3

- Kick / Snare: GATE con ascolto in diretta (LISTEN), soglia trascinabile sul misuratore, MIDI = colpi del gate,
  ricalcolo immediato spostando il gate dopo l'analisi.
- Snare: SNARE PITCH su Scriba (SET al valore consigliato, - / + di un semitono, 0), ascolto in diretta.
- Il file di testo dello snare riporta numero di colpi, soglia del gate e il pitch impostato.

## Novita' della versione 0.2

- Pulsante principale sempre con la scritta (all'apertura era vuoto) e menu strumento che si vedeva vuoto: corretti.
- Esportazione TXT: corretto un errore di memoria che poteva salvare un file vuoto o sporco.
- File di testo riscritto: accordi / note per battuta, mappa del brano, niente piu' secondi e posizioni decimali.
- Battute allineate a quelle della DAW (prima la battuta 1 era sempre il punto in cui partiva la registrazione).
- Rullante: tonalita' presa dalle altre istanze di Scriba, risposta in semitoni, menu SONG KEY per sceglierla a mano.
- Tonalita' piu' affidabile (dalle note trascritte) e risultati salvati nel progetto.
- Grafica rifatta: selettore strumenti con icone, pulsante principale ben visibile con lo stato (ANALYZE / PRESS PLAY / STOP / ANALYZING %),
  schermata dedicata al rullante con il numero di semitoni e il righello delle note, kick dedicato.

## Diritti e licenze

- `LICENSE.txt`: il codice e il plugin sono proprieta' di Meteora Plugins, tutti i diritti riservati.
- `EULA_IT.txt` / `EULA_EN.txt`: contratto di licenza per chi acquista il plugin (da completare con i dati del titolare
  tra parentesi quadre e da far rileggere a un legale prima della pubblicazione). Va incluso nel download di Gumroad.
- `THIRD_PARTY_NOTICES.txt`: licenze dei componenti di terzi. Va incluso nel download insieme al plugin.
- Ogni file sorgente scritto per Scriba ha l'intestazione di copyright. I file di terzi mantengono le loro licenze.
- Il repository GitHub deve restare **privato**.

## Licenze dei componenti

- JUCE 8 (framework del plugin): licenza JUCE (per vendere il plugin serve la licenza JUCE adatta al proprio fatturato).
- whisper.cpp e modello Whisper: MIT.
- Basic Pitch (Spotify): Apache 2.0. Codice della rete e della creazione delle note da NeuralNote v1 (Apache 2.0).
  Dettagli e testi delle licenze in `Source/basicpitch/NOTICE.txt` e `Source/basicpitch/LICENSE-*.txt`.
- RTNeural: BSD 3-Clause.
