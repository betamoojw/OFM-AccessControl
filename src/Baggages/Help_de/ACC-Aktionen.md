### Aktionen

Aktioen sind die Objekte, die beim Zutrittssystem etwas machen. Aktionen können benannt werden und definieren das Ausgangs-KO und dessen verhalten, wenn diese Aktion ausgelöst wird.

Es gibt 2 Typen von Aktionen:

**Direkte Aktionen**: Solche Aktionen weren direkt durch das Zutrittssystem aufgerufen. Sie haben ein Ausgangs-KO und optional ein Eingangs-KO (z.B. beim Umschalten). Über ein Sperr-KO kann die Aktion gesperrt werden.

**Autorisierungsaktionen**: Diese Aktionen werden nicht direkt vom Zutrittssystem aufgerufen, sondern über ein Aufrufen-KO. Anschließend kann die Aktion durch das Zutrittssystem authorisiert werden. Hier wird also das Zutrittssystem nicht dazu genutzt, die Aktion auszulösen sondern zu bestätigen, dass die Person diese Aktion ausführen darf, dafür also autorisiert ist. Auf diese Weise können mit einem Finger oder einem NFC-Tag viele Aktionen autorisiert werden.Autorisierungsaktionen haben keine eigene Sperre, hier sollte bereits der Aufruf der Aktion durch eine Sperre des Aufrufers verhindert werden. Falls der Aufrufer keine Sperre besitzt, kann diese leicht durch eine TOR-Logik im beiliegenden Logikmodul realisiert werden.

