# MemoMind — Course / randonnée GPS (gps)

Application **appairée** MemoMind : un affichage tête haute de course ou de randonnée sur les lunettes. Le téléphone possède le GPS (permission de localisation) et diffuse les métriques ; les lunettes les affichent en grand.

## Composition

| Côté | Dossier | Artefact |
| --- | --- | --- |
| Lunettes (plugin natif C) | `glass/gps/` | `builds/gps.gmp` |
| Téléphone (plugin Web) | `phone/gps/` | `builds/gps-1.0.0.mmpkg` |

## Fonctionnement

- Le téléphone suit la position (permission `location.foreground`) et calcule distance cumulée, allure (`min/km`), vitesse et cap à partir de la distance orthodromique entre points consécutifs. Les sauts de position implausibles (> 15 m/s) sont ignorés.
- Les lunettes affichent la distance (m ou km), la durée, l'allure et le cap (rose des vents + degrés + km/h). Appui court sur le bouton = démarrer / pause, appui long = terminer et réinitialiser la sortie.
- Le téléphone journalise les sorties (distance du jour, distance et nombre de sorties sur 7 jours).
- Bilingue FR/EN côté lunettes (selon la locale de l'appareil).

## Protocole binaire

`DOWNLINK_CHANNEL = 0x4750` ('GP', téléphone → lunettes), `UPLINK_CHANNEL = 0x4751` ('GQ', lunettes → téléphone), version 1.

- `MSG_METRICS` = `[version, msg, duration_sec u16, distance_m u32, pace_sec_km u16, bearing u16, speed_dm_s u16, flags]` (15 octets)
- `MSG_RESET` = `[version, msg]`
- `EVENT_BUTTON` = `[version, event, action]` (`action` 0 = court = toggle, 1 = long = reset)

## Build

Nécessite le SDK officiel MemoMind (`memomind-open/plugin-open-platform`). Déposer `glass/gps/` dans `GlassSDK/examples/` et `phone/gps/` dans `PhoneSDK/examples/`, puis lancer `python3 build.py`.

- Lunettes : `python3 build.py glass` → `.gmp`
- Téléphone : `python3 build.py web` → `.mmpkg`
