### Typ der Aktion

In der Auswahlbox kann man auswählen, was die Aktion macht. Passend zur Auswahl erscheinen in der Spalte **Weitere Argumente** die für diese Aktion erforderlichen Eingabemöglichkeiten.

##### aus

Die Aktion ist ausgeschaltet und hat keine Kommunikationsobjekte und keine weiteren Argumente.

##### Schalten

Diese Aktion kann ein EIN- oder ein AUS-Signal auf den Bus senden. Dazu erscheint ein passendes Ausgangs-KO. Ob ein EIN- oder ein AUS-Signal geschickt wird, bestimmt man über die erscheinenden weiteren Argumente.

##### Umschalten

Diese Aktion schaltet von EIN auf AUS und umgekehrt. Sie hat ein Ausgangs-KO und ein Eingangs-KO. Das Schaltsignal (Ausgangs-KO) ist immer der Invertierte Wert vom Eingangs-KO. Wird am Eingangs-KO kein Signal vor dem nächsten Schalten empfangen (weil es z.B. nicht mit einer GA verknüpft ist), dann wird der letzte Schaltzustand invertiert gesendet.

##### Treppenlicht

Diese Aktion sendet ein EIN- gefolgt von einem AUS-Signal auf das Ausgangs-KO. Die Zeit, die zwischen dem EIN- und AUS-Signal liegt, kann über weitere Argumente eingestellt werden. Falls dieses einfache Treppenlicht nicht ausreicht, kann ein deutlich funktionsreicheres Treppenlicht im beiliegenden Logikmodul genutzt werden.


