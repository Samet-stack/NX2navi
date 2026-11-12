# Construire un ocarina.exe optimisé (Windows)

Ce guide est pour le PC de Malayta : Ryzen 7 7700X, RTX 4070 SUPER, jeu 1.1.1, dépôt cloné depuis la branche qui
contient ce fichier. L’exe jouable est le code du jeu relevé depuis **ton** dump, lié au runtime. Le dépôt ne contient
pas le dump, et il ne doit pas le contenir. Aucun chiffre de ce fichier n’est un gain mesuré sur cet exe : tout ce qui
n’a pas été lu dans un log est marqué **non mesuré**.

Les mesures déjà faites sont dans [MEASURE.md](MEASURE.md). En plaine d’Hyrule, image 5100 du log de 23:55, l’exe
0.8.0 fait **75,2 images/s**, replay **6,61 ms**, GPU **6,81 ms**, `bound: cpu`, et le GPU attend la soumission
suivante **6,07 ms**. Sur 60 s (lignes 3420-3421 de ce log, citées par la PR 11) les draws sont 14 163 ms des
15 655 ms d’ops chronométrées, et le fil de replay est sur le CPU 18 078 ms sur 24 101 ms (75 %). La cible 100-150
images/s de jeu n’est pas mesurée : c’est la cible.

Le build par défaut de cette branche n’est pas changé. `--fast` et `--pgo` sont optionnels.

## 1. Ce que le build compile aujourd’hui

Deux chemins. Celui qui produit `D:\Games\NX2navi\ocarina.exe` est `tools/make_game.py` (le launcher l’appelle aussi).
`tools/build_game.py` est le build de développement, pour les versions qui ont un fichier `tools/data/expected-*.json`
(1.1.1 n’en a pas : il n’y a pas de `expected-v1.1.1.json`).

### Chemin du launcher, `tools/make_game.py` `runtime_cmd`

Sans `--fast` ni `--pgo` :

| | Code relevé (fichiers `main_`, `sdk_`, `subsdk0_`) | Runtime (`runtime/`) |
|---|---|---|
| Optimisation | `-O1` | `-O2` |
| Processeur | `-march=x86-64-v3` sur un 7700X (`cpu_level` : AVX2, FMA, BMI2). `-march=x86-64-v2` si le CPU n’a pas AVX2. Rien si le niveau n’a pas pu être lu | **aucun** `-march`. Le runtime précompilé d’une release doit tourner sur tous les PC. Ses vecteurs font 16 octets |
| Autre | `-fno-strict-aliasing`, `-w` | pareil, plus les includes |
| LTO | non | non |
| `-fno-plt` | non | non |
| `-g` | non | non |
| Asserts | pas de `-DNDEBUG`. Le runtime n’appelle pas `assert()` dans son propre code (seulement imgui). Les contrôles du runtime sont `rt_fatal`, ils restent | pareil |
| Traces | `trace` et `replay_time` sont des clés ini, lues au démarrage. Elles ne sont pas compilées à part | pareil |
| Pointeurs de cadre | clang les omet dès `-O1` sur x86-64. Le build par défaut ne passe pas `-fno-omit-frame-pointer` | pareil |
| Lien | `lld` s’il est là, pile de 16 Mio, carte `--Map` | pareil |

Le commentaire dans `runtime_cmd` dit pourquoi le code relevé reste à `-O1` : sa taille rend `-O2` long à compiler.
Un clang CLANG64 (le « fast clang » du launcher, compilé lui-même avec PGO) peut compiler le code relevé. Il écrit les
mêmes objets que le clang UCRT64 pour les mêmes flags. Ce n’est pas le profil `--fast` de ce guide : il accélère la
compilation, pas le jeu. **non mesuré** ici ; le commentaire du launcher parle d’environ un cinquième de temps de
compilation en moins.

### Chemin de développement, `tools/build_game.py`

| | Code généré | Runtime |
|---|---|---|
| Optimisation | `-O1` (`GEN_FLAGS`) | `-O2 -g` (`RT_FLAGS`) |
| Processeur | `-march=x86-64-v3` par défaut, `-march=x86-64-v2` avec `--cpu v2` | le même `-march`, plus `-DRT_CPU_LEVEL` |
| Autre | `-fno-strict-aliasing`, `-fvisibility=hidden` | `-Wall`, `-fno-strict-aliasing`, `-D_WIN32_WINNT=0x0A00` |
| LTO, `-fno-plt`, `-DNDEBUG` | non | non |
| Lien | `lld`, pile de 16 Mio | pareil |

`--pgo` sans `--fast` passe le code généré à `-O2` (le niveau où le profil inline et range le code chaud). Le runtime
reste à `-O2`.

## 2. Le profil `--fast`

Il ne s’applique que si tu passes `--fast`. Flags ajoutés ou remplacés, des deux côtés (code relevé **et** runtime) :

- `-O3` à la place de `-O1` / `-O2`
- `-flto=thin` à la compilation et au lien (pas `-flto` complet : le code relevé est gros, le LTO complet n’est pas ce
  profil)
- `-fno-plt`
- `-fomit-frame-pointer` (déjà le défaut de clang à `-O1` et au-dessus ; le flag le dit)
- `-DNDEBUG` (coupe `assert()`, pas `rt_fatal`, pas les traces `trace=perf` / `replay_time`)
- `-march` : le niveau du CPU (`x86-64-v3` sur le 7700X) **aussi sur le runtime**, parce que le replay est du code du
  runtime. `--march znver4` le remplace par `-march=znver4` (Zen 4, AVX-512 compris). `--march v4` est
  `-march=x86-64-v4` (AVX-512, sans le réglage propre à Zen 4)

Ce que `--fast` ne fait pas :

- Il ne change pas le launcher. Un « Rebuild » du launcher recompile **sans** `--fast` et remplace l’exe.
- Il n’utilise pas le clang CLANG64. Le LTO mince est lié par le clang qui a produit les objets ; un second clang
  casserait ça. La compilation est donc celle d’UCRT64. Le temps en plus est **non mesuré**.
- Il ne réutilise pas `libnx2navirt.a`. Le runtime est recompilé. Le log doit dire `the runtime is compiled with this
  profile`, pas `the runtime comes prebuilt`.
- Il ne réutilise pas les objets `-O1` / `-O2` déjà dans le cache. La clé de l’étape `compile` porte le suffixe
  `fast` (et `znver4` si tu l’as demandé), donc une compile par défaut déjà faite n’est pas sautée.
- Il ne coupe pas `trace=perf`. Le protocole A/B a besoin des mêmes lignes `perf:`.
- Sur Windows, les appels vers `vulkan-1.dll` passent par la table d’imports du PE. Il n’y a pas de PLT comme sous
  ELF. `-fno-plt` est passé quand même ; son effet sur cet exe est **non mesuré**, et il peut être nul.
- Le nombre de compilations en parallèle est divisé par deux si tu ne passes pas `--jobs`. C’est un garde-fou, pas un
  pic mesuré : `-O3` d’un gros fichier relevé dépasse le budget de 768 Mio qui a été choisi pour `-O1`. Si le
  compilateur est tué, relance avec `--jobs 4` (ou moins).

Le premier build de cette branche relance aussi la préparation des shaders : la clé de l’étape `compile` contient
l’empreinte de `make_game.py`, et l’étape shaders suit cette clé. Le temps de cette préparation est **non mesuré**
ici. Le cache de shaders déjà à côté de l’exe 0.8.0 peut être repris par le jeu si le traducteur n’a pas changé
(`shader carry`, activé tant que tu ne mets pas `shader_carry = 0`).

## 3. Garder l’exe 0.8.0

Ferme le jeu. Dans `D:\Games\NX2navi` :

```bat
copy /Y ocarina.exe ocarina-0.8.0.exe
```

`ocarina-0.8.0.exe` est la sauvegarde. MEASURE.md l’appelle `ocarina-stock.exe` : c’est le même geste, avec le numéro
de version dans le nom. Ne lance pas le launcher pour « mettre à jour » pendant l’essai : le journal du 11/11 dit
qu’il réinstalle le 0.8.0 (`finished in 26.4 min`) et il recompilerait **sans** `--fast`.

Pour revenir à l’exe 0.8.0 sans recompiler :

```bat
copy /Y ocarina-0.8.0.exe ocarina.exe
```

Les sauvegardes sont dans `D:\Games\NX2navi\profile`. Copie ce dossier ailleurs avant une longue session si tu veux
pouvoir le remettre. Le jeu l’écrit tout seul.

Un lien qui échoue ne remplace pas `ocarina.exe` : `make_game.py` écrit `ocarina.exe.new` et ne le renomme qu’une
fois le lien fini. Un jeu encore ouvert refuse le remplacement (`ocarina.exe is in use`).

## 4. Compiler le profil fast

Toolchain du launcher, comme MEASURE.md section 9. Le dépôt de cette branche :

```bat
git clone --branch cursor/fast-build-290d https://github.com/Samet-stack/NX2navi.git D:\src\NX2navi-fast
cd /d D:\src\NX2navi-fast
py -I tools\make_game.py --rebuild --fast --march znver4 --out D:\Games\NX2navi --msys "%LOCALAPPDATA%\Programs\NX2navi\toolchain\msys64\ucrt64"
```

`--march znver4` est pour ce 7700X. Sans `--march`, le profil prend le niveau détecté, `x86-64-v3` sur cette machine
(AVX2, pas le réglage AVX-512 de `znver4`). L’exe `znver4` ne tourne pas sur un CPU sans AVX-512. Le script refuse
`--march znver4` si la sonde du processeur dit qu’AVX-512 n’est pas là. L’autotest au lancement (`runtime/main.c`
`check_cpu`) vérifie AVX2, pas AVX-512 : un exe `znver4` copié sur un autre CPU peut passer cet autotest puis planter
sur une instruction AVX-512.

Si `--rebuild` refuse parce que l’exefs n’est pas dans le dossier de sortie :

```bat
py -I tools\make_game.py --fast --march znver4 --exefs D:\Games\NX2navi\exefs --romfs D:\Games\NX2navi\romfs --out D:\Games\NX2navi --msys "%LOCALAPPDATA%\Programs\NX2navi\toolchain\msys64\ucrt64"
```

Dans le log de build tu dois voir une ligne `profile: -O3 -flto=thin ... march -march=znver4`. Tu ne dois pas voir
`the runtime comes prebuilt`.

Même profil sur le build de développement (pas le chemin 1.1.1 ; il lui faut un `generated/<version>` et un fichier
expected) :

```bat
py -I tools\build_game.py --version v121 --fast --march znver4 --out build\v121-fast
```

## 5. PGO : faisabilité et commandes

Le PGO existe déjà. `docs/building.md` mesure, sur **1.4.3**, un banc de combat 12 à 15 % plus rapide par image et un
combat hors de l’entraînement environ 6 %. Ce n’est pas la plaine d’Hyrule, et ce n’est pas cet exe. Gain en plaine :
**non mesuré**.

### Ce qui bloque sur le clang du launcher

Le clang MSYS2 UCRT64 n’a pas le runtime de profil (`clang_rt.profile`). `--pgo gen` avec ce clang s’arrête tout de
suite, avant le relevé, avec cette raison. Il faut llvm-mingw **20261020**, le même que `tools/make_release.py`
(`LLVM_VERSION`), dont l’archive et l’empreinte sont :

- URL : `https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-x86_64.zip`
- sha256 : `fd5462b7f5d93b2ff981fcd3f9c7e427e47f3203c64ea91c27bcc444eebd74cd`

Dézippe-le. Le dossier à passer est celui qui contient `bin\clang.exe`
(`llvm-mingw-20261020-ucrt-x86_64`).

`--pgo FICHIER` (utiliser un profil déjà fusionné) n’a pas besoin de ce runtime. Le clang UCRT64 suffit. `--fast` peut
s’y ajouter : le code reste à `-O3`, le profil ne le rabaisse pas à `-O2`.

Un profil ne vaut que pour le C généré qui l’a entraîné. Les fonctions sont appariées par nom et par empreinte du
flot de contrôle. Un changement dans `recomp/` fait perdre les comptes sans avertissement bruyant (les warnings sont
coupés, comme dans `build_game.py`). Fusionne le profil tout de suite après l’entraînement, sur le même commit.
`tools/pgo.py` écrit à côté un `.json` avec l’arbre git de `recomp/`.

### Entraînement en plaine, exe 1.1.1

À lancer **après** la sauvegarde de l’exe 0.8.0. L’exe instrumenté est plus lent qu’un build normal. Ce n’est pas
l’exe que tu compares en A/B : il sert à écrire des comptes.

```bat
cd /d D:\src\NX2navi-fast
py -I tools\make_game.py --rebuild --pgo gen --toolchain D:\src\llvm-mingw-20261020-ucrt-x86_64 --out D:\Games\NX2navi --msys "%LOCALAPPDATA%\Programs\NX2navi\toolchain\msys64\ucrt64"
```

Le script compile avec le clang d’llvm-mingw et `--sysroot` pointé sur l’UCRT64 du launcher, pour garder les en-têtes
et les bibliothèques MSYS2. Une sonde compile et lie un programme vide avec `-fprofile-generate` **avant** le long
build. Si elle échoue, le runtime de profil n’est pas là : ne lance pas la suite.

Joue. Même sauvegarde, même point de plaine que MEASURE.md section 8 (`HyruleField_Child settled`), plus d’autres
lieux si tu peux (le profil ne compte que ce qui a tourné ; le reste est compilé pour la taille). Vingt à trente
minutes, comme `docs/building.md`. Quitte par le menu : `runtime/platform/win32.c` `plat_exit` appelle
`__llvm_profile_dump` parce que `ExitProcess` saute le `atexit` du runtime de profil. Un fil écrit aussi les comptes
chaque minute (`pgo_flush_thread`) dans `D:\Games\NX2navi\build-cache\pgo\*.profraw`.

Puis, avec le `llvm-profdata` **du même** llvm-mingw :

```bat
py -I tools\pgo.py merge D:\Games\NX2navi\build-cache --version v111 --toolchain D:\src\llvm-mingw-20261020-ucrt-x86_64 --out D:\src\NX2navi-fast\build\pgo\pgo-v111.profdata
```

`v111` est une étiquette dans le `.json`. Ce n’est pas une version que `build_game.py` sait construire : 1.1.1 passe
par `make_game.py`.

L’exe optimisé, profil + fast, de nouveau avec le clang UCRT64 :

```bat
py -I tools\make_game.py --rebuild --fast --march znver4 --pgo D:\src\NX2navi-fast\build\pgo\pgo-v111.profdata --out D:\Games\NX2navi --msys "%LOCALAPPDATA%\Programs\NX2navi\toolchain\msys64\ucrt64"
```

Variante plus légère, si le lien instrumenté **avec** `--fast` (LTO mince + instrumentation) manque de mémoire. Ce
cas n’est pas mesuré. Entraîne sans `--fast` (la commande `gen` plus haut le fait : le code relevé passe à `-O2`, pas
de LTO), fusionne, puis utilise le profil avec `--fast`. Les comptes correspondent moins bien au binaire final, parce
que l’inlining n’est pas le même. C’est quand même un profil valide : les fonctions qui ne correspondent plus sont
compilées sans comptes.

Le chemin `build_game.py --pgo` de `docs/building.md` reste celui des versions 1.2.1 et 1.4.3. Il ne construit pas
1.1.1.

## 6. Pourquoi le replay n’est pas parallélisé

Le goulot mesuré est l’enregistrement, sur un seul fil, du flux NVN vers un command buffer Vulkan primaire.
`runtime/vk_exec.c` `walk` avance opcode par opcode et modifie un seul état (`VkExec`). Un `OP_WAIT_EVENT` peut
arrêter la promenade au milieu d’une soumission et la reprendre plus tard. Les barrières, les descripteurs, la passe
en cours et le miroir mémoire sont ordonnés avec les draws.

Trois idées, et pourquoi aucune n’est dans ce changement :

- **Command buffers secondaires par passe, un fil par passe.** Un pool de commandes Vulkan n’est pas utilisable depuis
  deux fils. Il faudrait un pool par fil, découper le flux à des frontières de passe, et figer l’état hérité. Le code
  a déjà écarté les secondaires pour les draws de mouvement par objet : les enregistrer dans un secondaire faisait
  faire au pilote, sur le fil de replay, le travail de chaque draw (`runtime/vk_exec_draw.c`, le commentaire au-dessus
  de `MoDraw`). Ce fichier est modifié par un autre changement ; ce guide n’y touche pas. Un prototype ici pourrait
  montrer une image fausse (événement pris trop tôt, barrière manquante, descripteur d’une passe précédente). Il n’est
  pas implémenté.
- **Un pool de workers.** Même obstacle : l’état est une seule structure, écrite dans l’ordre. Le gain serait **non
  mesuré** et le risque est une image cassée. Pas de clé ini, pas de prototype.
- **Enchaîner enregistrement et soumission au-delà de `replay_async`.** `replay_async` (défaut 0) met déjà le replay
  sur un autre fil, au plus une image derrière (`runtime/vk_backend.c` `vk_replay_async`). Le GPU qui attend 6,07 ms
  attend la fin de l’enregistrement (6,61 ms), pas un second étage de file. Un étage de plus ajouterait de la latence
  sans raccourcir la promenade. Les risques déjà écrits pour `replay_async` restent : jusqu’à une image de latence,
  images sombres ou qui clignotent, carrés dans les rayons.

`replay_async = 1` est donc l’essai de pipelining à faire en premier, sur l’exe, sans nouveau code. Gain en plaine :
**non mesuré**.

## 7. Runtime hors des draws

Rien de nouveau n’est branché sur une clé ini. Les leviers déjà dans l’exe, et ce que le log de 23:55 permet d’en
dire :

- **Allocations.** `vk_exec_record` alloue la liste de vues d’une soumission et la libère au fence, sur un autre fil.
  Le chemin des événements alloue aussi un tableau de handles. Ce n’est pas le gros du 6,61 ms : les draws dominent
  les ops chronométrées (section 1). Un pool d’allocations n’est pas ajouté. Gain : **non mesuré**, et le mécanisme
  ne vise pas la part mesurée.
- **Verrous.** `LOCK_TIME` (défaut 0) compte déjà les prises du verrou de textures de plus d’1 ms. Le log de 23:55
  ne l’avait pas. On ne sait pas si un verrou est le quart du temps de replay qui n’est pas du CPU (18 078 / 24 101 ms
  sur 60 s). Pas de nouveau verrou, pas de verrou retiré.
- **Timer 1 ms.** `timer_1ms` vaut **1 par défaut** (`runtime/main.c` appelle `timeBeginPeriod(1)`). Le remettre à 0
  est un A/B déjà possible. Le défaut n’est pas changé : le jeu installé s’en sert. Sur une image limitée par 6,61 ms
  de replay CPU, la résolution du timer n’est pas ce que le log désigne. Effet de `timer_1ms = 0` en plaine : **non
  mesuré**.
- **Priorité et affinité.** Déjà en place, et déjà le défaut dès 12 processeurs logiques. `recorder_prio = 1` met le
  fil qui enregistre à la priorité la plus haute. `recorder_core = auto` lui réserve le cœur d’indice le plus haut de
  la classe la plus rapide, avec ses deux processeurs logiques (`runtime/platform/win32.c` `plat_cpu_reserve`). Sur
  un 7700X (16 fils, une seule classe), c’est le dernier cœur. Tu indiques les logiques 14 et 15 : la ligne de log
  `vk: the recording thread has a core of its own` nomme les numéros réels. Les ensembles de CPU Windows sont souples
  pour les *autres* fils (ils peuvent utiliser ce cœur s’il est libre). Le fil d’enregistrement, lui, a ce cœur comme
  ensemble sélectionné. Un masque d’affinité dur en plus ne raccourcit pas les 6,61 ms de travail sur ce fil. Il n’est
  pas ajouté.
- **Copies du miroir.** Le « merge ~28 Mio » du log (image 5100 : **28,294 Mio**, 44 dispatches) est le shader
  `wb_merge.comp`, sur le GPU : il réécrit dans la mémoire importée les mots de 32 bits qui diffèrent entre le miroir
  et l’instantané (`runtime/vk_memory.c`, section write-back merge). Le CPU de ce suivi est **0,431 ms**, dont
  **0,051 ms** de syncs. Les flush ne copient presque rien : **46,717 Mio** « flushed and not copied ». Couper le
  merge (`wb_merge = 0`) revient aux copies entières sur PCIe ; le commentaire du code dit que c’est le chemin le plus
  lent, pas le plus rapide (mesures d’un autre PC, pas de ce log). Rien n’est changé. Un gain CPU sur ces 0,431 ms
  est **non mesuré** et ne couvre pas les 6,61 ms.

## 8. Protocole A/B

Même règles que MEASURE.md section 8. Une seule chose change entre A et B. Ordre A, B, B, A. Même sauvegarde, même
point (`HyruleField_Child settled` dans le log), 20 s après le chargement, 60 s du même chemin. `trace = perf` et
`replay_time = 1` restent. Note `replay avg`, le fps de `perf: frame`, et `bound:`. Pas d’image noire, pas de texture
manquante, pas de carrés dans la lumière. `tools/ab_summary.py` sert quand la clé vaut `ab` et que `ab = 20`.

Garde `ocarina-0.8.0.exe` (section 3). Pour comparer un **exe**, A est l’exe 0.8.0, B est l’exe `--fast`. Remets
l’ini identique (les clés de la section 7 à leur défaut, donc `replay_async = 0`). Un seul exe change.

Pour comparer une **clé** sur le nouvel exe, l’exe reste celui du profil fast et une seule ligne de l’ini change.

Ordre suggéré, une chose à la fois :

1. Exe 0.8.0 contre exe `--fast --march znver4`, même ini. C’est le couple principal. Gain : **non mesuré** tant que
   ce log n’existe pas.
2. Sur l’exe fast : `replay_async = 1` contre 0. Si l’image casse, remets 0.
3. `one_per_core = ab` et `ab = 20`. Le 7700X a 8 cœurs. Gain : **non mesuré**.
4. `dlaa_objmv = 0` contre 1. Le log de 23:55 n’a aucune ligne `per-object motion`. Les personnages qui bougent
   peuvent laisser une traînée.
5. `pool_bind_skip = ab` seul, puis `pool_mid_skip = ab` seul (les deux sont dans cette branche, défaut 0).
   `pool_mid_skip` peut laisser une texture absente jusqu’à la soumission suivante.
6. `timer_1ms = 0` contre 1 (le défaut est 1).
7. Seulement après un entraînement : le même `--fast --march znver4` avec et sans `--pgo`. Gain : **non mesuré**.

`py -I tools\perf_summary.py D:\Games\NX2navi\logs\nx2navi.log 1 999999` résume les lignes `perf: frame`. Copie le log
avant de relancer : il est poussé vers `nx2navi.1.log`.

## 9. Risques

- **Image ou plantage.** `-O3` et le LTO mince réordonnent plus que `-O1`. Le code relevé est compilé avec
  `-fno-strict-aliasing` parce qu’il relit la mémoire invitée à travers plusieurs types. Un bug qui ne sortait pas à
  `-O1` peut sortir à `-O3`. Si le jeu plante au démarrage ou si l’image est fausse, reviens à `ocarina-0.8.0.exe` et
  note-le. Ce n’est pas un gain.
- **Exe `znver4`.** Il ne tourne que sur un CPU qui a les instructions de ce `-march` (AVX-512 compris). Ne le copie
  pas sur une autre machine.
- **Mémoire à la compilation.** Le garde-fou divise par deux le nombre de compilations. Ce n’est pas un pic mesuré.
  Un manque de mémoire se voit par un compilateur tué ; relance avec `--jobs` plus petit.
- **Temps de build.** `-O3` et le LTO mince sont plus longs que `-O1`. La durée est **non mesurée**.
- **Shaders.** Le premier build de la branche peut relancer la préparation. Le jeu reste jouable ; les premiers
  chemins peuvent saccader tant que le cache du pilote est froid (MEASURE.md et `docs/building.md`).
- **Launcher.** Il peut remplacer l’exe fast par un build par défaut. Garde `ocarina-0.8.0.exe`, et ne lance pas une
  mise à jour du launcher au milieu de l’A/B.
- **PGO.** Un profil d’un autre commit de `recomp/` est appliqué en silence aux fonctions qui ne correspondent plus.
  Vérifie le `.json` à côté du `.profdata`.
- **`replay_async`.** Latence d’une image, images sombres, carrés. Défaut 0. Ce n’est pas activé par `--fast`.
