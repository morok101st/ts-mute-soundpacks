# Mute Soundpack

TeamSpeak-3-Plugin, das die Soundpack-Sounds (z. B. „User joined your channel“) leise schaltet, solange man sich in einem bestimmten Channel befindet. Beim Verlassen des Channels wird die vorherige Lautstärke wiederhergestellt. Sprach-Audio bleibt unverändert.

Beim Betreten eines solchen Channels wird erst nach 0,5 Sekunden abgesenkt, damit der „Channel switched“-Sound noch vollständig zu hören ist. Beim Verlassen wird sofort zurückgesetzt.

Technisch setzt das Plugin den Playback-Wert `volume_factor_wave` pro Server-Verbindung.

## Installation

1. Die aktuelle `.ts3_plugin`-Datei unter [Releases](../../releases) herunterladen.
2. Doppelklick auf die Datei, der TeamSpeak-Installer übernimmt den Rest.
3. In TeamSpeak unter *Extras → Optionen → Addons* das Plugin aktivieren.

Nur Windows 64-bit.

## Konfiguration

Am bequemsten über TeamSpeak: *Extras → Optionen → Addons → Mute Soundpack → Einstellungen*. Dort lassen sich Channels, Groß-/Kleinschreibung, Lautstärke und Chat-Hinweis einstellen. Mit OK werden die Werte gespeichert und sofort angewendet.

Die Einstellungen liegen in einer ini-Datei, die auch direkt bearbeitet werden kann. Beim ersten Start legt das Plugin `%APPDATA%\TS3Client\mute_soundpack.ini` an:

```ini
channel=ACRE
ignorecase=0
volume=-100
messages=1
```

| Schlüssel    | Bedeutung |
|--------------|-----------|
| `channel`    | Teil des Channel-Namens. Bis zu 8 `channel=`-Zeilen. |
| `ignorecase` | `1` = Groß-/Kleinschreibung egal, `0` = exakt. |
| `volume`     | Soundpack-Lautstärke in dB im Channel (`-100` = praktisch unhörbar). |
| `messages`   | `1` = Hinweis im Server-Tab beim Ein-/Ausschalten. |

Änderungen gelten beim nächsten Channel-Wechsel, ein Neustart ist nicht nötig.

## Absturzschutz

Während die Sounds abgesenkt sind, merkt sich das Plugin den ursprünglichen Wert in `%APPDATA%\TS3Client\mute_soundpack.state`. Wird TeamSpeak beendet, ohne dass das Plugin zurücksetzen konnte (Absturz, Task-Manager), stellt es beim nächsten Verbinden in einem normalen Channel den alten Wert wieder her. Beim regulären Verlassen des Channels oder Beenden wird die Datei gelöscht.

## Bauen

Voraussetzung: Visual Studio 2022 oder neuer bzw. die Build Tools mit „Desktopentwicklung mit C++“.

```bat
git clone --recurse-submodules https://github.com/morok101st/ts-mute-soundpacks.git
cd ts-mute-soundpacks
build.bat
```

Ergebnis: `build\MuteSoundpack_<version>.ts3_plugin`.

Die DLL wird ohne C-Laufzeitbibliothek gebaut (`/NODEFAULTLIB`) und importiert nur `kernel32.dll`. Die TeamSpeak-Header kommen aus dem offiziellen [ts3client-pluginsdk](https://github.com/TeamSpeak-Systems/ts3client-pluginsdk) im Submodul `sdk/`.

## Neue Version veröffentlichen

1. Versionsnummer in `src/mute_soundpack.c` anpassen (`PLUGIN_VERSION`). `build.bat` übernimmt sie automatisch in Dateinamen und `package.ini`.
2. Committen, dann taggen und pushen:
   ```bat
   git tag v1.2
   git push origin main --tags
   ```
3. GitHub Actions baut das Plugin und hängt die `.ts3_plugin`-Datei an ein neues Release.

## Projektstruktur

```
src/mute_soundpack.c        Plugin-Quellcode
package/package.ini      Vorlage für die Installer-Metadaten (@VERSION@ wird beim Build ersetzt)
sdk/                     TeamSpeak Plugin SDK (Git-Submodul)
build.bat                Build + Paketierung
.github/workflows/       CI-Build und Releases
```

## Lizenz

MIT, siehe [LICENSE](LICENSE). Das TeamSpeak-SDK im Ordner `sdk/` unterliegt den Bedingungen von TeamSpeak Systems.
