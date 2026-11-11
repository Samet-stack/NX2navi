# Mesures sur le PC de Malayta (7700X, RTX 4070 SUPER, écran 1440p 480 Hz)

Ce guide est pour la version déjà installée, `D:\Games\NX2navi\ocarina.exe` (NX2navi 0.8.0, jeu 1.1.1, build e0f3ccfa).
Le premier lancement à faire ne compile rien. Il relance ce jeu avec le nouvel `nx2navi.ini`, plus deux lignes de diagnostic.

Les chiffres plus bas viennent du lancement de 23:36 à 23:44 (heure de Paris) le 11 novembre 2026, fichier
`logs\nx2navi.log`. Ce lancement a lu l’ancien ini (15 clés). Le nouvel ini (HDR native, `framegen_multiplier = 2`,
`audio_stats = 1`, overlay `full`) n’a pas encore été utilisé en jeu.

## 1. Prochaine relance (à faire en premier)

1. Ferme le jeu s’il tourne.
2. Ouvre `D:\Games\NX2navi\nx2navi.ini`. Garde le fichier actuel (HDR native, DLSS Quality, `framegen = dlss`,
   `framegen_multiplier = 2`, `fps_limit = display`, `audio_stats = 1`).
3. Ajoute ces lignes. `trace` et `replay_time` sont lus au démarrage (`runtime/cfg.c`, lignes du tableau des réglages :
   `TRACE` et `REPLAY_TIME` en `RESTART`).

```ini
[performance]
replay_time = 1

[diagnostics]
trace = perf
```

`audio_stats = 1` est déjà dans la section `[audio]`. Ne mets pas `mem_census = 1` : avec `trace = perf`, ce réglage
marche l’espace d’adressage et retarde une image de 100 à 240 ms (`nx2navi.ini.example` lignes 754-757).

4. Copie le dossier `D:\Games\NX2navi\profile` ailleurs si tu veux pouvoir revenir à la sauvegarde. Le jeu écrit
   dedans tout seul.
5. Lance `D:\Games\NX2navi\ocarina.exe`. Laisse passer le film d’ouverture. Charge la même sauvegarde, va en forêt
   Kokiri, devant la maison de Link. Attends 20 secondes après le chargement. Marche la même boucle pendant 60 secondes.
   Parle à un PNJ pendant 15 secondes (pour l’audio). Ferme le jeu par le menu, pour que la dernière ligne
   `frametime: exit` soit écrite.
6. Le log utile est `D:\Games\NX2navi\logs\nx2navi.log`. L’ancien passe dans `nx2navi.1.log`. Copie `nx2navi.log` avant
   de relancer, sinon il est poussé.

Vérifie dans les 40 premières lignes que le fichier a bien été lu. Tu dois voir `trace` n’apparaît pas dans la ligne
« sets » si la clé est inconnue ; elle doit être absente de la ligne `no setting of this version`. Tu dois voir plus
loin des lignes qui commencent par `perf: frame`.

`replay_time = 1` remplit la colonne « on the CPU » des lignes `frametime:`. Dans le log de 23:36 cette colonne vaut
`0.0` partout parce que le réglage était à son défaut 0 : le temps CPU du thread de replay n’est alors pas lu
(`runtime/vk_sync.c` lignes 763-783). Ce `0.0` ne veut pas dire que le thread n’a pas travaillé.

## 2. Ce que tu notes

Ouvre le log avec le Bloc-notes. Cherche `frametime:` et `perf: frame`.

Tableau, une ligne par bloc `frametime:` de 60 s pendant que tu es en forêt (pas pendant le film : le film est forcé
à 60 images par seconde, `runtime/frame_rate.c` `movie_cap_hz`, et le log de 23:36 l’écrit
`frame rate: a movie plays: 60 fps`).

| Bloc (numéro de la ligne `frametime:`) | fps moyen | 1 % low | images > 25 ms | pire image (ms) | cause écrite à côté (replay, pipeline, fence) | `bound:` de la ligne `perf:` la plus proche | `gpu avg` (ms) | `vram` (MiB) |
|---|---|---|---|---|---|---|---|---|

La ligne `perf:` est écrite par `runtime/perf.c` `perf_gpu_line` (autour des lignes 660-681). `bound: cpu` veut dire
que le GPU a été occupé moins de 80 % de l’intervalle entre images. `bound: gpu` veut dire l’inverse. `bound: pace`
veut dire que l’attente du limiteur a pris au moins 15 % de la fenêtre. Sans ces lignes, on ne sait pas qui limite.

Cherche aussi :

- `dlssg:` et `presented per frame`. `2` veut dire qu’à cet instant l’écran a reçu une image générée en plus.
  `1` veut dire que la génération était coupée à cet instant (menu, cinématique, ou image sans profondeur :
  `runtime/dlssg/nx2_sl.cpp` lignes 246-254).
- `audio: stats`. Avec `audio_stats = 1`, une ligne toutes les ~30 s dit si des voix ont manqué de tampon
  (`runtime/audren.c` `stats_report`).
- `audio: device sink inputs`. Le log de 23:36 a `downmix off 0.000 0.000 0.000 0.000`. Recopie la ligne du nouveau
  lancement.

Contrôle d’image, une fois, devant la grotte (rayons de lumière) : pas d’image noire, pas de carrés verts ou bleus.
Si tu en vois, note `replay_async` (il doit rester à 0 pour cette relance).

## 3. Option audio, un lancement à part

`audio_stats = 1` ne donne pas le volume par oreille. Pour voir si la voix des PNJ est sur le canal centre et à quel
niveau, fais un second lancement court :

```ini
[diagnostics]
trace = audio
```

Parle à un PNJ 20 secondes, quitte. Dans le log, cherche `audio: device sink inputs rms` (une ligne toutes les 5 s,
`runtime/audio_mixer.c` `level_note`). Recopie FL, FR, C. Remets ensuite `trace = perf` et enlève `audio`.
`trace = audio` écrit aussi une ligne à chaque voix qui démarre : ne le laisse pas pour la mesure de FPS.

## 4. Lire le log avec les scripts du dépôt

Ces scripts ne sont pas à côté de `ocarina.exe`. Ils sont dans le dépôt source.

```bat
py -I tools\perf_summary.py D:\Games\NX2navi\logs\nx2navi.log 1 999999
```

Le script lit les lignes `perf: frame` (`tools/perf_summary.py` lignes 12-16). Sans ces lignes il n’a rien à résumer :
c’est le cas du log de 23:36.

`tools/ab_summary.py` ne sert que quand un réglage vaut `ab` et que `ab = 20` est posé. Pas pour cette première relance.

## 5. Revenir au jeu normal

Dans `nx2navi.ini`, efface `trace = perf` et `replay_time = 1`, ou remets :

```ini
replay_time = 0
```

et supprime la ligne `trace`. Relance. Le chemin par défaut du 0.8.0 installé est celui du lancement de 23:36, plus
les clés déjà dans ton nouvel ini.

Pour revenir au binaire du launcher si une branche a été compilée par-dessus `D:\Games\NX2navi` : le launcher 0.8.0
réinstalle sa version (journal du 11/11, `finished in 26.4 min`). Ne lance pas une compilation tant que cette relance
de diagnostic n’est pas faite.

## 6. Ce que le log de 23:36 a déjà mesuré

À ne pas refaire juste pour les revoir. Détail dans le rapport. En bref, sur cet exe 0.8.0, ancien ini,
`framegen_multiplier = 4` abaissé à 2x par le pilote, pas de `trace` :

- Écran mesuré à 480,168 Hz. Plafond écrit dans le log : le jeu jusqu’à 240 images/s, l’écran 480 avec la génération x2.
- En jeu, après les films, les blocs de 60 s font environ 64 à 100 images/s de jeu, 1 % low environ 35 à 45.
  Le plafond 240 n’est pas ce qui limite cette session.
- Les images lentes sont surtout attribuées au replay (dizaines de ms), plus une image de 2,0 s au premier gros
  chargement (compilation de pipelines).
- La voix 0 joue. `audio_stats` était éteint. Le niveau des voix de PNJ n’est pas dans ce log.
