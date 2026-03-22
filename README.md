# 🏠 ESP32 Roller Shutter Controller

Système complet de contrôle de volet roulant avec ESP32, lecteur Wiegand (clavier/RFID/empreinte), interface web et MQTT.

## 🎯 Fonctionnalités

### ✅ Contrôle d'accès
- **Wiegand** : Clavier à code, badges RFID, lecteur d'empreintes
- **Gestion des codes** : Jusqu'à 50 codes d'accès différents
- **Types multiples** : Différenciation Wiegand/RFID/Empreinte
- **Stockage persistant** : Conservation en mémoire flash (NVS)
- **Fonctionnement hors-ligne** : Vérification locale sans connexion réseau

### 🌐 Interface Web
- **Design moderne** : Interface responsive avec gradients
- **Contrôle manuel** : Ouverture/Fermeture/Stop depuis le navigateur
- **Gestion des codes** : Ajout/Suppression via interface
- **Historique** : Visualisation des 100 derniers accès
- **Configuration** : Tous les paramètres modifiables en ligne
- **Statut en temps réel** : WiFi, MQTT, barrière photoélectrique

### 📡 MQTT
- **Publication automatique** : Événements d'accès et actions relais
- **Commandes à distance** : `open`, `close`, `stop` via MQTT
- **Topics configurables** : Personnalisation complète
- **Authentification** : Support utilisateur/mot de passe

### 🔧 Configuration
- **WiFi Manager** : Portail captif pour configuration WiFi initiale
- **Temporisation relais** : Durée d'activation configurable
- **Barrière photoélectrique** : Sécurité anti-blocage
- **Mise à jour OTA** : Firmware uploadable via navigateur

## 📋 Matériel requis

### Composants principaux
- **ESP32** (Freenove ESP32 WROVER ou compatible)
- **Lecteur Wiegand** (ex: TF886 avec clavier/RFID/empreinte)
- **2x Relais** pour contrôle ouverture/fermeture
- **Barrière photoélectrique** (optionnelle)
- **LED de statut** (optionnelle)

### Câblage

```
ESP32 Pin     →  Composant
─────────────────────────────
GPIO 32       →  Wiegand D0 (PINK)
GPIO 33       →  Wiegand D1 (BROWN)
GPIO 25       →  Relais OUVERTURE  / à modifier
GPIO 26       →  Relais FERMETURE  / à modifier pour lyligo
GPIO 27       →  Barrière photoélectrique
GPIO 2        →  LED de statut  / 25 sur lilygo
```

## 🚀 Installation

### 1. PlatformIO
```bash
# Cloner le projet
git clone <votre-repo>
cd ESP32-Relay

# Compiler et uploader
pio run --target upload

# Moniteur série
pio device monitor
```

### 1.b Configuration spécifique LilyGO T-Relay

Si vous utilisez une **LilyGO T-Relay** avec le dongle **T-U2T**:

1. Bien définir le port série dans `platformio.ini` pour éviter les erreurs de flash.

```ini
[env:freenove_esp32_wrover]
upload_port = COM7
monitor_port = COM7
monitor_speed = 115200
```

2. Avec le dongle USB **T-U2T**, le passage en mode bootloader est généralement automatique pendant l'upload.
3. En cas de blocage sur `Connecting...`, vérifier le port COM actif et relancer l'upload.

### 1.c Câblage Wiegand sur T-Relay

- Connecter `D0` et `D1` du lecteur Wiegand sur les GPIO configurés dans le firmware (par défaut `GPIO32` et `GPIO33`).
- Beaucoup de lecteurs Wiegand 12V sortent des signaux logiques proches de 5V.
- Certains montages fonctionnent directement, mais l'ESP32 n'est pas officiellement 5V tolerant sur ses GPIO: pour une installation fiable, utiliser un abaisseur de niveau (ou diviseur resistif) vers 3.3V.

### 2. Configuration initiale

1. **Premier démarrage** : L'ESP32 crée un point d'accès WiFi nommé `ESP32-Roller-Setup`
2. **Connexion** : Se connecter au WiFi avec un smartphone/PC
3. **Configuration** : Entrer les identifiants WiFi dans le portail captif
4. **Redémarrage** : L'ESP32 se connecte au réseau configuré

### 3. Accès à l'interface

```
http://<IP_ESP32>
```

L'adresse IP s'affiche dans le moniteur série au démarrage.

## 📚 Utilisation

### Ajout d'un code d'accès

1. Accéder à l'onglet **"Codes d'Accès"**
2. Cliquer sur **"+ Ajouter un Code"**
3. Remplir :
   - **Code** : Numéro (ex: code badge RFID ou PIN clavier)
   - **Type** : Wiegand/RFID/Empreinte
   - **Nom** : Identifiant (ex: "Utilisateur 1")
4. Enregistrer

### Configuration MQTT

1. Onglet **"Configuration"** → Section MQTT
2. Remplir :
   - **Serveur** : Adresse du broker MQTT
   - **Port** : 1883 (par défaut)
   - **Utilisateur** / **Mot de passe** (si requis)
   - **Topic** : Préfixe des topics (ex: `roller`)
3. Enregistrer

#### Topics MQTT

**Publications** (ESP32 → Broker) :
```
roller/access      → Événements d'accès (granted/denied)
roller/relay       → État des relais (open/close/stopped)
roller/status      → Statut système (barrier, online)
```

**Souscriptions** (Broker → ESP32) :
```
roller/cmd         → Commandes (payload: "open", "close", "stop")
```

Exemple avec Mosquitto :
```bash
# Ouvrir le volet
mosquitto_pub -h localhost -t "roller/cmd" -m "open"

# Fermer le volet
mosquitto_pub -h localhost -t "roller/cmd" -m "close"

# Arrêter
mosquitto_pub -h localhost -t "roller/cmd" -m "stop"

# Écouter les événements
mosquitto_sub -h localhost -t "roller/#"
```

### Mise à jour OTA

1. Onglet **"Mise à Jour"** → **"Ouvrir Interface OTA"**
2. Ou directement : `http://<IP_ESP32>/update`
3. Uploader le fichier `.bin` compilé

## 🔒 Sécurité

- **Stockage local** : Codes en mémoire flash (survie aux coupures)
- **Mot de passe admin** : Protection de la configuration (à implémenter)
- **Barrière photoélectrique** : Arrêt automatique en cas d'obstacle
- **Timeout relais** : Désactivation automatique après temporisation

## 🛠️ Configuration avancée

### Modifier les pins dans `main.cpp`
```cpp
#define WIEGAND_D0        32
#define WIEGAND_D1        33
#define RELAY_OPEN        25
#define RELAY_CLOSE       26
#define PHOTO_BARRIER     27
#define STATUS_LED        2
```

### Ajuster les limites
```cpp
AccessCode accessCodes[50];   // Max 50 codes
AccessLog accessLogs[100];    // Max 100 logs
```

### Temporisation par défaut
```cpp
config.relayDuration = 5000;  // 5 secondes (modifiable via web)
```

## 📊 Structure du projet

```
ESP32-Relay/
├── platformio.ini          # Configuration PlatformIO
├── src/
│   ├── main.cpp           # Programme principal
│   ├── web_server.h       # Interface web (HTML embarqué)
│   ├── web_server.cpp     # Endpoints API REST
│   └── mqtt_handler.cpp   # Gestion MQTT
├── include/
└── README.md
```

## 🐛 Dépannage

### L'ESP32 ne se connecte pas au WiFi
- Vérifier les identifiants dans le portail captif
- Réinitialiser : maintenir le bouton BOOT au démarrage
- Utiliser le moniteur série pour voir les erreurs

### Le Wiegand ne fonctionne pas
- Vérifier le câblage D0/D1 (pins 32/33)
- Alimenter correctement le lecteur (généralement 12V)
- Vérifier les logs série : `Wiegand input detected`

### MQTT ne se connecte pas
- Ping le broker depuis le réseau de l'ESP32
- Vérifier utilisateur/mot de passe
- Consulter les logs série : `MQTT connected!`

### Les relais ne s'activent pas
- Tester manuellement via l'interface web
- Vérifier le câblage (pins 25/26)
- Mesurer avec un multimètre la sortie GPIO

## 📝 TODO / Améliorations futures

- [ ] Authentification web (login/password)
- [ ] Support SSL/TLS pour MQTT
- [ ] Export des logs en CSV
- [ ] Planification horaire (ouverture automatique)
- [ ] Notification push (Telegram, email)
- [ ] Intégration Home Assistant
- [ ] Support de plusieurs volets

## 📄 Licence

MIT License - Libre d'utilisation et modification

## 👤 Auteur

Développé pour contrôle de volet roulant avec sécurité Wiegand

---

**Version** : 1.0  
**Date** : Novembre 2025  
**Plateforme** : ESP32 (Arduino Framework)
