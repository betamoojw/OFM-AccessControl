<!-- 
cSpell:words knxprod EEPROM Ausgangstrigger Sonnenstandsbezogene Sonnenauf vollzumüllen Enocean Pieptönen platformio
cSpell:words softwareseitig untergangszeit Urlaubsinfo Feiertagsinfo Konverterfunktionen Vergleicher Geokoordinaten
cSpell:words Konstantenbelegung vorzubelegen Intervallvergleich Hysteresevergleich Uebersicht Logiktrigger priorität
cSpell:words Szenenkonverter Szenennummern Zahlenbasierte Intervallgrenzen Hystereseschalter Ganzzahlbasierte
cSpell:words erwartungskonform hardwareabhängig Rueckkopplung eingabebereit maliges AUSschaltverzögerung EINschaltverzögerung
cSpell:words Triggersignal expample runterladen Wiregateway updatefähige Updatefunktion Auskühlalarm Zaehler tagestrigger
cSpell:words mgeramb ambiente Ambientenbeleuchtung
-->

# Applikationsbeschreibung Zutrittskontrolle

Die Applikation Zutrittskontrolle erlaubt eine Parametrisierung einer Zutrittskontrolle per Fingerabdruck oder NFC-Tag mit der ETS.

Es gibt eine kleinere Applikationsversion mit bis zu 200 Aktionen/Fingerzuordnungen/NFC-Tags und eine größere mit bis zu 1500 Aktionen/Fingerzuordnungen/NFC-Tags. Während die größere hauptsächlich für das "großen" Lesegerät R503Pro angeboten wird, kann sie bei Bedarf an mehr als 200 Fingerzuordnungen auch für das "kleinere" Lesegerät R503 verwendet werden.

## Änderungshistorie

Im folgenden werden Änderungen an dem Dokument erfasst, damit man nicht immer das Gesamtdokument lesen muss, um Neuerungen zu erfahren.

03.07.2026: Firmware 0.15.0, Applikation 0.15

* NEU: Unterstützung vom 4x3 Keypad am [UP1-SEN-8x](http://device.openknx.de/UP1-SEN-8x) 
* FIX: Probleme mit blockierten Tasten und verzögerten Reaktionen des Gerätes beim Anschluß vom Keypad für GIRA sind jetzt gelöst

22.01.2026: Firmware 0.11, Applikation 0.11

* NEU: Erweiterung der Applikation, um Zahlencodes als Zutrittsmedium zu etablieren und Aktionen zuzuweisen.
* NEU: Unterstützung vom Keypad für GIRA
* NEU: Vorbereitung für weitere Keypads

18.09.2025: Firmware 0.10, Applikation 0.10

* FIX: Rot/Grüne LED sowie Touch-Tasten wurden beim Schaltereinsatz ohne NFC nicht erkannt.
* FIX: Partielle Parameterüberlagerung in ETS konnte zu Problemen bei aktivierten Zusatzfunktionsoptionen in der ETS führen.
* FIX: Die Fingerprint-Scanner-Passwortlänge ist nun in der ETS auf 15 Zeichen (statt wie bisher 16) beschränkt, konsistent mit der firmware-seitigen Beschränkung auf 15 Zeichen.

01.04.2025: Firmware 0.9, Applikation 0.9

* NEU: Externer NFC-Leser is in der Applikation auswählbar und wird jetzt auch von der Firmware unterstützt.

26.02.2025: Firmware 0.8, Applikation 0.8

* NEU: Die Applikation wurde in "Zutrittskontrolle" umbenannt.
* NEU: Die Abfrage von NFC-Tags wird jetzt unterstützt.
* NEU: Die Applikation nutzt jetzt das neue Modul AccessControl.
* NEU: Aktionen, die keine Autorisierung erfordern, haben jetzt ein Sperr-KO

14.10.2024: Firmware 0.6.13, Applikation 0.6

* FIX: Erhöhung des Timesouts bei der Anlernung.
* FIX: Fehlgeschlagener Anlernvorgang wurde trotzdem mit einem Erfolg im Anlern-KO quittiert.

09.10.2024: Firmware 0.6.7, Applikation 0.6

* HW-SUPPORT: Unterstützung für AB-SmartHouse Fingerprint Hardware-Revision v1.7 und neuer hinzugefügt.

20.08.2024: Firmware 0.6.6, Applikation 0.6

* FIX: Seltener Fall, bei dem der Fingerprint fälschlicherweise dachte, er sei gesperrt, ist korrigiert.
* FIX: Diverse interne Verbesserungen im Ablauf der Verarbeitung und KNX-Kommunikation.
* FIX: Einige Standard-KO-Flags wurden korrigiert (wirkt sich nur bei neuen Installationen in er ETS aus).

01.08.2024: Firmware 0.6.3, Applikation 0.6

* CHANGE: Scanner reagiert nun auf externe Farbkontrolle, auch wenn er gerade gesperrt ist.
* CHANGE: Ist der "Fortlaufend"-Modus aktiv und bleibt derselbe Finger auf dem Scanner, werden Ereignisse und Aktion nur einmal ausgeführt.
* CHANGE: Das KO "Berührung (sofort, immer)" wird nun in beiden Modi bei Berührung auf True und, nachdem der Finger entfernt wurde, auch wieder auf False gesetzt.

22.07.2024: Firmware 0.6.2, Applikation 0.6

* NEU: Eine aktive Sperre schaltet den Led-Ring nun auf rot.
* FIX: Scannen von Fingern war trotz aktiver Sperre möglich.


20.07.2024: Firmware 0.6.1, Applikation 0.6

* FIX: Fingerprint wartet nun nicht länger auf weitere Finger, wenn ein Fingerscan fehlgeschlagen ist.
* FIX: Die Schaltung des Relais' der zusätzlichen Relais- und Binäreingangsplatine war invertiert.

13.07.2024: Firmware 0.6, Applikation 0.6

* NEU: Unterstützung für kommende Relais- und Binäreingangsplatine hinzugefügt.
* NEU: Option für kontinuierlichen Scan hinzugefügt (unabhängig vom Berührungsereignis).
* NEU: Scanner-Verbindung wird nun überwacht und Änderung als KO ausgegeben.
* NEU: Nach Neustart werden für Aktionen "Umschalten" "GroupValueRead"-Telegramme gesendet.
* NEU: Konfigurationstransfer hinzugefügt.
* FIX: Eingestellte Wartezeit auf Autorisierung wird und korrekt verwendet.
* FIX: Der Fingerprint-LED-Ring leuchtet nun gelb anstatt grün, wenn eine Autorisierungsaktion nicht ausgeführt werden konnte.
* Dokumentation aktualisiert.
* UI-Verbesserungen.

10.07.2024: Firmware 0.3, Applikation 0.3

* NEU: Man kann jetzt einstellen, ob der Finger nur bei einem Touch oder fortlaufend abgefragt wird.
* NEU: Es gibt jetzt ein KO, dass signalisiert, dass der Hardware-Scanner von der Hauptplatine getrennt wurde.
* NEU: Für Aktionen vom Typ "Umschalten" wird nach einem Neustart das Status-KO vom Bus gelesen.
* NEU: Das Schaltaktor-Modul wurde hinzugefügt.
* NEU: Das Binäreingang-Modul wurde hinzugefügt.
* NEU: Das Konfigurationstransfer-Modul wurde hinzugefügt.
* NEU: Das Logikmodul wurde auf die Version 3.3 aktualisiert.

02.06.2024: Firmware 0.2.1, Applikation 0.2

* KO "Scan: Erfolg" liefert nun eine "0", wenn ein Finger nicht erkannt wurde.

01.06.2024: Firmware 0.2.0, Applikation 0.2

* Es gibt nun eine Personensuche, mit der man nach den zugewiesenen Fingern und Namen suchen kann.
* Die Synchronisation zwischen mehreren Fingerabdrucklesern ist nun vollständig implementiert. Ist diese aktiviert, erfolgt die Synchronisation automatisch, nachdem ein neuer Finger angelernt wurde. Über die ETS ist es aber auch möglich die Synchronisation einzelner Finger manuell anzustoßen. Auch das Löschen eines Fingers wird synchronisiert.
* Die Funktion und Farbe des Fingerprint LED-Rings kann nun über Rohdaten-KOs auch extern gesteuert werden.

18.05.2024: Firmware 0.1.0, Applikation 0.1

* Initiales Release als OpenKNX Fingerprint

## **Verwendete Module**

Die Zutrittskontrolle verwendet weitere OpenKNX-Module, die alle ihre eigene Dokumentation besitzen. Im folgenden werden die Module und die Verweise auf deren Dokumentation aufgelistet.

### **OpenKNX**

Dies ist eine Seite mit allgemeinen Parametern, die unter [Applikationsbeschreibung-Common](https://github.com/OpenKNX/OGM-Common/blob/v1/doc/Applikationsbeschreibung-Common.md) beschrieben sind. 

### **Konfigurationstransfer**

Der Konfigurationstransfer erlaubt einen

* Export von Konfigurationen von OpenKNX-Modulen und deren Kanälen
* Import von Konfigurationen von OpenKNX-Modulen und deren Kanälen
* Kopieren der Konfiguration von einem OpenKNX-Modulkanal auf einen anderen
* Zurücksetzen der Konfiguration eines OpenKNX-Modulkanals auf Standardwerte

Die Funktionen vom Konfigurationstranfer-Modul sind unter [Applikationsbeschreibung-ConfigTransfer](https://github.com/OpenKNX/OFM-ConfigTransfer/blob/v1/doc/Applikationsbeschreibung-ConfigTransfer.md) beschrieben.

### **Schaltaktor**

Die Zutrittskontrolle unterstützt auch einen Schaltaktorkanal, um direkt z.B. eine Tür öffnen zu können. 

Die Funktion des Schaltaktors ist unter [Applikationsbeschreibung-Schaltaktor](https://github.com/OpenKNX/OFM-SwitchActuator/blob/main/doc/Applikationsbeschreibung-Schaltaktor.md) beschrieben

### **Logiken**

Wie die meisten OpenKNX-Applikationen enthält auch diese Applikation ein Logikmodul.

Die Funktionen des Logikmoduls sind unter [Applikationsbeschreibung-Logik](https://github.com/OpenKNX/OFM-LogicModule/blob/v1/doc/Applikationsbeschreibung-Logik.md) beschrieben.

### **Virtuelle Taster**

Es werden auch virtuelle Taster von der Applikation angeboten. Mit der Nutzung der Binäreingänge oder der Touch-Platine (als Erweiterung) - können es auch echte Taster werden.

Die Funktionen des Tastermoduls sind unter [Applikationsbeschreibung-Taster](https://github.com/OpenKNX/OFM-VirtualButton/blob/v1/doc/Applikationsbeschreibung-Taster.md) beschrieben.

### **Binäreingänge**

Diese Applikation unterstützt auch Binäreingänge.

Die Funktionen der Binäreingänge sind unter [Applikationsbeschreibung-Binäreingang](https://github.com/OpenKNX/OFM-BinaryInput/blob/v1/doc/Applikationsbeschreibung-Binaereingang.md) beschrieben.



# **Zutrittskontrolle**

<!-- DOC HelpContext="Dokumentation" -->
Mit diesem Modul können Finger, NFC-Tags und Tastenkombinationen im Lesegerät angelernt, gelöscht, Aktionen verknüpft und Finger, NFC-Tags oder Tastenkombinationen den Aktionen zugeordnet werden.

<!-- DOCCONTENT 
https://github.com/OpenKNX/OFM-AccessControl/blob/main/doc/Applikationsbeschreibung-Zutrittskontrolle.md
DOCCONTENT -->

## **Allgemein**

In der Titelzeile wird der Modulname und dessen Version ausgegeben. Diese Information ist für Support-Anfragen im OpenKNX-Forum relevant.

### **Hardware**

In diesem Bereich erfolgen Einstellungen, die die Hardware-Scanner für Zutrittskontrolle betreffen.

Detaileinstellungen erfolgen dann auf passenden Unterseiten, benannt nach der jeweiligen Hardware.

<!-- DOC -->
#### **Fingerprint Scanner**

Auswahl der angeschlossenen Fingerprint-Scanner-Hardware. Es wird die folgendes angeboten:

* **Kein Fingerprint**: Wenn keine Fingerprint-Hardware angeschlossen ist
* **R503** (Standard): Fingerprint-Sensor mit Speicherplatz für 200 Finger
* **R503S**: Fingerprint-Sensor mit Speicherplatz für 150 Finger
* **R503Pro**: Fingerprint-Sensor mit Speicherplatz für 1500 Finger

<!-- DOC -->
#### **NFC Scanner**

Auswahl der angeschlossenen NFC-Scanner-Hardware. Es wird die folgendes angeboten:

* **Kein NFC** (Standard): Wenn keine NFC-Scanner-Hardware angeschlossen ist
* **Schaltereinsatz mit NFC**: Die Touch-Frontplatine ist mit der NFC-Scanner-Hardware ausgerüstet, zu erkennen an der NFC-Antenne
* **Externer NFC-Leser**: Eine NFC-Scanner-Hardware ist über ein Kabel mit der Fingerprint-Platine verbunden
* **Wiegand NFC-Leser**: Ein RFID/NFC-Leser ist über die Wiegand-Schnittstelle angeschlossen (nur mit der Hardware "RP2040 SmartMF-Wiegand-Reader")

<!-- DOC -->
#### **Keypad**

Auswahl der angeschlossenen Keypad-Hardware. Es wird die folgendes angeboten:

* **Kein Keypad** (Standard): Wenn keine Keypad-Hardware angeschlossen ist
* **Keypad (Touch) von Gira**: Eine Keypad-Hardware von AB-Tools ist über ein Kabel mit der Fingerprint-Platine verbunden. Kann mit der Keypad-Blende von Gira verwendet werden.
* **Keypad (Tastatur 4x3)**: Vorbereitung für weitere Code-Tastaturen
* **Keypad (Wiegand)**: Eine Tastatur ist über die Wiegand-Schnittstelle angeschlossen, Tasten 0-9, \* und # (nur mit der Hardware "RP2040 SmartMF-Wiegand-Reader")

<!-- DOC -->
#### **Touch-Frontplatine vorhanden**

<!-- DOC Skip="1" -->
Erscheint nur, wenn bei "NFC Scanner" der Wert "Kein NFC" gewählt wurde.

Mit der Touch-Frontplatine werden 2 Touch-Sensortasten in der Applikation verfügbar gemacht.

### Zusatzfunktionen

<!-- DOC -->
#### **Rohdaten auf den Bus senden**

Die Hardware-Scanner können ihre Daten direkt auf den Bus senden, ohne jegliche Aktionszuordnungen "dazwischen".

Bei Aktivierung werden entsprechende Kommunikationsobjekte freigeschaltet.

<!-- DOC -->
#### **Zutrittsdaten-KOs aktivieren**

<!-- DOC Skip="1" -->
Erscheint nur, wenn bei "Rohdaten auf den Bus senden" ein "Ja" gewählt wurde.

Werden die speziellen Kommunikationsobjekte für Zutrittsdaten benötigt (DPT 15), können diese hier aktiviert werden.

<!-- DOC -->
#### **Synchronisation mehrerer Geräte**

Sind mehrere OpenKNX-Zutrittskontroll-Geräte vorhanden und sollen die Fingerprint- und NFC-Daten unter diesen Geräten synchronisiert werden, muss diese Option aktiviert werden.

Es stehen daraufhin zusätzliche Kommunikationsobjekte zur Synchronisation zur Verfügung.

<!-- DOC -->
#### **Verzögerung zwischen Sync-Telegrammen**

<!-- DOC Skip="1" -->
Erscheint nur, wenn bei "Synchronisation mehrerer Geräte" ein "Ja" gewählt wurde.

Um eine zu hohe Busbelastung zu vermeiden, wird hier die Verzögerung zwischen Sync-Telegrammen in Millisekunden festgelegt.

<!-- DOC -->
#### **Externe Kontrolle ermöglichen**

Bei Aktivierung werden entsprechende Kommunikationsobjekte freigeschaltet, die dazu verwendet werden können den Scanner extern zu steuern (um z.B. einen Anlernvorgang extern anzustoßen).

## **Fingerprint-Scanner**

Erscheint nur, wenn bei Fingerprint-Scanner eine entscprechende Hardware ausgewählt wurde.
Erscheint als Unterseite der Seite "Allgemein".

### **Hardware-Einstellungen**

Hier werden Detaileinstellungen zur Fingerprint-Scanner-Hardware vorgenommen.

<!-- DOC -->
#### **Fingerabfrage**

Normalerweise wird die Fingerabfrage "Bei Berührung" des Fingerprints gestartet. Es gib aber einige Fälle, in den der Touch nicht zuverlässig erkannt wird. In solchen Fällen kann mit der Einstellung "Fortlaufend" die Fingerabfrage unabhängig von einer Berührung erfolgen.

Die Einstellung "Fortlaufend" kann zur Folge haben, dass Logiken, die auf dem Fingerprint definiert sind, nicht mehr zuverlässig bzw. stark verzögert laufen. Sie sollte nur gewählt werden, wenn die normale Erkennung "Bei Berührung" nicht funktioniert.


### **Finger bearbeiten**

Dieser Bereich stellt einen Finger-Editor dar, mit dem man in der ETS einzelne Finger anlernen, ändern oder löschen kann.

Dieser Bereich kann erst funktionieren, wenn das Gerät das erste Mal programmiert worden ist, also eine PA und eine Applikation hat. Solange die entsprechenden Buttons ausgegraut sind, sind diese Voraussetzungen nicht erfüllt.

<!-- DOC -->
#### **Finger**

Dieses Auswahlfeld wählt die gewünschte Editierfunktion aus:

* **anlernen**: Zu einer neuen Finger-ID soll ein Finger einer Person angelernt werden
* **ändern**: Zu einer bestimmten Finger-ID soll der Finger oder die Person geändert werden
* **löschen**: Eine bestimmte Finger-ID und die damit verbundenen Finger- und Personendaten werden gelöscht.

### **Finger anlernen**

Zum Anlernen muss eine neue Finger-ID gewählt werden, dann der Name und der Finger der Person eingegeben werden und anschließend der Button "Anlernen" gedrückt werden.

#### **Name der Person**

Der Name der Person, welcher zusammen mit den neu angelernten Fingerdaten gespeichert werden soll.

#### **Finger der Person**

Der Finger der Person, welcher zusammen mit den neu angelernten Fingerdaten gespeichert werden soll.

#### **Scanner Finger ID**

Die ID des Fingers (= der Speicherplatz), auf welche die neu angelernten Fingerdaten gespeichert werden soll.

Dabei sind die verfügbaren SPeicherplätze abhängig von der ausgewählten Hardware des Fingerprint-Scanners:
Sie werden dabei von 0 beginnend durchnummeriert. Hat der Scanner also beispielsweise 200 Speicherplätze, stehen die IDs 0-199 zur Verfügung.

### **Finger ändern**

Zum Ändern muss eine existierende Finger-ID gewählt werden, dann der neue Name und der neue Finger der Person eingegeben werden und anschließend der Button "Ändern" gedrückt werden.

#### **Name der Person**

Der Name der Person, welche neu den existierenden Fingerdaten zugeordnet werden soll.

#### **Finger der Person**

Der Finger der Person, welcher neu den existierenden Fingerdaten zugeordnet werden soll.

#### **Scanner Finger ID**

Die existierende ID des Fingers (= der Speicherplatz), dem die neue Persond und/oder der neue Finger zugeordnet werden soll.

### **Finger löschen**

Zum Löschen muss eine existierende (vorher angelernte) Finger-ID eingegeben werden. Die Person, der Finger und die angelernten Fingerdaten werden gelöscht, sobald der Button "Löschen" gedrückt wurde.

#### **Scanner Finger ID**

Die ID des Fingers (= der Speicherplatz), die gelöscht werden soll.

<!-- DOC HelpContext="Name der Person" -->
<!-- DOCCONTENT
Der Name der Person, die einer bestimmten Finger-ID zugeordnet ist.
DOCCONTENT -->

<!-- DOC HelpContext="Finger der Person" -->
<!-- DOCCONTENT
Der Finger der Person, der einer bestimmten Finger-ID zugeordnet ist.
DOCCONTENT -->

<!-- DOC HelpContext="Scanner Finger ID" -->
<!-- DOCCONTENT
Die ID des Fingers (= der Speicherplatz), auf welche die Fingerdaten gespeichert sind.

Dabei sind die verfügbaren Speicherplätze abhängig von der ausgewählten Hardware des Fingerprint-Scanners. 
Sie werden dabei von 0 beginnend durchnummeriert. Hat der Scanner also beispielsweise 200 Speicherplätze, stehen die IDs 0-199 zur Verfügung.
DOCCONTENT -->

### **Finger synchronisieren**

Nach jedem Anlernvorgang wird der neue Finger mit den anderen Geräten automatisch synchronisiert. Da dieser Vorgang komplett asynchron passiert, gibt es keine Garantie und keine Rückmeldung, ob die Synchronisation funktioniert hat. 

Falls auf irgendeinem Gerät eine Finger ID fehlt oder nicht aktuell ist, kann man hier diese Finger ID erneut synchronisieren lassen.

<!-- DOC -->
#### **Finger ID synchronisieren**

Durch die Eingabe einer Finger ID und das Drücken des Buttons "Synchronisieren" kann hier die Synchronisation eines bestimmten Fingers manuell angestoßen werden.

### **Gefährliche Funktionen**

Die Funktionen in diesem Bereich können zum Datenverlust führen, der nicht behebbar ist. Bitte mit Vorsicht nutzen.

<!-- DOC -->
#### **Passwort**

Wenn Sie hier ein Passwort vergeben und dieses vergessen, wird das Fingerprint-Lesegerät unbrauchbar. Das Passwort kann nicht wiederhergestellt werden!

Sie haben die Möglichkeit ein Passwort erstmalig festzulegen oder ein bereits vorhandenes zu ändern. Im letzteren Fall muss auch das alte Passwort eingegeben werden.

<!-- DOC -->
##### **Altes Passwort**

Das bereits vorhandene Passwort, welches geändert werden soll.

<!-- DOC -->
##### **Neues Passwort**

Das neue, zu setzende Passwort.

<!-- DOC -->
#### **Alle Finger löschen?**

Mit dieser Funktion werden sämtliche gespeicherten Fingerdaten inklusive Personenzuordnung unwiderruflich gelöscht.





## **NFC-Scanner**

Erscheint nur, wenn bei NFC-Scanner eine entscprechende Hardware ausgewählt wurde.
Erscheint als Unterseite der Seite "Allgemein".


### **NFC-Tag bearbeiten**

Dieser Bereich stellt einen NFC-Tag-Editor dar, mit dem man in der ETS einzelne NFC-Tags anlernen, anlegen, ändern oder löschen kann.

Dieser Bereich kann erst funktionieren, wenn das Gerät das erste Mal programmiert worden ist, also eine PA und eine Applikation hat. Solange die entsprechenden Buttons ausgegraut sind, sind diese Voraussetzungen nicht erfüllt.

<!-- DOC -->
#### **NFC-Tag**

Dieses Auswahlfeld wählt die gewünschte Editierfunktion aus:

* **anlernen**: Zu einer neuen Tag ID soll ein neuer NFC-Tag angelernt werden
* **anlegen**: Zu einer neuen Tag ID soll ein neuer NFC-Tag angelegt werden, dessen Tag Schlüssel (UID) man kennt.
* **ändern**: Zu einer bestimmten Tag ID soll der NFC-Tag geändert werden
* **löschen**: Eine bestimmte Tag ID und die damit verbundenen Tagdaten werden gelöscht.

### **NFC-Tag anlernen**

Zum Anlernen muss eine neue Tag ID gewählt werden, dann der Name des Tags eingegeben werden und anschließend der Button "Anlernen" gedrückt werden.

#### **Tag Name**

Der Name des NFC-Tags, der angelernt werden soll.

#### **Tag ID**

Die ID des NFC-Tags (= der Speicherplatz), auf welchen die neuen Daten gespeichert werden sollen.

#### **Tag Schlüssel (UID)**

In diesem Feld erscheint der Tag Schlüssel (auch bekannt als UID) nach einem erfolgreichen Anlernvorgang.

### **NFC-Tag anlegen**

Zum Anlegen muss eine neue Tag ID gewählt werden, dann der Name des Tags eingegeben werden und der Tag Schlüssel (UID). Anschließend der Button "Anlegen" gedrückt werden.

#### **Tag Name**

Der Name des NFC-Tags, der angelegt werden soll.

#### **Tag ID**

Die ID des NFC-Tags (= der Speicherplatz), auf welchen die neuen Daten gespeichert werden sollen.

#### **Tag Schlüssel (UID)**

Der Tag Schlüssel (auch bekannt als UID) des neuen NFC-Tags.


### **NFC-Tag ändern**

Zum Ändern muss eine existierende Tag ID gewählt werden, dann der neue Name des NFC-Tags und/oder der Tag Schlüssel eingegeben werden und anschließend der Button "Ändern" gedrückt werden.

#### **Tag Name**

Der Name des NFC-Tags, der geändert werden soll.

#### **Tag ID**

Die ID des NFC-Tags (= der Speicherplatz), auf welchem die geänderten Daten gespeichert werden sollen.

#### **Tag Schlüssel (UID)**

Der Tag Schlüssel (auch bekannt als UID) des NFC-Tags.


### **NFC-Tag löschen**

Zum löschen muss eine existierende (vorher angelernte) Tag ID eingegeben werden. Der NFC-Tag und der Tag Schlüssel werden gelöscht, sobald der Button "löschen" gedrückt wurde.

<!-- DOC HelpContext="Tag Name" -->
<!-- DOCCONTENT
Der Name des NFC-Tags, der einer bestimmten Tag ID zugeordnet ist.
DOCCONTENT -->

<!-- DOC HelpContext="Tag ID" -->
<!-- DOCCONTENT
Die ID des Tags (= der Speicherplatz), auf welchem die NFC Tag Daten gespeichert sind.
DOCCONTENT -->

<!-- DOC HelpContext="Tag Schlüssel (UID)" -->
<!-- DOCCONTENT
Der Schlüssel des NFC-Tags, auch bekannt als UID, der einer bestimmten Tag ID zugeordnet ist.
DOCCONTENT -->


### **NFC-Tag synchronisieren**

Nach jedem Anlernvorgang wird der neue NFC-Tag mit den anderen Geräten automatisch synchronisiert. Da dieser Vorgang komplett asynchron passiert, gibt es keine Garantie und keine Rückmeldung, ob die Synchronisation funktioniert hat. 

Falls auf irgendeinem Gerät eine Tag ID fehlt oder nicht aktuell ist, kann man hier diese Tag ID erneut synchronisieren lassen.

<!-- DOC -->
#### **Tag ID synchronisieren**

Durch die Eingabe einer Tag ID und das Drücken des Buttons "Synchronisieren" kann hier die Synchronisation eines bestimmten NFC-Tags manuell angestoßen werden.

### **Gefährliche Funktionen**

Die Funktionen in diesem Bereich können zum Datenverlust führen, der nicht behebbar ist. Bitte mit Vorsicht nutzen.

<!-- DOC -->
#### **Alle NFC-Tags löschen?**

Mit dieser Funktion werden sämtliche gespeicherten NFC-Tags inklusive aller Tag Namen und Tag Schlüssel unwiderruflich gelöscht.




## **Keypad**

Erscheint nur, wenn bei Keypad eine entscprechende Hardware ausgewählt wurde.
Erscheint als Unterseite der Seite "Allgemein".

Ein Keypad dient zur Eingabe eines Tastencodes und kann auch als Autorisierungsmechanismus für die AccessControl Hardware verwendet werden. 

Ein Tastencode ist eine Folge von über das Keypad eingebbaren Zeichen. Die Applikation untersützt verschiedene Arten von Tastencode-Eingaben:

* **feste Länge** - es muss immer eine besimmte Anzahl von Zeichen eingegeben werden, danach ist klar, ob der Code gültig ist oder nicht.
* **terminiert** - es wird eine variable Anzahl von Zeichen eingegeben, gefolgt von einem festgelegten Ende-Zeichen. Danach ist klar, ob der Code gültig ist oder nicht.
* **variable Länge** - es wird eine variable Anzahl von Zeichen eingegeben. Sobald ein korrekter Code erkannt wird, erfolgt eine Aktion. In allen anderen Fällen ist der Code nicht gültig.

Für die terminierte Eingabe kann man in der Applikation ein Zeichen festlegen. Für den Fall, dass kein Ende-Zeichen festgelegt wurde, entscheidet die Applikation anhand der Länge der vorhandenen Codes, ob mit fester oder variabler Länge gearbeiet wird.

Man könnte auf die Idee kommen, einen Tastencode direkt mit einer Aktion zu verbinden, um so diese Aktion auszuführen. Ein solches Verfahren hätte den Nachteil, dass man sich nur auf die möglichen Zeichen (bzw. Tasten) eines bestimmten Keypad beschränken müsste. Außerden würden Änderungen eines Codes (weil diese z.B. durch abgucken zu vielen Menschen bekannt ist) dazu führen, dass dieser Code in allen zugehörigen Aktionen geändert werden müsste. Dies würde dann verhindern, dass Änderungen von Tastencodes synchronisiert werden können, da Aktionen nicht zwischen verschiedenen Geräten synchronisiert werden.

Deswegen wird beim Keyped das gleiche Verfahren angewendet wie schon beim Fingerprint und bei NFC-Scanner: Es gibt eine Art Anlernprozess - nur ist dieser einfach direkt in der ETS vornehmbar: Man weist einfach einem Tastencode eine ID zu, diese werden mit den Tastencodes zusammen zwischen verschiedenen Geräten synchronisiert und können dann in Aktionen verwendet werden.

### **Hardware-Einstellungen**

Hier erfolgend die für das ausgewählte Keypad spezifischen Einstellungen.

<!-- DOC -->
#### **Hintergrundbeleuchtung**

<!-- DOC Skip="1" -->
Erscheint nur bei einem Touchpad mit Hintergurndbeleuchtung.

Mit dieser Einstellung wird bestimmt, wie die Hintergrundbeleuchtung für das Keypad eingeschaltet wird.

* **immer eingeschaltet** - Bei dieser Einstellung ist die Beleuchtung immer eingeschaltet.
* **einschalten über irgendeine Taste** - Die Beleuchtung wird beim Drücken einer Taste für 15 Sekunden eingeschaltet.
* **einschalten über externes KO** - Die Beleuchtung wird über ein KO durch ein KNX-Telegramm (z.B. von einem PM) ein- und ausgeschaltet.
* **immer ausgeschaltet** - Die Beleuchtung bleibt immer aus.

<!-- DOC -->
#### **Helligkeit** 

<!-- DOC Skip="1" -->
Erscheint nur, wenn bei Hintergrundbeleuchtung nicht "immer ausgeschaltet" gewählt wurde.

Hier kann man die Helligkeit der Hintergrundbeleuchtung bestimmen.

* **hohe Helligkeit** - die Beleuchtung ist so hell wie möglich
* **mittlere Helligkeit** - die Tasten werden gut erkennbar beleuchtet
* **niedrige Helligkeit** - die Tasten werden nur leicht beleuchtet
* **über externes KO** - die Helligkeit kann über ein KO bestimmt werden, mit Werten von 0%-100%

<!-- DOC -->
#### **Ausschalten nach**

<!-- DOC Skip="1" -->
Erscheint nur, wenn bei Hintergrundbeleuchtung "einschalten über irgendeine Taste" oder "einschalten über externes KO" gewählt wurde.

Hier kann man angeben, nach welcher Zeit die Hintergrundbeleuchtung wieder ausgeschaltet wird. Bei der Eingabe einer 0 wird nicht automatisch ausgeschaltet. Dies sollte man nur verwenden, wenn bei Hintergrundbeleuchtung die Einstellung "inschalten über externes KO" gewählt wurde, da nur dann auch über den Bus ausgeschaltet werden kann.

<!-- DOC -->
#### **Ersten Tastendruck ignorieren**

<!-- DOC Skip="1" -->
Erscheint nur, wenn bei Hintergrundbeleuchtung "einschalten über irgendeine Taste" oder "einschalten über externes KO" gewählt wurde.

Ist diese Einstellung aktiv, wird der erste Tastendruck nicht als Eingabe für den Tastencode gewertet. Das kann z.B. sinnvoll dazu verwendet werden, dass bei abgeschalteter Hintergrundbeleuchtung irgendein Tastendruck dazu genutzt wird, die Beleuchtung einzuschalten und erst dann die Codeeingabe beginnt.

Schaltet man die Hintergrundbeleuchtung über einen PM ein, kann man auch die erste Taste zur Codeeingabe nutzen und sollte dann hier ein "Nein" wählen.

<!-- DOC -->
#### **Tastendruck auf den Bus senden**

Beim drücken einer Taste wird ein EIN-Telegramm auf den Bus gesendet. Damit kann man z.B. mit Hilfe einer Logik realisieren, dass die Hintergrundbeleuchtung in Abhängigkeit von mehreren Bedingungen eingeschaltet wird (Tastendruck und PM).

<!-- DOC -->
#### **Max. Pause zwischen zwei Eingaben**

Hier wird festegelegt, wie viel Zeit zwischen 2 Eingaben vergehen darf, bevor das Keypad die Eingabesequenz abbricht und die bisherige Eingabe verwirft. Der Code gilt dann als nicht erkannt. Die Rückmeldung wird dann entsprechend signalisiert, siehe Rückmelde LED oder Rückmelde Buzzer.

<!-- DOC HelpContext="Ansteuerung der Rückmelde-LED"-->
#### **Rückmelde LED**

Erscheint nur bei Keypads mit Rückmelde-LED.

Die Rückmelde-LED zeigt - ähnlich wie der Fingerprint - verschiedene Zustände während der Codeeingabe an.

Die Codeerkennung beginnt mit dem ersten Tastendruck, außer man hat eingestellt, dass die erste Taste ignoriert werden soll.

Bei jedem erkannten Tastendruck blinkt die LED kurz blau.
Wurde ein korrekter Code erkannt und eine entsprechende Aktion ausgelöst, leuchtet die LED kurz grün.
Wurde ein korrekter Code erkannt und es ist keine Aktion zugeordnet, leuchtet die LED kurz gelb.
Wurde ein Code nicht erkannt, leuchtet die LED kurz rot.
Ein Code ist dann nicht erkannt, wenn

* die maximale Anzahl von 10 Zeichen eingegeben wurde und diese keinem Code entsprechen
* ein Abschlußzeichen festgelegt und eingegeben wurde und der Code falsch ist
* für 15 Sekunden kein Zeichen eingegeben wurde

Zusätzlich zu den internen Zuständen kann die Rückmelde-LED auch über ein KO vom KNX-Bus gesteuert werden, um z.B. die erfolgreiche Ausführung einer Aktion anzuzeigen.

##### **Ansteuerung der Rückmelde-LED**

Hier kann man einstellen, wie die Rückmelde-LED angesteuert wird.

* **immer aus** - die Rückmelde-LED wird nicht angesteuert (nicht empfohlen)
* **nur intern** - die Rückmelde-LED stellt nur die beschriebenen internen Zustände dar
* **über KO** - die Rückmelde-LED stellt das dar, was über Bustelegramme an ihr KO übermittelt wird
* **intern und KO** - Es werden interne Zustände bei der Tasteneingabe und Farben über KO dargestellt

<!-- DOC HelpContext="Ansteuerung des Rückmelde-Buzzers" -->
#### Rückmelde Buzzer**

Erscheint nur bei Keypads mit Rückmelde-Buzzer.

Der Rückmelde-Buzzer erlaubt die Rückmeldung verschiedener Zustände während der Codeeingabe mit Hilfe von Tönen.

Die Codeerkennung beginnt mit dem ersten Tastendruck, außer man hat eingestellt, dass die erste Taste ignoriert werden soll.

Bei jedem erkannten Tastendruck wird ein kurzer Piepton generiert.
Wurde ein korrekter Code erkannt und eine entsprechende Aktion ausgelöst, werden 2 kuze Pieptöne abgespielt.
Wurde ein korrekter Code erkannt und es ist keine Aktion zugeordnet, werden 2 kurze, gefolgt von einem langen Piepton abgespielt.
Wurde ein Code nicht erkannt, wird ein langer Piepton abgespielt.
Ein Code ist dann nicht erkannt, wenn

* die maximale Anzahl von 10 Zeichen eingegeben wurde und diese keinem Code entsprechen
* ein Abschlußzeichen festgelegt und eingegeben wurde und der Code falsch ist
* für die festgelegte Zeit kein Zeichen eingegeben wurde

Zusätzlich zu den internen Zuständen kann der Buzzer auch über ein KO vom KNX-Bus gesteuert werden, um z.B. die erfolgreiche Ausführung einer Aktion anzuzeigen.

##### **Ansteuerung des Rückmelde-Buzzers**

Hier kann man einstellen, wie der Rückmelde-Buzzer angesteuert wird.

* **immer aus** - der Rückmelde-Buzzer wird nicht angesteuert (nicht empfohlen)
* **nur intern** - der Rückmelde-Buzzer stellt nur die beschriebenen internen Zustände dar
* **über KO** - der Rückmelde-Buzzer stellt das dar, was über Bustelegramme an sein KO übermittelt wird
* **intern und KO** - Es werden interne Zustände bei der Tasteneingabe und Signale über KO abgespielt


### **Tastenfunktionen**

Hier kann man bestimmen, ob vorhandene Sondertasten eine Reaktion auf dem Bus auslösen oder als Teil der Codeeingabe gewertet werden.

Alle Tasten, die als Sondertasten definiert werden, stehen für die Codeeingabe nicht mehr zur Verfügung.

<!-- DOC -->
#### **Explizites Abschlußzeichen**

Jede Taste, außer den Ziffern 0-9, kann als Abschlußzeichen definiert werden. 

Wird ein Abschlußzeichen definiert, wird erst mit der Eingabe des Abschlußzeichens der eingegebene Code überprüft und die Eingabe entsprechend als gültig oder ungültig gewertet.

Wird kein Abschlußzeichen definiert, wird nach jedem Zeichen geprüft, ob ein gültiger Code vorliegt. Falls ja, gibt es sofort eine Reaktion, falls nein, wird auf das nächste Zeichen gewartet. Erst wenn eine festgelegte Zeit kein Zeichen eingegeben wurde, wird die Eingabe als ungültig gewertet. 


<!-- DOC -->
#### **Taste zum löschen verwenden**

Die angegebene Taste (falls vorhanden, die Taste C, sonst die Taste *) kann als Löschtaste definiert werden. Wird sie gedrückt, wird die bisherige Eingabe gelöscht und muss von neuem begonnen werden.

<!-- DOC -->
#### **Taste F als KO verfügbar machen**

<!-- DOC Skip="1" -->
Erscheint nur beim Keypad von Gira.

Wird diese Einstellung gewählt, erscheint ein zusätzliches KO, dass den Tastendruck dieser Taste wiederspiegelt. Dieser Tastendruck kann dann z.B. im virtuellen Taster ausgewertet werden.

Wird die Einstellung nicht gewählt, steht die Taste F als Buchstabe F zur Codeeingabe zur Verfügung.

<!-- DOC -->
#### **Taste 'Klingel' als KO verfügbar machen**

<!-- DOC Skip="1" -->
Erscheint nur beim Keypad von Gira.

Wird diese Einstellung gewählt, erscheint ein zusätzliches KO, dass den Tastendruck dieser Taste wiederspiegelt. Dieser Tastendruck kann dann z.B. im virtuellen Taster ausgewertet werden oder direkt an eine Klingel weitergeleitet werden.

Wird die Einstellung nicht gewählt, steht die Taste 'Klingel' als Buchstabe B (Bell) zur Codeeingabe zur Verfügung.

<!-- DOC -->
#### **Taste 'Schlüssel' als KO verfügbar machen**

<!-- DOC Skip="1" -->
Erscheint nur beim Keypad von Gira.

Wird diese Einstellung gewählt, erscheint ein zusätzliches KO, dass den Tastendruck dieser Taste wiederspiegelt. Dieser Tastendruck kann dann z.B. im virtuellen Taster ausgewertet werden oder direkt ein Gartentor öffnen.

Wird die Einstellung nicht gewählt, steht die Taste 'Schlüssel' als Buchstabe K (Key) zur Codeeingabe zur Verfügung.


### **Tastencodes bearbeiten**

Dieser Bereich stellt einen Tastencode-Editor dar, mit dem man in der ETS einzelne Tastencodes anlegen, ändern oder löschen kann.

<!-- DOC HelpContext="Tastencode-Edit" -->
#### **Tastencode**

Dieses Auswahlfeld wählt die gewünschte Editierfunktion aus:

* **anlegen**: Zu einer neuen Tastendoce ID soll ein neuer Tastencode angelegt werden.
* **ändern**: Zu einer bestimmten Tastencode ID soll der Tastencode geändert werden
* **löschen**: Eine bestimmte Tastencode ID und der damit verbundene Tastencode werden gelöscht.

### **Tastencode anlegen**

Zum Anlegen muss eine neue Tastencode ID gewählt werden, dann der Name des Tastencodes eingegeben werden und der Tastencode selbst. Anschließend der Button "Anlegen" gedrückt werden.

#### **Tastencode Name**

Der Name des Testencodes, der angelegt werden soll. Der Name kann z.B. die Person sein, die diesen Code kennt oder die Rolle, die dieser Code übernehmen soll (Garage öffnen). 

#### **Tastencode ID**

Die ID des Tastencodes (= der Speicherplatz), auf welchen der Tastencode gespeichert werden soll.

#### **Tastencode**

Der einzugebende Tastencode. Es dürfen nur Zeichen verwendet werden, die das entsprechende Keypad auch anbietet.


### **Tastencode ändern**

Zum Ändern muss eine existierende Tastencode ID gewählt werden, dann der neue Name des Tastencodes und/oder der neue Tastencode eingegeben werden und anschließend der Button "Ändern" gedrückt werden.

#### **Tastencode Name**

Der Name des Testencodes, der geändert werden soll. Der Name kann z.B. die Person sein, die diesen Code kennt oder die Rolle, die dieser Code übernehmen soll (Garage öffnen). 

#### **Tastencode ID**

Die ID des Tastencodes (= der Speicherplatz), auf welchen der Tastencode gespeichert werden soll.

#### **Tastencode**

Der einzugebende Tastencode. Es dürfen nur Zeichen verwendet werden, die das entsprechende Keypad auch anbietet.


### **Tastencode löschen**

Zum löschen muss eine existierende (vorher angelegte) Tastencode ID eingegeben werden. Der Tatencode und dessen ID werden gelöscht, sobald der Button "löschen" gedrückt wurde.

<!-- DOC HelpContext="Tastencode Name" -->
<!-- DOCCONTENT
Der Name des Testencodes, der einer bestimmten ID zugeordnet ist. Der Name kann z.B. die Person sein, die diesen Code kennt oder die Rolle, die dieser Code übernehmen soll (Garage öffnen). 
DOCCONTENT -->

<!-- DOC HelpContext="Tastencode ID" -->
<!-- DOCCONTENT
Die ID des Tastencodes (= der Speicherplatz), auf welchen der Tastencode gespeichert ist.
DOCCONTENT -->

<!-- DOC HelpContext="Tastencode" -->
<!-- DOCCONTENT
Der eigentliche Tastencode. Es dürfen nur Zeichen verwendet werden, die das entsprechende Keypad auch anbietet.
DOCCONTENT -->


### **Tastencode synchronisieren**

Nach jedem Anlegen/Ändern/Löschen wird der neue Tastencode mit den anderen Geräten automatisch synchronisiert. Da dieser Vorgang komplett asynchron passiert, gibt es keine Garantie und keine Rückmeldung, ob die Synchronisation funktioniert hat. 

Falls auf irgendeinem Gerät ein Tastencode fehlt oder nicht aktuell ist, kann man hier diese Tastencode ID erneut synchronisieren lassen.

<!-- DOC -->
#### **Tastencode ID synchronisieren**

Durch die Eingabe einer Tastencode ID und das Drücken des Buttons "Synchronisieren" kann hier die Synchronisation eines bestimmten Tastencodes manuell angestoßen werden.

### **Gefährliche Funktionen**

Die Funktionen in diesem Bereich können zum Datenverlust führen, der nicht behebbar ist. Bitte mit Vorsicht nutzen.

<!-- DOC -->
#### **Alle Tastencodes löschen?**

Mit dieser Funktion werden sämtliche gespeicherten Tastencodes inklusive aller Tastencode Namen unwiderruflich gelöscht.







<!-- DOC -->
## Aktionen

Aktioen sind die Objekte, die beim Zutrittssystem etwas machen. Aktionen können benannt werden und definieren das Ausgangs-KO und dessen verhalten, wenn diese Aktion ausgelöst wird.

Es gibt 2 Typen von Aktionen:

**Direkte Aktionen**: Solche Aktionen weren direkt durch das Zutrittssystem aufgerufen. Sie haben ein Ausgangs-KO und optional ein Eingangs-KO (z.B. beim Umschalten). Über ein Sperr-KO kann die Aktion gesperrt werden.

**Autorisierungsaktionen**: Diese Aktionen werden nicht direkt vom Zutrittssystem aufgerufen, sondern über ein Aufrufen-KO. Anschließend kann die Aktion durch das Zutrittssystem authorisiert werden. Hier wird also das Zutrittssystem nicht dazu genutzt, die Aktion auszulösen sondern zu bestätigen, dass die Person diese Aktion ausführen darf, dafür also autorisiert ist. Auf diese Weise können mit einem Finger oder einem NFC-Tag viele Aktionen autorisiert werden.Autorisierungsaktionen haben keine eigene Sperre, hier sollte bereits der Aufruf der Aktion durch eine Sperre des Aufrufers verhindert werden. Falls der Aufrufer keine Sperre besitzt, kann diese leicht durch eine TOR-Logik im beiliegenden Logikmodul realisiert werden.

<!-- DOC -->
### **Verfügbare Aktionen**

Hier kann hier ausgewählt werden, wie viele Aktionen sichtbar und editierbar sind. 

Die ETS ist auch schneller in der Anzeige, wenn sie weniger (leere) Aktionen darstellen muss. Insofern macht es Sinn, nur so viele Aktionen anzuzeigen, wie man wirklich braucht.

### **Autorisierung**

<!-- DOC -->
#### **Warten auf Autorisierung**

Ist eine Autorisierung für eine Aktion angefordert, wird die hier angegebene Zeit auf das Auflegen eines Fingers oder das Vorhalten eines NFC-Tags am Scanner gewartet. Falls die Zeit ohne Autorisierung verstreicht, wird die Aktion abgebrochen.

### **Tabelle der Aktionen**

In der folgenden Tabelle können die vefügbaren Aktionen definiert werden. Pro Zeile erfolgt eine Aktionsdefinition. Im Folgenden werden die einzelnen Felder einer Zeile beschrieben.

#### **Nummer der Aktion**

Die Nummer der Aktion ist nicht eingebbar und enstpricht der Zeilennummer der Tabelle. Diese Nummer wird dazu verwendet, eine Aktion einem Finger oder einem NFC-Tag zuzuordnen.

<!-- DOC -->
#### **Name der Aktion**

Dies ist ein frei vergebbarer Text, der beschreibt, was die Aktion macht. Dieser Text erscheint auch als Text für die Kommunikationsobjekte der Aktion.

<!-- DOC -->
#### **Deaktiviert**

Die Spaltenüberschrift ist D*.

Eine Aktion kann über diese Checkbox deaktiviert werden. Die Aktion sendet nichts mehr auf den Bus. Das kann dazu genutzt werden, Aktionen temporär zu deaktivieren oder zu Testzwecken, falls man z.B. nach unabsichtlich ausgelösten Aktionen sucht.

<!-- DOC HelpContext="Autorisierungsaktion" -->
#### **Autorisierungsaktion**

Die Spaltenüberschrift ist A**.

Wenn diese Checkbox eingeschaltet ist, handelt es sich bei dieser Aktion um eine Autorisierungsaktion.

<!-- DOC -->
#### **Typ der Aktion**

In der Auswahlbox kann man auswählen, was die Aktion macht. Passend zur Auswahl erscheinen in der Spalte **Weitere Argumente** die für diese Aktion erforderlichen Eingabemöglichkeiten.

##### **aus**

Die Aktion ist ausgeschaltet und hat keine Kommunikationsobjekte und keine weiteren Argumente.

##### **Schalten**

Diese Aktion kann ein EIN- oder ein AUS-Signal auf den Bus senden. Dazu erscheint ein passendes Ausgangs-KO. Ob ein EIN- oder ein AUS-Signal geschickt wird, bestimmt man über die erscheinenden weiteren Argumente.

##### **Umschalten**

Diese Aktion schaltet von EIN auf AUS und umgekehrt. Sie hat ein Ausgangs-KO und ein Eingangs-KO. Das Schaltsignal (Ausgangs-KO) ist immer der Invertierte Wert vom Eingangs-KO. Wird am Eingangs-KO kein Signal vor dem nächsten Schalten empfangen (weil es z.B. nicht mit einer GA verknüpft ist), dann wird der letzte Schaltzustand invertiert gesendet.

##### **Treppenlicht**

Diese Aktion sendet ein EIN- gefolgt von einem AUS-Signal auf das Ausgangs-KO. Die Zeit, die zwischen dem EIN- und AUS-Signal liegt, kann über weitere Argumente eingestellt werden. Falls dieses einfache Treppenlicht nicht ausreicht, kann ein deutlich funktionsreicheres Treppenlicht im beiliegenden Logikmodul genutzt werden.


<!-- DOC HelpContext="Finger-Seite" -->
## **Finger**

Erscheint nur, wenn bei Fingerprint-Scanner eine entscprechende Hardware ausgewählt wurde.

Im oberen Bereich können bereits angelernte Finger und Personen gesucht werden. So kann man überprüfen, welche Finger und Personen schon bekannt sind. 

Diese Funktionalität ist leider vollkommen von der Übertragungs-Infrastruktuktur der ETS abhängig, die wiederum von der Topologie und den verwendeten Schnittstellen und Linienkopplern abhängt. Schlagwort hier ist die APDU des Übertragungsweges (es würde zu weit führen, das hier auszuführen - siehe KNX-User-Forum). Für diese Funktion wird eine APDU vom 220 benötigt. Wenn nur eine kleinere APDU verfügbar ist, sollte die Suche nicht verwendet werden.

Im unteren Bereich können angelernte Finger zu Aktionen zugeordnet werden. Dieser Bereich kann unabhängig von der Übertragungs-Infrastruktur verwendet werden. 

<!-- DOCEND -->


### **Zuweisung von Fingern zu Aktionen**

In der folgenden Tabelle könenn angelernte Finger zu Aktionen zugeordnet werden. 

<!-- DOC -->
#### **Verfügbare Zuordnungen**

Hier wird die Anzahl der sichbaren Zeilen der Zuordnugnstabelle eingestellt. Da die ETS schneller ist, je weniger sie darstellen muss, sollte hier nur die Anzahl der wirklich benötigten Zeilen eingestellt werden.

<!-- DOC -->
#### **Aktion**

Die Nummer einer definierten Aktion, die zugeordnet werden soll.

<!-- DOC -->
#### **Finger**

Der angelernte Finger, der dieser Aktion zugeorndet werden soll.

<!-- DOC -->
#### **Information**

Die Zuordnung kann mit einem beliebigen Text benannt werden. Man kann einen Vorschlagstext generieren lassen, indem man den Button "Info holen" betätigt.

<!-- DOC HelpContext="NFC-Seite" -->
## **NFC**

Erscheint nur, wenn bei NFC-Scanner eine entscprechende Hardware ausgewählt wurde.

Im oberen Bereich können bereits angelernte NFC-Tags gesucht werden. So kann man überprüfen, welche NFC-Tags schon bekannt sind. 

Diese Suchfunktionalität ist leider vollkommen von der Übertragungs-Infrastruktuktur der ETS abhängig, die wiederum von der Topologie und den verwendeten Schnittstellen und Linienkopplern abhängt. Schlagwort hier ist die APDU des Übertragungsweges (es würde zu weit führen, das hier auszuführen - siehe KNX-User-Forum). Für diese Funktion wird eine APDU vom 203 benötigt. Wenn nur eine kleinere APDU verfügbar ist, sollte die Suche nicht verwendet werden.

Im unteren Bereich können angelernte NFC-Tags zu Aktionen zugeordnet werden. Dieser Bereich kann unabhängig von der Übertragungs-Infrastruktur verwendet werden. 

<!-- DOCEND -->

### **Zuweisung von NFC-Tags zu Aktionen**

In der folgenden Tabelle könenn angelernte Finger zu Aktionen zugeordnet werden. 

#### **Verfügbare Zuordnungen**

Hier wird die Anzahl der sichbaren Zeilen der Zuordnugnstabelle eingestellt. Da die ETS schneller ist, je weniger sie darstellen muss, sollte hier nur die Anzahl der wirklich benötigten Zeilen eingestellt werden.

#### **Aktion**

Die Nummer einer definierten Aktion, die zugeordnet werden soll.

<!-- DOC -->
#### **Tag**

Der angelernte NFC-Tag, der dieser Aktion zugeorndet werden soll.

#### **Information**

Die Zuordnung kann mit einem beliebigen Text benannt werden. Man kann einen Vorschlagstext generieren lassen, indem man den Button "Info holen" betätigt.

<!-- DOC HelpContext="KEY-Seite" -->
## **Keypad**

Erscheint nur, wenn bei Keypad eine entscprechende Hardware ausgewählt wurde.

Im oberen Bereich können bereits angelernte Tastencodes gesucht werden. So kann man überprüfen, welche Tastencodes schon bekannt sind. 

Diese Suchfunktionalität ist leider vollkommen von der Übertragungs-Infrastruktuktur der ETS abhängig, die wiederum von der Topologie und den verwendeten Schnittstellen und Linienkopplern abhängt. Schlagwort hier ist die APDU des Übertragungsweges (es würde zu weit führen, das hier auszuführen - siehe KNX-User-Forum). Für diese Funktion wird eine APDU vom 203 benötigt. Wenn nur eine kleinere APDU verfügbar ist, sollte die Suche nicht verwendet werden.

Im unteren Bereich können definierte Tastencodes zu Aktionen zugeordnet werden. Dieser Bereich kann unabhängig von der Übertragungs-Infrastruktur verwendet werden. 

<!-- DOCEND -->




## **Unterstützte Hardware**

Die Software für dieses Release wurde auf folgender Hardware getestet und läuft damit "out-of-the-box":

* **AB-SmartHouse Fingerprint-Leser** [www.ab-smarthouse.com](https://www.ab-smarthouse.com/produkt/openknx-fingerprint-leser/) als Basisplatine mit Finger-Lesegeräte R503, R503S und R503Pro

Andere Hardware kann genutzt werden, jedoch muss das Projekt dann neu kompiliert und gegebenenfalls angepasst werden. Alle notwendigen Teile für ein Aufsetzen der Build-Umgebung inklusive aller notwendigen Projekte finden sich im [OpenKNX-Projekt](https://github.com/OpenKNX).

Interessierte sollten auch die Beiträge im [OpenKNX-Forum](https://knx-user-forum.de/forum/projektforen/openknx) studieren.
