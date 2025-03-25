### NFC

Erscheint nur, wenn bei NFC-Scanner eine entscprechende Hardware ausgewählt wurde.

Im oberen Bereich können bereits angelernte NFC-Tags gesucht werden. So kann man überprüfen, welche NFC-Tags schon bekannt sind. 

Diese Suchfunktionalität ist leider vollkommen von der Übertragungs-Infrastruktuktur der ETS abhängig, die wiederum von der Topologie und den verwendeten Schnittstellen und Linienkopplern abhängt. Schlagwort hier ist die APDU des Übertragungsweges (es würde zu weit führen, das hier auszuführen - siehe KNX-User-Forum). Für diese Funktion wird eine APDU vom 203 benötigt. Wenn nur eine kleinere APDU verfügbar ist, sollte die Suche nicht verwendet werden.

Im unteren Bereich können angelernte NFC-Tags zu Aktionen zugeordnet werden. Dieser Bereich kann unabhängig von der Übertragungs-Infrastruktur verwendet werden. 

