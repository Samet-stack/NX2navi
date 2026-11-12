# Frame generation sur une RTX 4070 SUPER : ce que le code, le journal et NVIDIA permettent

Étude sur le source de cette branche (`main` au moment de l’écriture, NX2navi 0.8.0 dans le journal) et sur le journal `nx2navi_live.log` joint (session du 11 novembre 2026, 23:55 heure locale, `NX2navi 0.8.0`, jeu 1.1.1). Aucun chiffre de latence en millisecondes n’a été mesuré dans ce journal : là où une durée se déduit d’un taux, elle est marquée comme un calcul, pas comme une mesure chronométrée.

Machine lue dans le journal, pas supposée : `NVIDIA GeForce RTX 4070 SUPER`, pilote NVIDIA `621.01`, API Vulkan `1.4.357`, tas device-local `11997 MiB`, fenêtre `2560x1440`, moniteur `480.168 Hz` (`nx2navi_live.log` lignes 20, 43, 64, 82). Le processeur n’est pas nommé par le journal.

Ini lu au démarrage (ligne 2) : `dlss = quality`, `taa = dlss`, `dlaa = 1`, `framegen = dlss`, `framegen_multiplier = 2`, `fps_limit = display`, `vk_present = mailbox`, `hdr = native`, `replay_async = 0`, `one_per_core = 0`, `fsr4 = 0`, `dlss_preset = default`, `fxaa = 0`, `upscaler = bilinear`. `reflex` n’est pas dans la liste des clés posées : le défaut du code s’applique (section 4).

## 1. Menu, launcher, clés ini

Les onglets réellement dessinés sont Écran, Jeu, Son, Contrôles, et Co-op seulement si `coop_beta = 1` (`runtime/ui/menu.cpp`, `kTabNames` vers la ligne 1566, `tabs_shown` vers 1572, `layout_menu` vers 1688). Les textes `UI_MENU_TAB_ADVANCED` et `UI_MENU_REPLAY_ASYNC` existent dans `runtime/ui_text.c` (lignes 44 et 379) mais aucun appel ne les place dans la barre d’onglets. `nx2navi.ini.example` lignes 57-58 dit pourtant que l’onglet Advanced montre les clés de `[performance]` et `[diagnostics]`. Dans ce source, ces clés se règlent dans le fichier, pas dans le menu.

Le launcher n’écrit qu’une clé du jeu : `discord_rpc` (`launcher/ui/app.py`, `discord_row` vers 878 et `rows_settings` vers 1724). Ses propres interrupteurs (`auto_check`, `auto_install`, `hide_while_playing`, `keep_cache`, canal de mises à jour) ne touchent ni le frame generation ni Vulkan. Les réglages graphiques sont ceux du menu en jeu et de `nx2navi.ini`.

Une clé absente du tableau `known` de `runtime/cfg.c` (ligne 36) est ignorée et nommée au démarrage. Une clé connue sans ligne dans `metas` (ligne 163) est lue par `cfg_get` mais n’a pas de valeur par défaut dans le tableau : le consommateur décide. `LIVE` dans `metas` veut dire que le menu peut l’appliquer sans redémarrage ; le consommateur peut quand même ne la relire qu’une fois.

Sur le build Ocarina du journal, les patches `wide_*` et `res_fixed` sont éteints (ligne 10) et `output_res` / `render_scale` ne font rien (ligne 14). Les rangées Résolution et format d’image du menu ne s’affichent que si `wide_available()` / `res_adjustable()` (`menu.cpp` `tab_screen`, vers 1217-1224). Elles sont donc absentes de ce build. Le mode DLSS, lui, réduit bien la scène : le journal dit `DLSS quality: the scene at 67 % of the output` puis `1707x960 -> 2560x1440` (lignes 86 et 1648). C’est `runtime/resolution.c` vers 156-158, avec les parts de `runtime/widescreen.c` `dlss_share` (lignes 91-96) : DLAA `1`, quality `2/3`, balanced `0.58`, performance `1/2`, ultra performance `1/3`.

### Onglet Écran (`tab_screen`, `menu.cpp` vers 1204)

| Rangée | Clés écrites | Valeurs que le menu pose | Défaut `metas` | Effet |
|---|---|---|---|---|
| Plein écran | `fullscreen` | `0`, `1` | `0`, live | tout de suite |
| Synchronisation verticale | `vk_present` | interrupteur : `mailbox` si oui, `immediate` si non. `fifo` déjà dans le fichier est affiché comme oui (`menu.cpp` vers 1210-1212) | `mailbox`, parmi `mailbox\|fifo\|immediate`, live (`cfg.c` ligne 166) | le mode réel peut différer ; la note « in use » vient de `vsync_note` (vers 867) |
| GPU | `gpu_index` | index listé, ou vide | vide, redémarrage | `vk_device.c` |
| Overlay | `perf_overlay` | `off`, `fps`, `full` | `off`, live | F10 cycle (`menu.cpp` `menu_perf_cycle`, vers 1774). En `fps`, s’il y a un taux de sortie, la ligne montre le jeu et l’écran (`perf_tick`, vers 1827) |
| Mise à l’échelle | `upscaler`, `dlaa`, `dlss`, `taa`, et `fsr4` si FSR 3.1 ou FSR 4 | voir plus bas | `upscaler=fsr`, `dlaa=0`, `dlss=dlaa`, `taa=dlss`, `fsr4=0` | une passe temporelle force `upscaler=bilinear` |
| Frame generation | `framegen`, et `fsr4_fg` / `taa` / `fsr4` si la bibliothèque FSR 4 est choisie | `off`, `fsr3`, `dlss` | `off`, live dans le tableau, mais DLSS attend un redémarrage (`framegen_restart`, vers 1073) | ci-dessous |
| Multiplicateur DLSS | `framegen_multiplier` (Windows) ou `fg_mult` (ailleurs) | `2`,`3`,`4` dans la limite Streamline ; sous NGX : `2` à `6` et `dynamic` | `2` ; `fg_mult` = `2\|3\|4\|5\|6\|dynamic` | tout de suite |
| HDR | `hdr`, puis `hdr_peak_nits`, `hdr_game_nits`, `hdr_ui_nits` | `0`, `1`, `native` ; pics 400..10000 pas 50 ; blancs 48..500 | `0`, `1000`, `203`, `203` | `native` attend un redémarrage (`vk_hdr.c`) |
| Netteté | `sharpness` | 0..100 pas 5 | `80` | actif sous FSR 1 ou FSR 3.1 ; la rangée est grisée sinon (vers 1230) |
| Anisotropie | `aniso` | `game`, `2`, `4`, `8`, `16` | `game` | tout de suite |
| Détail des textures | `texture_detail` | `auto`, `full`, `reduced`, `low` | `auto`, redémarrage | `vk_texture.c` |

La rangée Mise à l’échelle (`row_upscaling`, vers 930) écrit d’un coup :

- Bilinéaire : `upscaler=bilinear`, `dlaa=0`, et `framegen=off` (le FG a besoin des vecteurs, vers 986).
- FSR 1 : `upscaler=fsr`, `dlaa=0`, `framegen=off`.
- DLSS, cinq modes : `dlaa=1`, `taa=dlss`, `dlss` = `dlaa|quality|balanced|performance|ultra_performance`. Proposé si le vendeur PCI est inconnu ou `0x10de` (`dlss_offered`, vers 885). Passer à DLSS depuis un état où NGX n’est pas monté attend un redémarrage (`vk_dlaa_needs_restart`).
- FSR 3.1, les mêmes cinq échelles, libellé Native AA pour le plein format : `taa=fsr3`, `fsr4=0`. Là où `amd_fidelityfx_vk.dll` se charge (`vk_ffx_available`).
- FSR 4, mêmes modes, seulement si la bibliothèque le liste pour ce GPU (`vk_ffx4_available`) : `fsr4=1`. Le journal de cette session, à l’ouverture du menu, dit qu’aucune version listée n’est FSR 4 et que FSR 3.1 reste (ligne 11542).

La rangée Frame generation (`row_framegen`, vers 1093) propose, selon ce qui se charge :

- Off.
- `AMD FSR 3.1` si `vk_ffx_framegen_available` : écrit `framegen=fsr3`. S’applique sans redémarrage (`vk_fg.cpp`).
- `AMD FSR 4 library` si FSR 4 et sa bibliothèque de génération sont là : écrit `framegen=fsr3` et `fsr4_fg=1`, et bascule la mise à l’échelle sur FSR 4. Le code dit que la bibliothèque choisit la génération apprise sur RX 9000 (non testée ici) et `3.1.6` ailleurs (`nx2navi.ini.example` lignes 464-469, `vk_fg.h` lignes 30-34).
- `NVIDIA DLSS` sur un GPU NVIDIA : `framegen=dlss`. Monter ou couper DLSS-G attend un redémarrage, parce que l’instance Vulkan passe par Streamline (`vk_dlssg.c` en-tête, `framegen_restart`).

Le multiplicateur Windows (`row_framegen_mult`, vers 1024) n’affiche que `2x` quand Streamline rapporte un maximum de 2, avec le texte « RTX 50 » ou « DLL ancienne » (`UI_MENU_FRAMEGEN_MULT_RTX50`). Avant que Streamline ait parlé, la rangée liste `2x` `3x` `4x`. Sous Linux, DLSS-G passe par NGX et la rangée lit `fg_mult`, pas `framegen_multiplier` (`row_framegen_mult_ngx`, vers 1052 ; `vk_dlaa.h`). Cette session est Windows : `fg_mult` n’est pas lu par `vk_dlssg.c`.

### Onglet Jeu, ce qui change le rythme

`fps_limit` (`row_fps_limit`, vers 840) : `30`, `40`, `45`, `60`, `90`, `120`, `144`, `165`, `240`, `display`, et `unlimited` seulement si le build avance à l’horloge (`frame_free_running`). Le parseur (`frame_rate.c` `limit_parse`, vers 148) accepte en plus tout entier de 30 à 240 ; au-dessus, il ramène à 240. Défaut du tableau : `60` (`cfg.c` ligne 206). `display` compte les images à l’écran quand le FG tourne, le jeu étant tenu au taux du moniteur divisé par le multiplicateur, puis plafonné à 240 (`frame_rate.c` `limit_game_hz` vers 221, `LIMIT_MAX_HZ` et `LIMIT_FREE_MAX_HZ` lignes 125 et 141). Le journal l’écrit : `the game draws up to 240 frames a second and frame generation shows 2 for each (480 on screen)` (ligne 1724). C’est le plafond du cadenceur, pas un taux mesuré.

`unlimited` sur un build qui n’avance pas à l’horloge est ramené à 240 (ligne 155). Le journal dit que ce build suit l’horloge (`set ostep`, ligne 12) et qu’il n’a pas la table de patch 30/60 (ligne 13). `frame_free_running` est vrai quand les jeux de patch `fps60` et `realtime` sont absents (`frame_rate.c` ligne 142). Le plafond 240 s’applique quand même (`LIMIT_FREE_MAX_HZ`).

Les autres rangées de l’onglet Jeu (`operation_mode` `docked|handheld`, langue, `assume_focus`, `discord_rpc`, menu de debug) ne changent pas le FG. `realtime`, `cloth_realtime`, `highfps_fix`, `physics_steps`, `pickup_count`, `step_steady` sont des clés `[frame rate]` du fichier d’exemple (lignes 38-54), lues au démarrage, sans rangée dans `tab_game`.

### Clés graphiques et de rythme qui n’ont pas de rangée

Toutes sont dans `known[]` (`cfg.c`). Celles qui ont une ligne `metas` ont le défaut indiqué.

| Clé | Défaut et valeurs | Où c’est lu | Dans cette session |
|---|---|---|---|
| `reflex` | pas de ligne `metas`. `vk_dlssg.c` `reflex_mode` (lignes 66-73) : vide → `1` ; `0` off, `1` latence basse, `2` latence basse avec boost ; autre valeur → `1`. Lu une fois | `nx2sl_reflex` (`runtime/dlssg/nx2_sl.cpp` lignes 173-179) | non posée. Journal : `Reflex low-latency mode on (0)` (lignes 38 et 81). Le `(0)` est le code de retour Streamline (`eOk`), pas le mode. Le mot `on` est le mode `1` |
| `replay_async` | `0\|1`, défaut `0`, redémarrage (`cfg.c` ligne 248) | `vk_backend.c` vers 297 | `0`. Journal ligne 357 : `replay on the submitting thread` |
| `one_per_core` | `0\|1\|ab`, défaut `0`, redémarrage | `runtime/perf.c` `cpu_placement` vers 719 | `0` |
| `dlss_preset` | `default\|k\|j`, live. Le commentaire d’exemple (lignes 478-481) dit que d’autres lettres `a` à `m` sont acceptées par `vk_dlaa.c` | preset K mesuré pour quality (journal ligne 1648) | `default` |
| `dlss_char_mask` | `0\|35\|65\|100`, défaut `65` | `vk_dlaa.c` | non posée, donc 65 |
| `dlss_idle_free` | `0\|1\|2`, défaut `1`, redémarrage | `vk_dlaa.c` | défaut |
| `fxaa` | `0\|1`, défaut `1` | le jeu, lu en direct | `0` dans l’ini |
| `fsr4`, `fsr4_fg` | `0\|1` | `vk_ffx.cpp`, `vk_fg.cpp` | `fsr4=0`. FSR 4 refusé par la bibliothèque sur ce GPU (ligne 11542) |
| `fg_mult` | Windows : non lu par DLSS-G Streamline | `gfx_present.c` chemin NGX | sans effet ici |
| `dyn_res`, `dyn_res_target`, `dyn_res_step` | off, `16.7`, `45` | `dynres.c`, seulement avec un mode DLSS | non posées |
| `hdr_live` | `1`, redémarrage | `vk_hdr.c` | non posée |
| `movie_fps` | connue, pas dans `metas`. Défaut 60 dans `frame_rate.c` `movie_cap_hz` (ligne 202) | pendant un film | journal ligne 892 : `a movie plays: 60 fps` |
| `fg_hudless_show` | connue, pas dans `metas`. `1` montre l’image sans HUD à l’écran (`gfx_present.c` vers 1486) | test | non posée |
| `shadow_res`, `shadow_scale`, `shadow_range`, `contact_shadows`, `pcss`, `color_*`, `aspect`, `fov` | voir `metas` et l’exemple | pas de rangée dans `tab_screen` de ce source | non posées, sauf ce que l’ini liste |
| `recorder_core`, `recorder_prio`, `submit_batch`, `vk_recorder`, `timer_1ms`, `replay_time` | voir l’exemple `[performance]` | le fil qui enregistre les commandes Vulkan | cette session : `recorder_core=auto`, `recorder_prio=1`, `submit_batch=4`, `vk_recorder=1`, `timer_1ms=1`, `replay_time=1` |
| `pool_bind_skip` | absente de ce source | branche `cursor/pool-bind-skip-7f0d`, défaut `0` | cette session ne l’a pas |

`bind_skip` et les autres optimisations de replay (`shader_memo`, `pipe_memo`, `split_draws`, `wb_lazy`, `barrier_game`, …) sont documentées dans `nx2navi.ini.example` section `[performance]`. Elles ne sont pas des rangées du menu de ce source. Les citer toutes ici répéterait l’exemple ; celles qui touchent le fil de replay mesuré sont dans la section 5.

## 2. Comment le frame generation DLSS est branché

Windows, `framegen=dlss` et `dlaa=1` avec DLSS ou FSR 3.1 : `vk_dlssg_early` charge `nx2navi_dlssg.dll` à côté du jeu, qui charge `sl.interposer.dll` (`runtime/dlssg/nx2_sl.cpp` `nx2sl_init`, vers 95). L’instance et le device Vulkan passent par l’interposeur (`vk_dlssg_create_instance`, `vk_dlssg_create_device`). Les fonctions de swapchain aussi (`vk_dlssg_create_swapchain` et la suite).

Streamline charge trois features : DLSS-G, Reflex, PCL (`nx2_sl.cpp` lignes 112-126). Au device prêt, le journal de cette session dit `DLSS Frame Generation available, up to 2x` puis `on the swapchain` (lignes 34-35). Ce `2x` est `numFramesToGenerateMax + 1` (`nx2sl_available`, vers 157, et `gpu_multiplier_max` dans `vk_dlssg.c` lignes 198-202). Il n’y a pas de test « si le nom contient 4070, clamper à 2 ». Le clamp est la valeur que Streamline rapporte. `vk_dlssg_multiplier_of` (lignes 181-194) ramène `framegen_multiplier` à cet maximum, avec une ligne de log si la valeur posée est plus haute. Ici la valeur posée est déjà `2`, et le maximum rapporté est `2` : rien n’a été abaissé dans cette session. Une DLL sans `nx2sl_frames_max` est traitée comme 2x maximum (ligne 115 et 200).

À chaque présentation, `gfx_present.c` vers 1544 appelle `vk_dlssg_frame`. Celle-ci (`vk_dlssg.c` vers 285) demande à `vk_dlaa_fg_inputs`, `vk_dlaa_fg_views` et `vk_dlaa_fg_camera` la profondeur, les vecteurs et la caméra de la passe DLSS ou FSR 3.1. Le quatrième argument de `nx2sl_frame`, l’image sans HUD, est passé `NULL` (ligne 315). `hudless_record` (`gfx_present.c` lignes 905-911) ne construit cette image que si `vk_fg_active()` (FSR) ou `ng_running()` (NGX, pas le build Windows). Sur le chemin Streamline de cette session, l’image sans HUD n’est donc pas fournie. `nx2sl_frame` sait étiqueter `kBufferTypeHUDLessColor` si le pointeur n’est pas nul (`nx2_sl.cpp` lignes 234-240). Ici il étiquette deux ressources : profondeur et vecteurs.

La décision on/off tient 12 images (`FG_SETTLE_FRAMES`, `vk_dlssg.c` lignes 260-296). Voulu = profondeur, vecteurs et caméra présents, et menu fermé (`menu_is_open`). Un changement n’est appliqué qu’après 12 images dans le nouvel état ; pendant l’attente, l’état précédent reste, y compris « génération demandée » alors que les tampons du cadre courant sont nuls. Le journal le montre : à l’image 12259 Streamline écrit qu’il ne trouve pas les constantes, ni `kBufferTypeDepth`, ni `kBufferTypeMotionVectors`, et qu’il réutilise l’image 12258 (lignes 7048-7052), puis `frame generation off` arrive à la ligne 7065, après une série d’images `without the DLAA point` (profondeur de scène absente). Ce n’est pas une coupure sans cause. Les trois coupures de la session :

1. Ligne 447, avant toute scène armée. `600 frames, 1 presented per frame` (ligne 776). Le film d’ouverture est à 60 img/s de jeu (lignes 892 et 1137 ; fenêtres d’images 960 à 1380 : `60.0 fps`).
2. Ligne 7065, images sans point DLAA à partir de 12259. La génération revient (ligne 7256) sur la fenêtre de l’image 12600, qui est à `240.0 fps`.
3. Ligne 11562, juste après `menu: open` (ligne 11541) et des images `without a camera` (11533, 11543). Le menu coupe le FG par `!menu_is_open()`. La génération revient ligne 13272, fenêtre de l’image 24720.

`numFramesActuallyPresented` est ce que la dernière présentation a montré, pas une moyenne (`nx2_sl.cpp` lignes 266-277). Les échantillons : images 3600, 7200, 10800, 14400, 18000, 25200, 32400 → `2 presented per frame` ; images 600, 21600, 28800 → `1`. Le `1` de 21600 tombe dans la période menu (FG coupé). Il n’y a pas de compteur d’images affichées moyenné dans le journal. « Affiché ≈ jeu × 2 » n’est vrai que pendant que la génération est active et que cet échantillon vaut 2.

Reflex : `vk_dlssg_frame_start` (`vk_dlssg.c` ligne 266), appelé à la prise d’image de la fenêtre, dort dans `slReflexSleep` si une présentation hôte a eu lieu depuis moins de 200 ms (`HOLD_IDLE_NS`). Les marqueurs PCL vont de `eSimulationStart` à `ePresentEnd` (`nx2_sl.cpp` `nx2sl_sleep`, `nx2sl_submit_end`, `nx2sl_present_marker`). Le mode est reposé à chaque swapchain.

Le rythme : `generated_factor` (`frame_rate.c` ligne 214) vaut le multiplicateur DLSS pendant que la génération tourne, sinon 1 (ou 2 si le FG FSR dit qu’il génère). Avec `fps_limit=display`, le jeu est cadencé à `480.168 / 2 = 240.084`, puis coupé à 240. Le journal écrit ce plafond au moment où le FG s’allume (ligne 1724), pas un taux atteint.

FSR 3 dans le même binaire génère une image entre deux, facteur fixe 2 (`frame_rate.c` ligne 214, `vk_fg.cpp`). Il reçoit profondeur, vecteurs, caméra et l’image sans HUD (`vk_fg_frame`). Il n’y a pas d’appel Anti-Lag dans `runtime/`. Le multiplicateur 3 ou 4 n’existe pas sur ce chemin.

Avertissement VSync. Streamline, `dlfgPresent.cpp:1207`, texte exact : `VSync interval 1 not supported with FG, clamping to 1.` Le journal du jeu n’en garde que les occurrences puissances de deux, de 1 à 16384 (15 lignes, la dernière vers l’image 19560, ligne 11072 du journal). Le compteur 16384 est celui de Streamline. De l’allumage (fenêtre de l’image 2520) à l’image 19560 il y a environ 17 000 images de jeu ; 16384 est du même ordre. On peut dire que l’avertissement est compté sur la grande majorité des présentations tant que le FG est actif. On ne peut pas dire, à partir du fichier, qu’il l’est sur chacune : le journal n’a pas les occurrences intermédiaires. Le mode de présentation de la session est `mailbox` (ligne 67). Ce que « clamping to 1 » change dans le flip n’est pas décrit par notre code ; seule la phrase de Streamline est connue.

Autres avertissements Streamline au démarrage (lignes 28-36) : hooks `CmdBindPipeline`, `CmdBindDescriptorSets` et `BeginCommandBuffer` non supportés, et `slDLSSGGetState` qui devrait être synchronisé avec le fil de présentation. Le FG a quand même rapporté 2x et des échantillons à 2 images présentées. Effet de ces hooks manquants sur le taux : non mesuré.

## 3. Ce que le journal mesure

Fenêtres de 60 images, `perf: frame`, `bound` selon `runtime/perf.c` `perf_gpu_line` (vers 653) : `pace` si l’attente du cadenceur ≥ 15 % de la fenêtre, `gpu` si le GPU occupe ≥ 80 % de l’intervalle, sinon `cpu`. `pacing` est à 0 % sur toutes les fenêtres citées : le limiteur 240 ne retient pas ces images, sauf là où le taux est déjà sur 240.

Scènes nommées : `KokiriForest_Child` chargée image 844, posée image 1570 (lignes 888, 1200) ; `HyruleField_Child` chargée 2543, posée 2997 (lignes 1625, 2089) ; `InsideTheDekuTree` chargée 32583 (ligne 17274). Pas d’autre scène entre les deux.

Kokiri posée, images 1620 à 2520, 16 fenêtres : 220,6 à 240,9 img/s de jeu, médiane 240,0 ; replay 0,20 à 0,57 ms, médiane 0,32 ; GPU médian 0,11 ms ; `bound: cpu` partout ; `pacing` 0 %. Le FG n’est pas encore la source d’un échantillon « 2 presented » : le premier date de l’image 3600, après le chargement de la plaine. La ligne « 480 on screen » (1724) est écrite dans la même fenêtre que l’image 2520, au moment où le FG s’allume, à la bascule vers Hyrule. Ce 480 est le plafond `240 × 2`. Il n’est pas un taux affiché mesuré pendant la marche en Kokiri. Dire « Kokiri à 480 affichés » dépasse ce fichier.

Plaine, images 3000 à 9960, 117 fenêtres, toutes `bound: cpu` : 47,2 à 90,5 img/s, médiane 77,5 ; replay 4,65 à 14,67 ms, médiane 5,63 ; GPU médian 7,02 ms ; intervalle médian 12,90 ms.

Plaine, images 10020 à 19980, 167 fenêtres, toutes `bound: cpu` : 42,6 à 240,1 img/s, médiane 67,7 ; replay médian 8,06 ms ; GPU médian 7,76 ms ; intervalle médian 14,77 ms. Le 240,1 de cette plage est une fenêtre isolée, pas le régime de la plaine.

Sous-ensemble « lourd » défini après coup : images 3000 à 31920, fenêtres entre 60 et 85 img/s et replay entre 6 et 9 ms. 211 fenêtres sur 484. Médiane 73,0 img/s, replay médian 7,18 ms. C’est la bande « environ 70 img/s, replay 6 à 9 ms ». Elle ne décrit pas toute la plaine : la première heure de plaine (3000-9960) a un replay médian de 5,63 ms et 77,5 img/s.

Exemple chiffré d’une fenêtre lourde, image 12240 (lignes 7028-7030) : intervalle 15,65 ms (63,9 img/s), replay 8,65 ms, GPU 8,00 ms, `bound: cpu`, GPU inactif 7,63 ms entre passes dont 7,11 ms à attendre la soumission suivante. Le GPU attend le CPU. Le fil de replay est sur le fil qui soumet (`replay_async=0`), donc ces 8,65 ms sont dans les 15,65 ms, pas à côté.

Blocs `frametime:` de 60 s (images de jeu, pas affichées) : 91,3 (1 % low 38,5), 76,5 (58,8), 66,7 (41,7), 65,9 (42,1), 88,1 (42,6), 85,9 (44,0), 64,0 (27,6). Ils mélangent chargements, menu et jeu. Le bloc à 66,7 img/s (ligne 8043) attribue 22 images lentes sur 25 au replay, et dit que le fil de replay a passé 85 % de son temps mur sur le CPU (ligne 8046 : 33171 ms mur, 28250 ms CPU). Utile comme ordre de grandeur du fil, pas comme taux d’une seule clairière.

La plage 21420-24660, 55 fenêtres : médiane 192,8 img/s, replay médian 1,04 ms, GPU médian 3,67 ms, `bound: cpu`, `pacing` 0 %. Elle commence après `menu: open` (image ~20452) et pendant que l’échantillon 21600 dit `1 presented per frame`. Ce n’est pas la plaine en jeu avec FG. C’est une vue légère, menu ouvert, FG coupé. La revenir à 240 img/s de l’image 12600 suit le retour du FG après la coupure sans profondeur ; l’appeler « Kokiri » n’est pas dans le journal (la seule scène Kokiri est avant l’image 2543).

VRAM en plaine posée : 7374 MiB sur 11229 à l’image 2997 (ligne 2097). Plus tard, vers l’image 20400, 10062 sur 10947 (ligne 11490), toujours `bound: cpu` sur la fenêtre citée. Le tas de 12 Go n’est pas ce qui fixe les 70 img/s de ces fenêtres. Non mesuré : un autre réglage de textures changerait-il le taux.

Affiché, calcul et non mesure. Tant que l’échantillon Streamline vaut 2, l’écran reçoit deux images par image de jeu. 77,5 × 2 = 155, 73,0 × 2 = 146, 67,7 × 2 = 135. La fourchette « environ 139 » tombe dans ce calcul sur la partie lourde de la plaine. Elle n’est pas une lecture d’un compteur d’écran.

## 4. Le plafond matériel, sources NVIDIA et AMD

DLSS Multi Frame Generation 3x et 4x, au sens « jusqu’à trois images générées par image rendue », est décrit par NVIDIA comme une fonction des GeForce RTX 50 :

- [DLSS 4, GeForce News, janvier 2025](https://www.nvidia.com/en-us/geforce/news/dlss4-multi-frame-generation-ai-innovations/) : Multi Frame Generation « for GeForce RTX 50 Series », « up to three additional frames per traditionally rendered frame ». Le même texte dit que le nouveau modèle de Frame Generation (une image générée, moins de VRAM) concerne les RTX 50 et les RTX 40. Les RTX 40 ne sont pas dans la phrase qui attribue le multi-frame.
- [NVIDIA App, overrides](https://www.nvidia.com/en-us/geforce/news/nvidia-app-update-dlss-overrides-and-more/) : l’override Multi Frame Generation est « for GeForce RTX 50 Series users » ; le choix 3X / 4X y est décrit pour ce cas. L’upgrade du modèle Frame Generation pour RTX 40 reste le modèle à une image générée.
- [developer.nvidia.com/rtx/dlss](https://developer.nvidia.com/rtx/dlss), page lue pour cette étude : « DLSS Multi Frame Generation … up to five frames per rendered frame » sur « GeForce RTX 50 Series and RTX PRO Blackwell ». C’est plus tard que le 4x du menu (DLSS 4.5 / Dynamic MFG). Ce binaire, chemin Windows, n’accepte que `framegen_multiplier` 2, 3 ou 4 (`cfg.c` ligne 176) et les ramène à `numFramesToGenerateMax`. Le 5x et le 6x du même fichier sont `fg_mult`, chemin NGX, pas cette session Windows.

La 4070 SUPER de ce journal est donc limitée à 2x par ce que Streamline a rapporté (`up to 2x`, ligne 34), et cette limite coïncide avec ce que NVIDIA écrit pour une RTX 40 : une image générée, pas trois. Le code ne contient pas une liste de PCI ids. Si un pilote futur rapportait `numFramesToGenerateMax` supérieur, le menu proposerait 3x ou 4x sans autre changement (`row_framegen_mult`). NVIDIA, dans les pages citées, ne présente pas ce rapport comme quelque chose qu’un pilote débloque sur RTX 40. Affirmer qu’un pilote plus récent que 621.01 donnerait le 3x sur cette carte : hypothèse contredite par ces pages, non testée ici.

FSR dans ce binaire, sur cette carte :

- FSR 3.1 frame generation : une image interpolée, facteur 2 dans `generated_factor`. AMD, note GD-231A sur [la page FSR](https://www.amd.com/en/products/graphics/technologies/fidelityfx/super-resolution.html) : « inserts 1 frame between existing ones which can therefore enable up to 2x the framerate ». [Annonce FSR 3, GPUOpen](https://gpuopen.com/news/fsr3-announce/) : le même « up to 2x ». Pas de 3x ou 4x dans ces textes ni dans `vk_fg.cpp`.
- Le menu peut poser `framegen=fsr3` sur un GPU NVIDIA, là où `amd_fidelityfx_vk.dll` se charge (`nx2navi.ini.example` lignes 499-500 : « on any GPU », « not on Linux »). Cette session ne l’a pas fait. Taux, artefacts et latence de FSR 3.1 FG sur cette 4070 SUPER : non mesuré. Le code ne branche pas Anti-Lag ; AMD écrit que la latence de la génération se compense avec Anti-Lag 2 ([GPUOpen, FSR Frame Generation](https://gpuopen.com/amd-fsr-framegeneration/)). Sans cette brique, le coût de latence ici est non mesuré et n’a pas le réducteur qu’AMD décrit.
- FSR Frame Generation « ML » : la même page GPUOpen la réserve aux Radeon RX 9000, avec repli analytique (l’ancien FSR 3) sur RDNA 2, 3 et 3.5. `vk_fg4` dans ce dépôt dit la même chose et ajoute « has not been tested » pour le chemin RX 9000 (`ini.example` lignes 467-468). Sur la 4070 SUPER le journal refuse déjà FSR 4 upscaler (ligne 11542). Il n’y a pas de FG 3x FSR à activer sur cette carte dans ce code.

Outils externes, ce qu’une page officielle dit, et ce qu’elle ne dit pas.

NVIDIA Smooth Motion. [GeForce News, août 2025](https://www.nvidia.com/en-us/geforce/news/nvidia-app-global-dlss-overrides-rtx-40-series-smooth-motion/) et [l’aide NVIDIA 5621](https://nvidia.custhelp.com/app/answers/detail/a_id/5621) : modèle du pilote qui infère une image entre deux images rendues, pour les jeux sans DLSS Frame Generation, GeForce RTX 40 et RTX 50, jeux DirectX 11, DirectX 12 ou Vulkan compatibles, « typically doubling the perceived frame rate ». Une image ajoutée, donc 2x, pas 3x ou 4x, dans ces textes. Latence en millisecondes : non chiffrée là. HUD : non mentionné. HDR : non mentionné sur les pages lues. Les deux textes disent que c’est pour les titres qui n’ont pas DLSS Frame Generation. L’activer par-dessus le FG déjà branché de NX2navi n’est pas un mode décrit. Le faire : hypothèse, non mesuré, et hors du cadre « titres sans FG » écrit par NVIDIA.

Lossless Scaling (LSFG), page Steam de l’application, développeur THS, lue pour cette étude : LSFG 3, mode Fixed qui « multiply the original frame rate », mode Adaptive vers un taux qui peut ne pas être un multiple. Aucun multiplicateur maximal n’est écrit sur cette page. En inventer un (10x, 20x) serait faux ici. Fenêtré et borderless ; le plein écran exclusif est dit limité. Une seconde GPU peut déporter le calcul et « may reduce latency ». Pas de milliseconde. DirectX 11 est dans la configuration demandée ; Vulkan n’est pas nommé pour l’application Windows. Le port Linux communautaire est renvoyé vers lsfg-vk.dev, ce n’est pas l’application Windows. HUD et HDR : non décrits sur la page lue. Mécanisme, d’après le texte « compatible with most games, even those that don't have built-in frame generation » : l’outil n’est pas intégré au jeu, il n’a donc pas la profondeur ni l’image sans HUD que `vk_dlssg_frame` prépare. Que l’interface soit déformée dans les images générées : hypothèse cohérente avec une interpolation de l’image déjà composée, non mesurée sur NX2navi, et non écrite telle quelle par THS sur la page consultée.

## 5. Le calcul, et ce qu’il faudrait gagner sur le replay

Affiché = images de jeu × multiplicateur, tant que le FG est réellement actif (échantillon Streamline à 2, section 2). Avec un maximum de 2x sur cette carte, 200 affichés demandent 100 img/s de jeu, 300 affichés en demandent 150. Le plafond jeu est 240 (`LIMIT_MAX_HZ`), donc 2x ne dépasse pas 480, et seulement si le jeu tient 240. Kokiri tient 240 de jeu et le FG n’y était pas encore l’échantillon « 2 presented ». La plaine ne tient pas 100.

Budget, à partir des médianes mesurées, pas d’un objectif rond :

- 73,0 img/s → intervalle 13,70 ms. 100 img/s → 10,00 ms. Il manque 3,7 ms sur le chemin critique pour 200 affichés à 2x.
- 150 img/s → 6,67 ms. Il manque 7,0 ms pour 300 affichés, soit presque tout le replay médian du sous-ensemble lourd (7,18 ms).
- La plaine « facile » à 77,5 img/s (intervalle médian mesuré 12,90 ms) est déjà à 2,9 ms des 10,0 ms. 2 × 77,5 = 155 affichés, calcul, si le FG tient sur ces fenêtres. L’échantillon « 2 presented » existe à 3600, 7200 et 10800, qui tombent dans ou juste après cette plage. Ce n’est pas une moyenne.

Le replay de la fenêtre 12240 (8,65 ms) est du même ordre que l’attente GPU « waiting for the next submission » (7,11 ms). Réduire le replay raccourcit l’image si, et seulement si, ce temps est en série sur le fil qui soumet. Avec `replay_async=0`, le code le dit (`vk_backend.c`, journal ligne 357). Ce n’est pas une preuve que chaque milliseconde de replay gagnée devient une milliseconde d’image : d’autres attentes (fence 36 % du fil de rendu sur cette même fenêtre, ligne 7028) restent. Gain d’un changement précis : non mesuré, sauf mention.

Pistes, gain marqué.

`replay_async=1`. Défaut 0, redémarrage. Le fichier d’exemple (lignes 64-67) écrit « 15 to 18 percent more frames per second in a village » et « adds up to a frame of latency », avec parfois une image sombre ou qui clignote parce que l’autre fil relit une mémoire que le jeu a réécrite. Ce 15 à 18 % n’est pas dans le journal de cette session (`replay_async=0`). L’appliquer à 73 img/s donnerait environ 84 à 86 img/s. C’est une règle de trois sur un chiffre qui concerne « a village », pas cette plaine : hypothèse, non mesuré ici. Même le haut de la fourchette reste sous 100 img/s. Le texte du menu (`ui_text.c` ligne 380) dit la même latence d’au plus une image, sans pourcentage.

`one_per_core=1`. Défaut 0. `perf.c` vers 719 : « Off by default until measured here » et « the same rule is reported to give +4-10% on an 8-core SMT CPU ». Le « reported » n’est pas une mesure de ce jeu ni de ce journal. 4 à 10 % de 73 img/s font 76 à 80. Hypothèse arithmétique, non mesuré. Le 7700X a 8 cœurs et 16 fils ; le journal ne le nomme pas et n’a pas journalisé de masque, puisque la clé vaut 0.

`pool_bind_skip`. Pas dans ce source. Sur la branche `cursor/pool-bind-skip-7f0d`, défaut 0, le dessin ne rappelle pas `vkCmdBindDescriptorSets` du set 2 si l’ensemble n’a pas changé. Le commentaire de l’exemple ajouté là dit que le gain sur le fil de replay n’est pas mesuré. Dans le bloc à 66,7 img/s de ce journal (ligne 8045), les binds du replay comptent 1337 ms sur 60 s, soit 2,2 % du temps mur, pour 70 894 921 binds. Ce n’est pas le gain du skip : le skip ne retirerait que les binds répétés inchangés, et cette part n’est pas comptée. Non mesuré.

`split_draws`, `split_us`, `split_in_pass`, `split_idle`. Défauts 64, 1000 µs, 1, et 0. L’exemple dit que `split_idle=1` a fait clignoter le feuillage en Kokiri, donc il reste à 0. Cette session journalise `replay command buffer split every 64 draws` (ligne 439) et, en plaine, souvent `2.00` parts par soumission dont `1.00` splits inside a pass, et `0.00 first parts sent while the GPU was idle`. Le GPU attend la soumission (7,11 ms sur l’exemple) alors que le split anticipé est coupé. L’allumer : le commentaire dit un défaut visuel déjà vu. Gain : non mesuré. Le rallumer par défaut ne serait pas « clairement mieux ».

`submit_batch=4` (déjà le cas). Le passer à 1 envoie chaque soumission tout de suite (`ini.example` lignes 83-84). Effet sur l’attente « waiting for the next submission » : non mesuré.

`barrier_game`. Défaut 1 sur NVIDIA, le commentaire dit environ un dixième du temps GPU en moins. Le GPU de la plaine est à 7-8 ms sur un intervalle de 13-15 ms, sous le seuil 80 % : le temps GPU n’est pas le lien. Un dixième de 8 ms est 0,8 ms de GPU, pas 0,8 ms d’image, tant que `bound` reste `cpu`. Non mesuré sur le taux.

`dlss=performance` ou `ultra_performance`. La scène passerait de 2/3 (1707×960, mesuré) à 1/2 ou 1/3 (`dlss_share`). Le GPU est déjà sous les 80 %. Baisser la définition réduit un temps qui n’est pas le plafond de ces fenêtres. Gain d’images de jeu : non mesuré. Hypothèse : faible sur les fenêtres `bound: cpu`, peut-être visible sur les fenêtres `bound: gpu` (il y en a, par exemple images 20460 et suivantes, GPU 10 à 15 ms, et 30600-30840). Ces fenêtres `gpu` sont des chargements ou le menu, pas le régime de marche. Les utiliser pour choisir le mode DLSS de la marche : non mesuré.

`dyn_res=1` descend d’un cran DLSS quand le GPU dépasse `dyn_res_target` (défaut 16,7 ms). En marche, le GPU est vers 7-8 ms : le mécanisme ne descendrait pas. Non mesuré.

Aucun de ces leviers, avec les seuls chiffres écrits dans le dépôt ou le journal, ne porte 73 img/s de jeu à 150. En porter 73 à 100 demanderait un gain plus grand que le 15-18 % du village cité par l’exemple, ou une addition de gains qui n’a pas été mesurée. Additionner 15 % et 10 % et 2 % comme s’ils se cumulaient serait inventer.

## 6. Latence, et deux fichiers ini

Aucun temps de clic à photon n’est dans le journal. Ce qui suit est le mécanisme, plus un calcul quand le journal donne le taux.

FG 2x avec Reflex. NVIDIA présente le Frame Generation comme associé à Reflex pour garder la manette réactive ([developer.nvidia.com/rtx/dlss](https://developer.nvidia.com/rtx/dlss) : « generate frames while maintaining great responsiveness with NVIDIA Reflex »). Ce dépôt active le mode bas (`reflex` défaut 1) dès que DLSS-G tourne, et c’est ce que le journal confirme. Un chiffre du type « la latence double » ou « elle baisse de N ms » pour ce jeu : absent du journal et des pages citées pour une 4070 SUPER dans Ocarina. La page DLSS 4 donne des latences pour des scènes de jeux précis sur RTX 5090. Les recopier ici serait faux. Non mesuré sur cette session.

FG avec `reflex=0`. Le code l’accepte. Streamline reçoit `eOff`. NVIDIA écrit que le FG s’utilise avec Reflex ; ce dépôt aussi (« DLSS-G needs Reflex », `vk_dlssg.c` ligne 16). De combien la latence monte si on coupe Reflex ici : non mesuré. `reflex=2` (boost) garde les horloges GPU hautes selon le nom du mode Streamline `eLowLatencyWithBoost`. Le plafond de cette session est le CPU. Effet du boost sur le taux ou la latence : non mesuré.

`replay_async=1`. Le code et le texte du menu disent jusqu’à une image de latence en plus. À 73 img/s, une image dure 13,7 ms. C’est le produit `1000/73`, pas un chronomètre. À 240 img/s, une image dure 4,17 ms, le même calcul. Le décalage est « jusqu’à » une image de jeu, parce que le replay a au plus une image de retard (`vk_backend.c` en-tête : « at most one frame behind »).

`mailbox` contre `immediate`. `mailbox` attend une vblank pour montrer l’image, `immediate` la montre sans cette attente et peut déchirer (`nx2navi.ini.example` lignes 15-20 et 354-355 ; le menu appelle ça VSync). À 480,168 Hz, une vblank dure `1000/480,168` = 2,08 ms. C’est la période du moniteur mesuré, pas la latence ajoutée : l’attente réelle va de 0 à une période si le mode attend vraiment la vblank. Avec le FG, Streamline dit que l’intervalle de VSync 1 n’est pas supporté (section 2). Savoir si `immediate` fait disparaître cet avertissement et combien de millisecondes il retire : non mesuré. `fifo` est une valeur acceptée (`cfg.c` ligne 166) que l’interrupteur du menu ne réécrit pas ; il la traite comme VSync allumé.

`fps_limit=display` avec FG 2x ne fait pas attendre le jeu dans la plaine : `pacing` 0 %, taux loin de 240. Le cadenceur et le sommeil Reflex ne retiennent ces images que lorsqu’elles iraient plus vite que le plafond. En Kokiri à 240, le plafond est atteint (`pacing` reste 0 % dans les fenêtres : le 240 y est le coût CPU de 4,17 ms, pas une attente affichée comme pacing). Le sommeil Reflex peut quand même exister sans apparaître dans ce pourcentage. Non mesuré.

### Config « le plus d’images », clés vérifiées, gains non mesurés

À poser dans `nx2navi.ini`, puis redémarrer : `replay_async`, `one_per_core` et `framegen` DLSS sont lus au démarrage. Ne pas cumuler les pourcentages de la section 5.

```ini
[display]
vk_present = mailbox
hdr = native
dlaa = 1
taa = dlss
dlss = quality
dlss_preset = default
framegen = dlss
framegen_multiplier = 2
fps_limit = display

[performance]
replay_async = 1
one_per_core = 1
```

`reflex` omis reste `1`. `framegen_multiplier=3` ou `4` sera ramené à 2 tant que Streamline rapporte 2, avec une ligne `dlssg:` si la valeur dépasse. `dlss=performance` est le levier de définition si une fenêtre devient `bound: gpu` ; sur les fenêtres cpu de cette session, le gain est non mesuré et probablement faible (section 5). `pool_bind_skip` n’existe pas dans ce binaire.

Pourquoi `vk_present` reste `mailbox` dans cette config « taux » : le journal donne le rafraîchissement `480.168 Hz` et ne dit pas si G-Sync ou FreeSync est actif. L’exemple (`nx2navi.ini.example` lignes 20-21) dit qu’un écran à rafraîchissement variable suit les images. Mailbox évite le déchirement. `immediate` est l’essai latence. Hypothèse, appuyée par `pacing` à 0 % sur les fenêtres de marche : mailbox n’est pas le plafond des 73 img/s.

### Config « peu de latence »

Le FG ajoute un mécanisme que NVIDIA associe à Reflex précisément parce qu’il a un coût de latence. Le couper est le seul levier dont le sens est certain sans chronomètre : il n’y a plus d’image générée, donc plus le délai que Reflex est là pour contenir. Le taux affiché redevient le taux de jeu. En plaine, cela rend environ 70 img/s à l’écran au lieu d’environ 140. C’est le calcul 1x, pas une préférence cachée.

```ini
[display]
vk_present = immediate
hdr = native
dlaa = 1
taa = dlss
dlss = quality
framegen = off
fps_limit = display

[performance]
replay_async = 0
one_per_core = 0
```

`framegen=off` demande un redémarrage pour quitter DLSS-G (`framegen_restart`). `vk_present=immediate` peut déchirer. `reflex` n’est plus appelé si DLSS-G n’est pas chargé. Garder le FG et seulement couper le sommeil n’est pas une clé séparée : `reflex=0` laisse la génération et retire le mode bas. Latence qui en résulte : non mesuré. Ce n’est pas la config recommandée ; NVIDIA et le commentaire du code veulent Reflex avec le FG.

Compromis, si les images générées restent voulues et que la latence prime sur le taux : la config du haut avec `replay_async=0` (retire l’image de retard du replay) et `vk_present=immediate` (retire l’attente de vblank, au prix du déchirement). `framegen_multiplier` reste 2. Effet chiffré : non mesuré.

## 7. Pas de changement de code dans cette branche

Le critère était : un changement qui améliore clairement le FG, derrière une clé éteinte par défaut.

Garder le FG allumé quand il se coupe. Dans ce journal il se coupe pour une cause écrite : pas de profondeur (images 12259 et suivantes, erreurs Streamline sur les tags), ou menu ouvert (ligne 11541 puis 11562). Le tenir allumé sans profondeur, c’est ce que font déjà les 12 images de grâce, et Streamline répond en réutilisant les constantes de l’image d’avant (lignes 7048-7052). Allonger cette grâce irait contre le message d’erreur. La raccourcir est le sujet de la branche `cursor/dlssg-flicker-hold-hudless-8da9`, qui coupe dès que la profondeur ou les vecteurs manquent. Cette branche désactive aussi la génération en HDR `native`, parce que l’image sans HUD n’est pas dans les couleurs du swapchain : le jeu encode son PQ après avoir dessiné l’interface. L’ini de cette session est `hdr=native`. Porter cette branche telle quelle éteindrait le FG sur la config mesurée. Ce n’est pas un gain clair. La laisser derrière une clé, sans mesure d’image, ajouterait un chemin non essayé sur la seule session qu’on a. Non fait.

Passer l’image sans HUD à Streamline. Le chemin Windows la laisse à `NULL` (section 2). FSR, lui, la passe. L’effet sur les artefacts d’interface : non mesuré dans ce journal (aucune ligne ne décrit l’image). Le faire par défaut en `hdr=native` est exactement le cas que l’autre branche refuse. Non fait.

Reflex boost par défaut, ou un autre pacing. Le mode 1 est déjà celui du journal. Le boost sur un plafond CPU : non mesuré, et rien dans le journal ne montre des horloges GPU basses. `reflex=2` est déjà la clé, sans recompilation.

`pool_bind_skip` est déjà derrière une clé éteinte, sur une autre branche, gain non mesuré. Le recopier ici doublerait cette branche.

Aucun autre levier de la section 5 n’a un gain mesuré sur cette plaine qui justifierait de changer le défaut.

## 8. Dix lignes, puis le plan de test

1. Sur cette 4070 SUPER, Streamline a dit « up to 2x » et NVIDIA réserve le 3x/4x aux RTX 50 : 200 à 300 affichés demandent 100 à 150 img/s de jeu.
2. La plaine mesurée tourne vers 68-78 img/s de jeu selon la tranche, `bound: cpu`, replay souvent 6 à 9 ms, GPU vers 7-8 ms qui attend la soumission.
3. À 2x, tant que l’échantillon Streamline vaut 2, cela fait un calcul d’environ 135-155 affichés, pas 200. Le journal n’a pas de compteur d’écran moyen.
4. Kokiri posée est à 240 img/s de jeu, replay sous 0,6 ms. Le « 480 on screen » du journal est le plafond du cadenceur, pas une mesure d’affichage pendant la marche.
5. `replay_async=1` est le seul levier que le dépôt chiffre (15 à 18 % dans un village, jusqu’à une image de latence). Sur 73 img/s cela resterait sous 100, et cette plaine ne l’a pas mesuré.
6. `one_per_core=1` et `pool_bind_skip` (autre branche, défaut 0) sont non mesurés. Les additionner au 15 % inventerait un cumul.
7. FSR 3 dans ce code est aussi du 2x, sans Anti-Lag, non mesuré ici. Smooth Motion, chez NVIDIA, est une image intercalée pour les jeux sans DLSS FG.
8. LSFG, sur la page Steam lue, multiplie l’image déjà composée, sans facteur maximal publié, sans milliseconde de latence, sans mot sur Vulkan ni le HDR.
9. Le FG de la session se coupe sur le menu, le film à 60, et les images sans profondeur. Pendant 12 images il reste demandé, et Streamline réutilise l’ancienne constante.
10. Aucun code n’a été changé : couper cette grâce ou passer l’image sans HUD n’a pas de gain de taux mesuré, et une autre branche éteint le FG quand `hdr=native`.

Plan de test, une variable à la fois, même sauvegarde, même boucle en plaine, `trace=perf` et `replay_time=1`, 60 s après la ligne `settled` de Hyrule, menu fermé. 1) ini actuel : médiane des `perf: frame` jusqu’au `menu: open`, et la ligne `dlssg:` `presented per frame`. 2) seulement `replay_async=1`, et noter une image noire ou instable s’il y en a une. 3) remettre 0, seulement `one_per_core=1`. 4) si la branche pool-bind est compilée à part, seulement `pool_bind_skip=1`. 5) `dlss=performance` seul, pour voir si `bound` quitte `cpu`. 6) `vk_present=immediate` seul, pour l’avertissement VSync et le déchirement, pas pour le taux. Un chargement ou le menu ouvert ne compte pas comme la marche.
