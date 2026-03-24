# ESP32 Roller Shutter Controller

Système complet de contrôle de volet roulant sur ESP32 combinant contrôle d'accès Wiegand (clavier, badges RFID, empreintes digitales), double relais avec suivi de position, interrupteurs muraux, interface web temps réel, MQTT et mises à jour OTA.

---

## Sommaire

1. [Matériel & environnement de build](#1-matériel--environnement-de-build)
2. [Broches GPIO](#2-broches-gpio)
3. [Démarrage & configuration WiFi](#3-démarrage--configuration-wifi)
4. [Contrôle des relais](#4-contrôle-des-relais)
5. [Suivi de position du volet](#5-suivi-de-position-du-volet)
6. [Interrupteurs muraux](#6-interrupteurs-muraux)
7. [Lecteur Wiegand & contrôle d'accès](#7-lecteur-wiegand--contrôle-daccès)
8. [Modes d'authentification](#8-modes-dauthentification)
9. [Mode apprentissage](#9-mode-apprentissage)
10. [Gestion des codes d'accès](#10-gestion-des-codes-daccès)
11. [Journal d'accès](#11-journal-daccès)
12. [Interface web & API REST](#12-interface-web--api-rest)
13. [WebSocket & logs temps réel](#13-websocket--logs-temps-réel)
14. [Intégration MQTT](#14-intégration-mqtt)
15. [Persistance NVS (flash)](#15-persistance-nvs-flash)
16. [Mise à jour firmware OTA](#16-mise-à-jour-firmware-ota)
17. [Sécurité](#17-sécurité)
18. [Installation pas à pas](#18-installation-pas-à-pas)
19. [Structure du projet](#19-structure-du-projet)
20. [Dépannage](#20-dépannage)

---

## 1. Matériel & environnement de build

### Plateforme cible

| Paramètre | Valeur |
|---|---|
| MCU | ESP32 (Freenove WROVER, LilyGO T-Relay ou compatible) |
| Framework | Arduino via PlatformIO |
| Port upload par défaut | `COM7` |
| Vitesse moniteur série | 115 200 baud |
| Niveau de debug | `CORE_DEBUG_LEVEL=3` |

### Bibliothèques utilisées

| Bibliothèque | Rôle |
|---|---|
| `monkeyboard/Wiegand-Protocol-Library` | Décodage des trames Wiegand D0/D1 |
| `me-no-dev/ESPAsyncWebServer` | Serveur HTTP asynchrone + WebSocket |
| `me-no-dev/AsyncTCP` | Couche TCP asynchrone |
| `bblanchon/ArduinoJson ^7.2` | Sérialisation/désérialisation JSON |
| `knolleary/PubSubClient ^2.8` | Client MQTT |
| `tzapu/WiFiManager` | Portail captif pour configuration WiFi |
| `ayushsharma82/ElegantOTA` | Mise à jour firmware OTA via navigateur |

---

## 2. Broches GPIO

Toutes les broches sont configurables via l'interface web (onglet **Pins**) sans recompilation. Les valeurs par défaut sont :

| Signal | GPIO par défaut | Direction |
|---|---|---|
| Wiegand D0 | 36 | Entrée |
| Wiegand D1 | 39 | Entrée |
| Relais OUVERTURE | 21 | Sortie |
| Relais FERMETURE | 29 | Sortie |
| Barrière photoélectrique | 13 | Entrée pull-up |
| LED de statut | 2 | Sortie |
| LED lecteur rouge (refus) | 14 | Sortie |
| LED lecteur verte (accès) | 12 | Sortie |
| Interrupteur monter | 32 | Entrée pull-up |
| Interrupteur descendre | 33 | Entrée pull-up |
| Bouton BOOT (reset WiFi) | 0 | Entrée (fixe) |

Les broches sont validées côté serveur (plage 0–39). Tout changement est persisté en NVS et nécessite un redémarrage.

---

## 3. Démarrage & configuration WiFi

### Séquence de démarrage

1. Init série 115 200 baud + init gestionnaire de logs
2. Ouverture du namespace NVS `"roller"` — chargement des broches, de la config et de la dernière position connue du volet
3. Configuration des 10 GPIO (entrées pull-up / sorties LOW)
4. Détection du **triple appui sur le bouton BOOT** (dans les 10 premières secondes) : 3 appuis ? effacement des identifiants WiFi + redémarrage
5. Application de l'IP statique si activée
6. `wifiManager.autoConnect("ESP32-Roller-Setup")` : portail captif si aucun réseau connu (timeout portail 180 s, timeout connexion 30 s, 3 essais) ? redémarrage si échec
7. Initialisation Wiegand, chargement des codes, démarrage serveur web, connexion MQTT
8. Triple clignotement LED de statut = système opérationnel

### Portail captif WiFi

- Point d'accès `ESP32-Roller-Setup` automatiquement créé au premier démarrage
- Page de configuration accessible à `192.168.4.1`
- Une fois le réseau configuré, les identifiants sont persistés par WiFiManager

### IP statique

Si activée dans la configuration, l'ESP32 applique les paramètres `staticIP`, `staticGateway` et `staticSubnet` avant la connexion WiFi.

### Watchdog WiFi

Toutes les 30 secondes dans la boucle principale, si le WiFi est déconnecté : `WiFi.reconnect()` automatique.

---

## 4. Contrôle des relais

### Activation

1. Les deux relais sont forcés à LOW (verrou hardware)
2. Pause 100 ms
3. Vérification logicielle que le relais opposé est bien LOW
4. Si un relais était déjà actif : désactivation + 100 ms supplémentaires
5. Activation du relais correct, snapshot de la position de départ, enregistrement du timestamp de départ
6. Publication MQTT `roller/relay` : `{"action":"open"/"close","duration":N}`

### Désactivation

1. Calcul et persistance de la nouvelle position en NVS
2. Les deux relais forcés à LOW
3. Nettoyage des flags `relayActive` et `manualRelayActive`
4. Publication MQTT `roller/relay` : `{"action":"stopped"}`

### Auto-stop temporisé

Lorsque le relais est activé en mode temporisé (via web, MQTT ou authentification Wiegand), il se désactive automatiquement après `relayDuration` ms (5 000 ms par défaut, modifiable dans la configuration).

### Arrêt d'urgence barrière photoélectrique

Si la barrière est activée et que son GPIO passe à LOW pendant qu'un relais est actif : désactivation immédiate + publication MQTT `{"event":"barrier_triggered"}`.

### Interverrouillage hardware

Les deux relais ne peuvent jamais être actifs simultanément. La séquence impose une vérification logicielle et une temporisation avant toute commutation.

---

## 5. Suivi de position du volet

### Paramètres de durée

| Paramètre | Valeur par défaut | Description |
|---|---|---|
| `fullOpenDuration` | 20 000 ms | Durée de course complète 0 % ? 100 % |
| `fullCloseDuration` | 20 000 ms | Durée de course complète 100 % ? 0 % |

Les deux valeurs sont indépendantes et configurables via l'interface web.

### Calcul de position à l'arrêt

Position calculée et persistée en NVS à chaque arrêt de relais, bornée entre 0 et 100 %.

### Position live pendant le mouvement

L'endpoint `/api/status` interpole la position en temps réel pendant le mouvement. La barre de progression de l'interface web se met à jour sans attendre l'arrêt.

### Position inconnue

Après un redémarrage sans position préalablement enregistrée, `shutterKnown = false`. Dans ce cas, l'authentification déclenche systématiquement une ouverture (comportement sécurisé par défaut).

### Logique d'action automatique post-authentification

| Situation | Action déclenchée |
|---|---|
| Position inconnue | Ouverture |
| Position >= 50 % | Fermeture |
| Position < 50 % | Ouverture |

---

## 6. Interrupteurs muraux

**Comportement : maintien = relais actif, relâchement = relais coupé** (pas d'auto-stop temporisé).

- Antirebond 50 ms sur les deux broches
- La position de départ est snapshotée à l'activation, la position est calculée au relâchement
- **Conflits** : si les deux interrupteurs sont pressés simultanément, les deux relais sont immédiatement coupés

### Publications MQTT

| Événement | Topic | Payload |
|---|---|---|
| Activation | `roller/relay` | `{"action":"open"/"close","source":"switch"}` |
| Relâchement | `roller/relay` | `{"action":"stopped"}` |

---

## 7. Lecteur Wiegand & contrôle d'accès

### Formats de trames supportés

| Bits | Type | Contenu |
|---|---|---|
| 4 | Touche clavier | Chiffre 0–9, `#` (code 13), `*` (codes 11, 14) |
| 8 | ESC/CLEAR | Code 27 = effacement du buffer clavier |
| 26 | Badge RFID ou empreinte | code >= 100 ? RFID ; code < 100 ? empreinte digitale |
| 34 | Badge RFID étendu | Facility code (16 bits hauts) + card number (16 bits bas) |

### Buffer clavier

- Les chiffres s'accumulent dans un buffer de 10 caractères (FIFO si dépassement)
- `#` ? validation du code saisi puis vidage du buffer
- `*` ou ESC ? vidage immédiat + double clignotement LED rouge
- Timeout 10 000 ms sans appui ? vidage automatique

### Filtre anti-trame spurieuse

Après réception d'une trame >= 24 bits, les trames 4 bits sont ignorées pendant 500 ms (évite les faux chiffres générés par certains lecteurs).

### Empreintes digitales (26 bits, code < 100)

Le lecteur hardware valide l'empreinte en interne et envoie l'ID via Wiegand. L'ESP32 vérifie cet ID dans la base de codes de type 2.

### Décodage 34 bits

Extraction de `facilityCode` et `cardNumber`. Ces champs sont inclus dans la publication MQTT `roller/access`.

### Retour visuel lecteur

| Événement | LED |
|---|---|
| Accès accordé | LED verte — 1 clignotement 50 ms |
| Accès refusé | LED rouge — 2 clignotements (50 ms ON / 50 ms OFF) |

---

## 8. Modes d'authentification

| Mode | Valeur | Comportement |
|---|---|---|
| PIN uniquement | 0 | Seul le clavier est pris en compte ; les badges RFID sont ignorés |
| RFID uniquement | 1 | Seuls les badges RFID/empreintes sont pris en compte (défaut) |
| PIN + RFID (2FA) | 2 | Les deux facteurs sont requis ; chaque facteur peut arriver en premier ; timeout 30 s |

### Fenêtre de commande manuelle post-authentification

Après une authentification réussie, une fenêtre de `manualWindowDuration` ms (15 000 ms par défaut) permet d'envoyer des commandes directement depuis le clavier :

| Touche | Action |
|---|---|
| 1, 2 ou 3 | Ouvrir |
| 7, 8 ou 9 | Fermer |
| Toute autre touche | Fermeture immédiate de la fenêtre |

La fenêtre expire automatiquement après `manualWindowDuration` ms.

---

## 9. Mode apprentissage

### Activation

Via l'interface web ou via MQTT (`roller/learn`). On spécifie le type (0 = PIN, 1 = RFID, 2 = empreinte) et un nom (1–31 caractères).

### Comportement

- Timeout automatique de **60 secondes**
- Le prochain code/badge/empreinte du type configuré est automatiquement enregistré
- L'apprentissage s'arrête dès l'enregistrement, à l'expiration, ou sur arrêt manuel

### Publications MQTT

| Événement | Payload |
|---|---|
| Démarrage | `{"learning":true,"type":T,"name":"X","timeout":60}` |
| Arrêt | `{"learning":false}` |

L'état d'apprentissage est visible dans `/api/status` via les champs `learning` et `learningType`.

---

## 10. Gestion des codes d'accès

### Capacité et structure

Maximum **50 codes** simultanés. Chaque code contient :

| Champ | Type | Description |
|---|---|---|
| `code` | `uint32_t` | Valeur numérique |
| `type` | `uint8_t` | 0 = PIN, 1 = RFID, 2 = empreinte |
| `name` | `char[32]` | Étiquette (max 31 caractères) |
| `active` | `bool` | Activation individuelle |

### Opérations disponibles

| Opération | Via web | Via API REST | Via MQTT |
|---|---|---|---|
| Lister | Onglet Codes | GET `/api/codes` | — |
| Ajouter | Formulaire d'ajout | POST `/api/codes` | `roller/codes/add` |
| Supprimer par index | Bouton dans le tableau | DELETE `/api/codes?index=X` | — |
| Supprimer par code+type | — | — | `roller/codes/remove` |
| Mode apprentissage | Bouton dédié | POST `/api/learn` | `roller/learn` |

### Validations à l'ajout

- Code != 0
- Type dans [0, 1, 2]
- Nom entre 1 et 31 caractères
- Pas de doublon (même code + même type)
- Nombre total < 50

### Persistance

Stockage en NVS sous `"codeCount"` et `"code0"` … `"code49"` (structs bruts d'AccessCode).

---

## 11. Journal d'accès

- Buffer circulaire de **100 entrées** en RAM
- Chaque entrée : `{timestamp (ms depuis boot), code, granted, type}`
- Consultable via l'onglet **Journaux** de l'interface web ou l'API GET `/api/logs`
- Réinitialisé à chaque redémarrage (stockage RAM uniquement)

---

## 12. Interface web & API REST

### Authentification de session

- Token de session 128 bits généré aléatoirement via `esp_random()` x4
- Cookie `session=TOKEN; HttpOnly; SameSite=Strict; Path=/`
- Déconnexion : rotation du token (toutes les sessions existantes são invalidées)
- Toutes les routes API protégées retournent HTTP 401 JSON si le cookie est absent ou invalide

### Endpoints HTTP

| Méthode | Chemin | Auth | Description |
|---|---|---|---|
| GET | `/` | Oui | Dashboard principal (HTML/CSS/JS embarqué en PROGMEM) |
| GET | `/login` | Non | Page de connexion (redirige vers `/` si déjà connecté) |
| POST | `/login` | Non | Vérification du mot de passe — pose le cookie de session |
| POST | `/logout` | Non | Rotation du token — suppression du cookie |
| GET | `/api/status` | Oui | État complet du système |
| POST | `/api/relay` | Oui | Commande relais : `{"action":"open"/"close"/"stop"}` |
| POST | `/api/learn` | Oui | Démarrer apprentissage : `{"type":T,"name":"X"}` |
| POST | `/api/learn/stop` | Oui | Arrêter le mode apprentissage |
| GET | `/api/codes` | Oui | Liste de tous les codes d'accès |
| POST | `/api/codes` | Oui | Ajouter un code : `{"code":N,"type":T,"name":"X"}` |
| DELETE | `/api/codes?index=X` | Oui | Supprimer le code à l'index X |
| GET | `/api/logs` | Oui | 100 derniers accès |
| GET | `/api/config` | Oui | Configuration complète (mot de passe exclu de la réponse) |
| POST | `/api/config` | Oui | Sauvegarder la configuration |
| GET | `/api/pins` | Oui | Valeurs actuelles des 10 GPIO |
| POST | `/api/pins` | Oui | Sauvegarder les GPIO (validation 0–39, redémarrage requis) |
| POST | `/api/wifi/reset` | Oui | Effacer les identifiants WiFi + redémarrer |
| POST | `/api/restart` | Oui | Redémarrer l'ESP32 |
| GET/POST | `/update` | Non | Interface ElegantOTA pour upload firmware |
| WS | `/ws` | Oui | Flux de logs temps réel |

### Réponse `/api/status`

```json
{
  "mqtt": true,
  "barrier": false,
  "wifi": true,
  "ip": "192.168.1.100",
  "ssid": "MonReseau",
  "relayActive": false,
  "learning": false,
  "learningType": -1,
  "authMode": 1,
  "shutterPosition": 75.3,
  "shutterKnown": true,
  "manualWindow": false,
  "manualWindowRemaining": 0
}
```

### Interface utilisateur embarquée

L'interface HTML/CSS/JS complète est stockée en PROGMEM (aucun fichier externe, aucun CDN). Elle propose :

- **Onglet Contrôle** : boutons Ouvrir / Stop / Fermer, barre de progression de position live, cartes de statut (MQTT, WiFi, barrière, relais), indicateur de mode apprentissage avec pastille animée, indicateur de fenêtre de commande manuelle avec temps restant
- **Onglet Codes d'accès** : tableau des codes avec nom/type/état actif, formulaire d'ajout, bouton de suppression ligne par ligne
- **Onglet Journaux** : tableau des 100 derniers accès avec horodatage, code, résultat (accordé/refusé), type
- **Onglet Configuration** : paramètres MQTT (serveur, port, user, password, topic), durée relais, activation barrière, mode d'authentification, IP statique (IP/passerelle/masque), durées de course complète (ouverture/fermeture), durée fenêtre manuelle, mot de passe admin
- **Onglet Pins** : formulaire des 10 GPIO avec validation
- **Onglet Mise à jour** : lien vers `/update` (ElegantOTA)

---

## 13. WebSocket & logs temps réel

- Endpoint : `ws://<IP>/ws`
- À la connexion : envoi immédiat de l'historique des 200 derniers logs sous forme de message `{"logs":[...]}`
- En continu : chaque nouveau message de log est broadcasté à tous les clients connectés
- File d'attente thread-safe : 64 emplacements, écrite depuis n'importe quelle tâche FreeRTOS via section critique, vidée exclusivement depuis `loop()` (évite les accès WebSocket concurrents)
- Limiteur de débit : au plus 1 broadcast toutes les 30 ms
- Les clients inactifs/déconnectés sont purgés à chaque itération de `loop()`

---

## 14. Intégration MQTT

### Configuration

| Paramètre | Défaut | Description |
|---|---|---|
| Broker | (vide) | MQTT désactivé si vide |
| Port | 1883 | Port TCP du broker |
| Username | (vide) | Optionnel |
| Password | (vide) | Optionnel |
| Topic de base | `roller` | Préfixe de tous les topics |

### Reconnexion automatique

- Vérifiée toutes les 5 secondes dans `loop()`
- Client ID aléatoire : `ESP32-Roller-XXXX`
- Réabonnement complet aux 7 topics à chaque reconnexion

### Topics souscrits (commandes entrantes)

| Topic | Payload | Action |
|---|---|---|
| `roller/cmd` | `"open"` | Ouvrir le volet |
| `roller/cmd` | `"close"` | Fermer le volet |
| `roller/cmd` | `"stop"` | Arrêter le volet |
| `roller/codes/add` | `{"code":N,"type":T,"name":"X"}` | Ajouter un code d'accès |
| `roller/codes/remove` | `{"code":N,"type":T}` | Supprimer un code d'accès |
| `roller/learn` | `{"type":T,"name":"X"}` | Démarrer le mode apprentissage (60 s) |
| `roller/learn/stop` | (quelconque) | Arrêter le mode apprentissage |

### Topics publiés (événements sortants)

| Topic | Payload | Déclencheur |
|---|---|---|
| `roller/status` | `{"state":"online"}` | Connexion au broker |
| `roller/status` | `{"learning":true,"type":T,"name":"X","timeout":60}` | Démarrage apprentissage |
| `roller/status` | `{"learning":false}` | Arrêt apprentissage |
| `roller/status` | `{"event":"barrier_triggered"}` | Arrêt d'urgence barrière |
| `roller/relay` | `{"action":"open"/"close","duration":N}` | Activation relais (mode temporisé) |
| `roller/relay` | `{"action":"open"/"close","source":"switch"}` | Activation relais (interrupteur mural) |
| `roller/relay` | `{"action":"stopped"}` | Désactivation relais |
| `roller/access` | `{"code":N,"granted":true/false,"type":"pin"/"rfid"/"fingerprint"/"pin+rfid","bits":26}` | Toute tentative d'authentification |
| `roller/access` | + `"uid"`, `"facility_code"`, `"card_number"` | Badge 34 bits spécifiquement |
| `roller/codes` | `{"action":"added"/"removed","code":N,"type":T,"name":"X","total":N}` | Ajout ou suppression de code |
| `roller/shutter` | `{"position":X.X}` | Après chaque fin de mouvement |

### Exemple avec Mosquitto

```bash
# Ouvrir le volet
mosquitto_pub -h localhost -t "roller/cmd" -m "open"

# Fermer le volet
mosquitto_pub -h localhost -t "roller/cmd" -m "close"

# Arrêter
mosquitto_pub -h localhost -t "roller/cmd" -m "stop"

# Ajouter un badge RFID
mosquitto_pub -h localhost -t "roller/codes/add" \
  -m '{"code":123456,"type":1,"name":"Badge entree"}'

# Démarrer l'apprentissage d'un badge RFID
mosquitto_pub -h localhost -t "roller/learn" \
  -m '{"type":1,"name":"Nouveau badge"}'

# Écouter tous les événements
mosquitto_sub -h localhost -t "roller/#" -v
```

---

## 15. Persistance NVS (flash)

Toutes les données sont stockées dans le namespace NVS `"roller"` et survivent aux coupures de courant.

### Configuration système

| Clé NVS | Type | Description |
|---|---|---|
| `relayDur` | ULong | Durée timeout relais (ms) |
| `photoEn` | Bool | Barrière photoélectrique activée |
| `mqttSrv` | String | Adresse du broker MQTT |
| `mqttPort` | Int | Port du broker MQTT |
| `mqttUser` | String | Utilisateur MQTT |
| `mqttPass` | String | Mot de passe MQTT |
| `mqttTop` | String | Topic de base MQTT |
| `adminPw` | String | Mot de passe admin web |
| `authMode` | UChar | Mode authentification (0/1/2) |
| `useStaticIP` | Bool | IP statique activée |
| `staticIP` | String | Adresse IP statique |
| `staticGW` | String | Passerelle |
| `staticSN` | String | Masque sous-réseau |
| `fullOpDur` | ULong | Durée course complète ouverture (ms) |
| `fullClDur` | ULong | Durée course complète fermeture (ms) |
| `manWinDur` | ULong | Durée fenêtre commande manuelle (ms) |
| `shutterPos` | Float | Dernière position connue (0.0–100.0, ou -1 si inconnue) |
| `init` | Bool | Indicateur de première initialisation |

### Codes d'accès

| Clé NVS | Type | Description |
|---|---|---|
| `codeCount` | Int | Nombre de codes enregistrés |
| `code0` … `code49` | Bytes | Structs `AccessCode` bruts |

### Broches GPIO

| Clé NVS | Signal |
|---|---|
| `pin_wgD0` / `pin_wgD1` | Wiegand D0 / D1 |
| `pin_rlOp` / `pin_rlCl` | Relais ouverture / fermeture |
| `pin_phBr` | Barrière photoélectrique |
| `pin_stLd` | LED de statut |
| `pin_rlR` / `pin_rlG` | LED lecteur rouge / verte |
| `pin_upSw` / `pin_dwSw` | Interrupteur monter / descendre |

---

## 16. Mise à jour firmware OTA

- Basé sur **ElegantOTA** en mode asynchrone
- Accessible à `http://<IP_ESP32>/update`
- Pas d'authentification sur cet endpoint (prévu pour réseaux locaux de confiance)
- Uploader le fichier `.bin` compilé par PlatformIO : `.pio/build/<env>/firmware.bin`

---

## 17. Sécurité

### Mesures implémentées

- Token de session 128 bits aléatoire (HttpOnly, SameSite=Strict) sur toutes les routes API
- Rotation du token à la déconnexion — invalidation immédiate de toutes les sessions
- Interverrouillage hardware double relais : impossible d'activer les deux simultanément
- Arrêt d'urgence sur pression simultanée des deux interrupteurs muraux
- Validation des entrées API : longueur des noms, plage des types (0–2), code != 0, absence de doublon, maximum 50 codes
- Validation numérique des GPIO (0–39) et des index de suppression
- Vérification logicielle du relais opposé avant toute activation

### Limitations connues (voir [SECURITY.md](SECURITY.md))

| Sévérité | Problème |
|---|---|
| Critique | Commandes MQTT non authentifiées : tout client sur le broker peut contrôler le volet |
| Critique | HTTP non chiffré (pas de TLS/HTTPS) |
| Critique | MQTT non chiffré |
| Critique | Mots de passe stockés en clair dans la NVS |
| Élevée | Absence de limitation des tentatives sur la page de connexion web et sur les codes physiques |
| Moyenne | Risque de dépassement de tampon sur les champs `name` en entrée MQTT |
| Moyenne | Injection JSON possible via des payloads MQTT malformés |

---

## 18. Installation pas à pas

### 1. Cloner et compiler

```bash
git clone <votre-repo>
cd ESP32-Roller-Shutter-Controller
pio run --target upload
pio device monitor
```

### 2. Configuration LilyGO T-Relay (si applicable)

Dans `platformio.ini`, vérifier le port USB :

```ini
upload_port = COM7
monitor_port = COM7
```

Le dongle T-U2T gère le passage en mode bootloader automatiquement. En cas de blocage sur `Connecting…`, vérifier le port COM actif dans le Gestionnaire de périphériques.

### 3. Câblage Wiegand

- Connecter D0 et D1 du lecteur sur les GPIO configurés (36 et 39 par défaut)
- Les GPIO 36 et 39 sont input-only sur ESP32 — idéal pour D0/D1
- La plupart des lecteurs 12 V sortent des signaux logiques proches de 5 V. Utiliser un adaptateur de niveau logique vers 3,3 V pour une installation fiable

### 4. Premier démarrage

1. L'ESP32 crée le point d'accès `ESP32-Roller-Setup`
2. Se connecter avec un smartphone ou un PC
3. Saisir les identifiants WiFi dans le portail captif
4. Après redémarrage, l'adresse IP locale s'affiche dans le moniteur série

### 5. Accès à l'interface

```
http://<IP_ESP32>
```

Mot de passe par défaut : `admin`

### 6. Réinitialisation WiFi

- **Triple appui** sur le bouton BOOT dans les 10 premières secondes après le démarrage
- Ou via l'API : `POST /api/wifi/reset`

---

## 19. Structure du projet

```
ESP32-Roller-Shutter-Controller/
+-- platformio.ini                 # Configuration build PlatformIO
+-- MQTT_COMMANDS.md               # Référence complète des commandes MQTT
+-- SECURITY.md                    # Analyse de sécurité
+-- src/
¦   +-- main.cpp                   # Boucle principale, relais, switches, watchdog WiFi
¦   +-- config.h                   # Structs (Config, PinConfig, AccessCode, AccessLog), constantes, defaults GPIO
¦   +-- access_control.cpp/.h      # CRUD codes d'accès, journal, mode apprentissage
¦   +-- wiegand_handler.cpp/.h     # Décodage trames Wiegand, logique d'authentification 2FA
¦   +-- web_server.cpp/.h          # API REST, gestion sessions, UI HTML embarquée en PROGMEM
¦   +-- mqtt_handler.cpp           # Client MQTT, publications, souscriptions, reconnexion
¦   +-- log_manager.cpp/.h         # Buffer logs 200 entrées, broadcast WebSocket thread-safe
+-- include/
¦   +-- README
+-- lib/
¦   +-- README
+-- test/
    +-- README
```

---

## 20. Dépannage

### L'ESP32 ne se connecte pas au WiFi

- Vérifier les identifiants dans le portail captif
- Triple appui BOOT au démarrage pour réinitialiser les identifiants WiFi
- Moniteur série : messages WiFiManager en temps réel

### Le lecteur Wiegand ne répond pas

- Vérifier l'alimentation du lecteur (généralement 12 V)
- Vérifier le câblage D0/D1 sur les bons GPIO (36 et 39 par défaut)
- Moniteur série : `Wiegand input detected (X bits)` lors d'une présentation de badge
- Les GPIO 36 et 39 sont input-only sur ESP32 : ne pas les configurer en sortie

### MQTT ne se connecte pas

- Vérifier l'adresse du broker et le port dans la configuration
- S'assurer que le broker est accessible depuis le réseau local de l'ESP32
- Moniteur série : `MQTT connected` ou description de l'erreur

### Les relais ne s'activent pas

- Tester via les boutons Ouvrir/Fermer de l'interface web
- Vérifier les GPIO attribués dans l'onglet **Pins**
- Mesurer la tension de sortie des GPIO concernés avec un multimètre

### La position du volet est imprécise

- Calibrer `fullOpenDuration` et `fullCloseDuration` en mesurant la durée réelle de course complète du volet
- Effectuer un aller complet après calibration pour que les valeurs se synchronisent

### L'interface web ne charge pas

- Vérifier que l'IP de l'ESP32 est correcte (moniteur série au démarrage)
- Vider le cache du navigateur
- Vérifier qu'aucun pare-feu ne bloque le port 80

---

**Plateforme** : ESP32 — Arduino Framework via PlatformIO
