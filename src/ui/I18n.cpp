#include "ui/I18n.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace ns {

namespace {

struct Row {
    const char* en;
    const char* pt; // Brazilian Portuguese
    const char* es;
    const char* tr;
    const char* fr;
    const char* it;
};

// clang-format off
const Row kRows[] = {
    // Tabs and collections
    { "All games", "Todos os jogos", "Todos los juegos", "Tüm oyunlar", "Tous les jeux", "Tutti i giochi" },
    { "Favorites", "Favoritos", "Favoritos", "Favoriler", "Favoris", "Preferiti" },
    { "2024 & newer", "2024 em diante", "2024 en adelante", "2024 ve sonrası", "2024 et après", "Dal 2024" },
    { "Downloads", "Downloads", "Descargas", "İndirmeler", "Téléchargements", "Download" },
    { "ALL GAMES", "TODOS OS JOGOS", "TODOS LOS JUEGOS", "TÜM OYUNLAR", "TOUS LES JEUX", "TUTTI I GIOCHI" },
    { "FAVORITES", "FAVORITOS", "FAVORITOS", "FAVORİLER", "FAVORIS", "PREFERITI" },
    { "2024 & NEWER", "2024 EM DIANTE", "2024 EN ADELANTE", "2024 VE SONRASI", "2024 ET APRÈS", "DAL 2024" },
    { "DOWNLOADS", "DOWNLOADS", "DESCARGAS", "İNDİRMELER", "TÉLÉCHARGEMENTS", "DOWNLOAD" },
    { "SEARCH: \"{0}\"", "BUSCA: \"{0}\"", "BÚSQUEDA: \"{0}\"", "ARAMA: \"{0}\"", "RECHERCHE : \"{0}\"", "RICERCA: \"{0}\"" },
    { "{0} game", "{0} jogo", "{0} juego", "{0} oyun", "{0} jeu", "{0} gioco" },
    { "{0} games", "{0} jogos", "{0} juegos", "{0} oyun", "{0} jeux", "{0} giochi" },
    { "{0} versions", "{0} versões", "{0} versiones", "{0} sürüm", "{0} versions", "{0} versioni" },

    // Catalogue
    { "No games found", "Nenhum jogo encontrado", "No se encontraron juegos", "Oyun bulunamadı", "Aucun jeu trouvé", "Nessun gioco trovato" },
    { "Press SQUARE on a game to save it here.", "Pressione QUADRADO em um jogo para salvá-lo aqui.", "Pulsa CUADRADO en un juego para guardarlo aquí.", "Buraya kaydetmek için bir oyunda KARE tuşuna bas.", "Appuyez sur CARRÉ sur un jeu pour l'enregistrer ici.", "Premi QUADRATO su un gioco per salvarlo qui." },
    { "Press TRIANGLE to change your search, or CIRCLE to reset.", "Pressione TRIÂNGULO para mudar a busca ou CÍRCULO para limpar.", "Pulsa TRIÁNGULO para cambiar la búsqueda o CÍRCULO para restablecerla.", "Aramayı değiştirmek için ÜÇGEN, sıfırlamak için DAİRE tuşuna bas.", "Appuyez sur TRIANGLE pour modifier la recherche, ou ROND pour la réinitialiser.", "Premi TRIANGOLO per cambiare la ricerca o CERCHIO per azzerarla." },
    { "Clear", "Limpar", "Borrar", "Temizle", "Effacer", "Cancella" },
    { "Details", "Detalhes", "Detalles", "Ayrıntılar", "Détails", "Dettagli" },
    { "Search", "Buscar", "Buscar", "Ara", "Rechercher", "Cerca" },
    { "Favorite", "Favoritar", "Favorito", "Favori", "Favori", "Preferito" },
    { "Remove favorite", "Remover favorito", "Quitar favorito", "Favorilerden çıkar", "Retirer des favoris", "Rimuovi dai preferiti" },
    { "Page", "Página", "Página", "Sayfa", "Page", "Pagina" },
    { "Settings", "Configurações", "Ajustes", "Ayarlar", "Paramètres", "Impostazioni" },

    // Game page
    { "GAME DETAILS", "DETALHES DO JOGO", "DETALLES DEL JUEGO", "OYUN AYRINTILARI", "DÉTAILS DU JEU", "DETTAGLI DEL GIOCO" },
    { "Download", "Baixar", "Descargar", "İndir", "Télécharger", "Scarica" },
    { "Download this version", "Baixar esta versão", "Descargar esta versión", "Bu sürümü indir", "Télécharger cette version", "Scarica questa versione" },
    { "Back", "Voltar", "Atrás", "Geri", "Retour", "Indietro" },
    { "VERSIONS", "VERSÕES", "VERSIONES", "SÜRÜMLER", "VERSIONS", "VERSIONI" },
    { "Version not given", "Versão não informada", "Versión no indicada", "Sürüm belirtilmemiş", "Version non indiquée", "Versione non indicata" },
    { "Size on page", "Tamanho na página", "Tamaño en la página", "Boyut sayfada", "Taille sur la page", "Dimensione nella pagina" },

    // Starting a download
    { "Checking torrent link", "Verificando link torrent", "Verificando enlace torrent", "Torrent bağlantısı kontrol ediliyor", "Vérification du lien torrent", "Verifica del link torrent" },
    { "Choose a torrent", "Escolha um torrent", "Elige un torrent", "Bir torrent seç", "Choisissez un torrent", "Scegli un torrent" },
    { "Add to downloads", "Adicionar aos downloads", "Añadir a descargas", "İndirmelere ekle", "Ajouter aux téléchargements", "Aggiungi ai download" },

    // Download queue
    { "TRANSFER QUEUE", "FILA DE TRANSFERÊNCIAS", "COLA DE TRANSFERENCIAS", "AKTARIM KUYRUĞU", "FILE DE TRANSFERTS", "CODA DI TRASFERIMENTO" },
    { "UP TO 2 ACTIVE DOWNLOADS", "ATÉ 2 DOWNLOADS ATIVOS", "HASTA 2 DESCARGAS ACTIVAS", "EN FAZLA 2 ETKİN İNDİRME", "JUSQU'À 2 TÉLÉCHARGEMENTS ACTIFS", "FINO A 2 DOWNLOAD ATTIVI" },
    { "Your download queue is empty", "Sua fila de downloads está vazia", "Tu cola de descargas está vacía", "İndirme kuyruğun boş", "Votre file de téléchargements est vide", "La coda dei download è vuota" },
    { "Open a game and select Download to add its torrent.", "Abra um jogo e selecione Baixar para adicionar o torrent.", "Abre un juego y elige Descargar para añadir su torrent.", "Torrent'ini eklemek için bir oyun aç ve İndir'i seç.", "Ouvrez un jeu et choisissez Télécharger pour ajouter son torrent.", "Apri un gioco e scegli Scarica per aggiungere il torrent." },
    { "size pending", "tamanho pendente", "tamaño pendiente", "boyut bekleniyor", "taille en attente", "dimensione in attesa" },
    { "{0} peers", "{0} peers", "{0} pares", "{0} eş", "{0} pairs", "{0} peer" },
    { "Pause / Resume", "Pausar / Retomar", "Pausar / Reanudar", "Duraklat / Sürdür", "Pause / Reprendre", "Pausa / Riprendi" },
    { "Pause", "Pausar", "Pausar", "Duraklat", "Pause", "Pausa" },
    { "Resume", "Retomar", "Reanudar", "Sürdür", "Reprendre", "Riprendi" },
    { "Install", "Instalar", "Instalar", "Yükle", "Installer", "Installa" },
    { "Cancel", "Cancelar", "Cancelar", "İptal", "Annuler", "Annulla" },
    { "Delete", "Excluir", "Eliminar", "Sil", "Supprimer", "Elimina" },
    { "Select", "Selecionar", "Seleccionar", "Seç", "Sélectionner", "Seleziona" },
    { "Queued", "Na fila", "En cola", "Sırada", "En attente", "In coda" },
    { "Downloading", "Baixando", "Descargando", "İndiriliyor", "Téléchargement", "Download in corso" },
    { "Pausing", "Pausando", "Pausando", "Duraklatılıyor", "Mise en pause", "Messa in pausa" },
    { "Paused", "Pausado", "En pausa", "Duraklatıldı", "En pause", "In pausa" },
    { "Complete", "Concluído", "Completado", "Tamamlandı", "Terminé", "Completato" },
    { "Failed", "Falhou", "Error", "Başarısız", "Échec", "Non riuscito" },
    { "Cancelled", "Cancelado", "Cancelado", "İptal edildi", "Annulé", "Annullato" },

    // Install
    { "Install downloaded packages", "Instalar pacotes baixados", "Instalar paquetes descargados", "İndirilen paketleri yükle", "Installer les paquets téléchargés", "Installa i pacchetti scaricati" },
    { "Checking downloaded PKG files...", "Verificando arquivos PKG baixados...", "Comprobando archivos PKG descargados...", "İndirilen PKG dosyaları denetleniyor...", "Vérification des fichiers PKG téléchargés...", "Verifica dei file PKG scaricati..." },
    { "Choose a package. Install the base game before updates or DLC.", "Escolha um pacote. Instale o jogo base antes de atualizações ou DLC.", "Elige un paquete. Instala el juego base antes de actualizaciones o DLC.", "Bir paket seç. Güncelleme veya DLC'den önce ana oyunu yükle.", "Choisissez un paquet. Installez le jeu de base avant les mises à jour ou DLC.", "Scegli un pacchetto. Installa il gioco base prima di aggiornamenti o DLC." },
    { "Install selected package", "Instalar pacote selecionado", "Instalar paquete seleccionado", "Seçili paketi yükle", "Installer le paquet choisi", "Installa il pacchetto scelto" },
    { "Installed", "Instalado", "Instalado", "Yüklendi", "Installé", "Installato" },
    { "Installing", "Instalando", "Instalando", "Yükleniyor", "Installation", "Installazione" },
    { "Ready to install", "Pronto para instalar", "Listo para instalar", "Yüklemeye hazır", "Prêt à installer", "Pronto da installare" },
    { "Preparing system installation", "Preparando a instalação no sistema", "Preparando la instalación en el sistema", "Sistem yüklemesi hazırlanıyor", "Préparation de l'installation", "Preparazione dell'installazione" },
    { "Registered with PS4 installer", "Registrado no instalador do PS4", "Registrado en el instalador de PS4", "PS4 yükleyicisine kaydedildi", "Enregistré auprès de l'installateur PS4", "Registrato nel programma di installazione PS4" },
    { "Update package; base game required", "Pacote de atualização; requer o jogo base", "Paquete de actualización; requiere el juego base", "Güncelleme paketi; ana oyun gerekli", "Paquet de mise à jour ; jeu de base requis", "Pacchetto di aggiornamento; serve il gioco base" },
    { "No complete .pkg files found. Archives must be extracted first.", "Nenhum arquivo .pkg completo encontrado. Arquivos compactados precisam ser extraídos antes.", "No se encontraron archivos .pkg completos. Primero hay que extraer los archivos comprimidos.", "Tam .pkg dosyası bulunamadı. Arşivler önce çıkarılmalı.", "Aucun fichier .pkg complet trouvé. Les archives doivent d'abord être extraites.", "Nessun file .pkg completo trovato. Gli archivi vanno prima estratti." },

    // Settings
    { "Catalog source", "Fonte do catálogo", "Fuente del catálogo", "Katalog kaynağı", "Source du catalogue", "Fonte del catalogo" },
    { "Sort games by", "Ordenar jogos por", "Ordenar juegos por", "Oyunları sırala", "Trier les jeux par", "Ordina i giochi per" },
    { "Download location", "Local de download", "Ubicación de descarga", "İndirme konumu", "Emplacement de téléchargement", "Posizione dei download" },
    { "Debrid service", "Serviço Debrid", "Servicio Debrid", "Debrid hizmeti", "Service Debrid", "Servizio Debrid" },
    { "Debrid API key", "Chave de API do Debrid", "Clave API de Debrid", "Debrid API anahtarı", "Clé API Debrid", "Chiave API Debrid" },
    { "Official artwork", "Arte oficial", "Arte oficial", "Resmi görseller", "Visuels officiels", "Grafica ufficiale" },
    { "Refresh catalogue", "Atualizar catálogo", "Actualizar catálogo", "Kataloğu yenile", "Actualiser le catalogue", "Aggiorna catalogo" },
    { "Choose a service first", "Escolha um serviço primeiro", "Elige primero un servicio", "Önce bir hizmet seç", "Choisissez d'abord un service", "Scegli prima un servizio" },
    { "Not set", "Não definida", "Sin definir", "Ayarlanmadı", "Non définie", "Non impostata" },
    { "Saved", "Salva", "Guardada", "Kaydedildi", "Enregistrée", "Salvata" },
    { "On  \xC2\xB7  official icons", "Ligado  \xC2\xB7  ícones oficiais", "Activado  \xC2\xB7  iconos oficiales", "Açık  \xC2\xB7  resmi simgeler", "Activé  \xC2\xB7  icônes officielles", "Attivo  \xC2\xB7  icone ufficiali" },
    { "Off  \xC2\xB7  source covers", "Desligado  \xC2\xB7  capas das fontes", "Desactivado  \xC2\xB7  portadas de las fuentes", "Kapalı  \xC2\xB7  kaynak kapakları", "Désactivé  \xC2\xB7  jaquettes des sources", "Disattivo  \xC2\xB7  copertine delle fonti" },
    { "Change", "Alterar", "Cambiar", "Değiştir", "Modifier", "Modifica" },
    { "Close", "Fechar", "Cerrar", "Kapat", "Fermer", "Chiudi" },
    { "Refresh now", "Atualizar agora", "Actualizar ahora", "Şimdi yenile", "Actualiser", "Aggiorna ora" },
    { "Turn on / off", "Ligar / desligar", "Activar / desactivar", "Aç / kapat", "Activer / désactiver", "Attiva / disattiva" },
    { "Enter key", "Digitar chave", "Introducir clave", "Anahtarı gir", "Saisir la clé", "Inserisci chiave" },
    { "Choose source", "Escolher fonte", "Elegir fuente", "Kaynak seç", "Choisir la source", "Scegli la fonte" },
    { "Choose order", "Escolher ordem", "Elegir orden", "Sırayı seç", "Choisir l'ordre", "Scegli l'ordine" },
    { "Choose service", "Escolher serviço", "Elegir servicio", "Hizmet seç", "Choisir le service", "Scegli il servizio" },
    { "Use location", "Usar local", "Usar ubicación", "Konumu kullan", "Utiliser cet emplacement", "Usa posizione" },
    { "Recently added", "Adicionados recentemente", "Añadidos recientemente", "Son eklenenler", "Ajoutés récemment", "Aggiunti di recente" },
    { "Title A-Z", "Título A-Z", "Título A-Z", "Başlık A-Z", "Titre A-Z", "Titolo A-Z" },
    { "Release year", "Ano de lançamento", "Año de lanzamiento", "Çıkış yılı", "Année de sortie", "Anno di uscita" },
    { "Smallest first", "Menores primeiro", "Más pequeños primero", "Önce en küçük", "Plus petits d'abord", "Prima i più piccoli" },
    { "All sources", "Todas as fontes", "Todas las fuentes", "Tüm kaynaklar", "Toutes les sources", "Tutte le fonti" },
    { "Off", "Desligado", "Desactivado", "Kapalı", "Désactivé", "Disattivo" },
    { "Torrents download directly", "Torrents baixam diretamente", "Los torrents se descargan directamente", "Torrentler doğrudan iner", "Les torrents se téléchargent directement", "I torrent si scaricano direttamente" },
    { "Get the API key of your account at the address shown, then enter it in Settings.", "Pegue a chave de API da sua conta no endereço indicado e digite-a em Configurações.", "Obtén la clave API de tu cuenta en la dirección indicada e introdúcela en Ajustes.", "Hesabının API anahtarını gösterilen adresten al, sonra Ayarlar'a gir.", "Récupérez la clé API de votre compte à l'adresse indiquée, puis saisissez-la dans Paramètres.", "Ottieni la chiave API del tuo account all'indirizzo indicato, poi inseriscila in Impostazioni." },
    { "Choose download location", "Escolher local de download", "Elegir ubicación de descarga", "İndirme konumunu seç", "Choisir l'emplacement", "Scegli la posizione dei download" },
    { "New downloads will use this location. Existing jobs stay where they are.", "Novos downloads usarão este local. Os atuais continuam onde estão.", "Las nuevas descargas usarán esta ubicación. Las actuales se quedan donde están.", "Yeni indirmeler bu konumu kullanır. Mevcut olanlar yerinde kalır.", "Les nouveaux téléchargements utiliseront cet emplacement. Les autres restent en place.", "I nuovi download useranno questa posizione. Quelli esistenti restano dove sono." },
    { "USB destinations are checked for write access when selected.", "Destinos USB são testados para gravação ao serem escolhidos.", "Los destinos USB se comprueban al elegirlos.", "USB hedefleri seçildiğinde yazma için denetlenir.", "Les destinations USB sont vérifiées lors de la sélection.", "Le destinazioni USB vengono verificate quando selezionate." },
    { "New downloads: ", "Novos downloads: ", "Nuevas descargas: ", "Yeni indirmeler: ", "Nouveaux téléchargements : ", "Nuovi download: " },
    { "Internal storage", "Armazenamento interno", "Almacenamiento interno", "Dahili depolama", "Stockage interne", "Memoria interna" },
    { "Bundled catalogue", "Catálogo incluído", "Catálogo incluido", "Dahili katalog", "Catalogue intégré", "Catalogo incluso" },
    { "Refreshing {0}", "Atualizando {0}", "Actualizando {0}", "{0} yenileniyor", "Actualisation de {0}", "Aggiornamento di {0}" },
    { "{0} updated / {1} cached / {2} unavailable", "{0} atualizados / {1} em cache / {2} indisponíveis", "{0} actualizados / {1} en caché / {2} no disponibles", "{0} güncellendi / {1} önbellekte / {2} erişilemez", "{0} mis à jour / {1} en cache / {2} indisponibles", "{0} aggiornati / {1} in cache / {2} non disponibili" },
    { "{0} updated / {1} cached / {2} unavailable | Saved catalogues retained", "{0} atualizados / {1} em cache / {2} indisponíveis | Catálogos salvos mantidos", "{0} actualizados / {1} en caché / {2} no disponibles | Catálogos guardados conservados", "{0} güncellendi / {1} önbellekte / {2} erişilemez | Kayıtlı kataloglar korundu", "{0} mis à jour / {1} en cache / {2} indisponibles | Catalogues enregistrés conservés", "{0} aggiornati / {1} in cache / {2} non disponibili | Cataloghi salvati mantenuti" },
    { "Caching provider cover art | Catalogue ready to browse", "Salvando capas das fontes | Catálogo pronto", "Guardando portadas de las fuentes | Catálogo listo", "Kaynak kapakları kaydediliyor | Katalog hazır", "Mise en cache des jaquettes | Catalogue prêt", "Salvataggio delle copertine | Catalogo pronto" },
    { "Could not start the refresh. Try again.", "Não foi possível iniciar a atualização. Tente de novo.", "No se pudo iniciar la actualización. Inténtalo de nuevo.", "Yenileme başlatılamadı. Tekrar dene.", "Impossible de lancer l'actualisation. Réessayez.", "Impossibile avviare l'aggiornamento. Riprova." },

    // Keyboard
    { "Search games", "Buscar jogos", "Buscar juegos", "Oyun ara", "Rechercher des jeux", "Cerca giochi" },
    { "Game title", "Título do jogo", "Título del juego", "Oyun adı", "Titre du jeu", "Titolo del gioco" },
    { "Paste or type the key", "Cole ou digite a chave", "Pega o escribe la clave", "Anahtarı yapıştır veya yaz", "Collez ou tapez la clé", "Incolla o digita la chiave" },
    { "Space", "Espaço", "Espacio", "Boşluk", "Espace", "Spazio" },
    { "Done", "OK", "Listo", "Tamam", "OK", "Fine" },

    // Notices
    { "Added to Downloads.", "Adicionado aos downloads.", "Añadido a descargas.", "İndirmelere eklendi.", "Ajouté aux téléchargements.", "Aggiunto ai download." },
    { "Connect a controller to browse the catalog.", "Conecte um controle para navegar no catálogo.", "Conecta un mando para explorar el catálogo.", "Kataloğa göz atmak için bir kumanda bağla.", "Connectez une manette pour parcourir le catalogue.", "Collega un controller per sfogliare il catalogo." },
    { "Download removed. Its files are being deleted.", "Download removido. Os arquivos estão sendo excluídos.", "Descarga eliminada. Se están borrando sus archivos.", "İndirme kaldırıldı. Dosyaları siliniyor.", "Téléchargement retiré. Ses fichiers sont en cours de suppression.", "Download rimosso. I suoi file vengono eliminati." },
    { "Refreshing the catalogue.", "Atualizando o catálogo.", "Actualizando el catálogo.", "Katalog yenileniyor.", "Actualisation du catalogue.", "Aggiornamento del catalogo." },
    { "Debrid key saved.", "Chave Debrid salva.", "Clave Debrid guardada.", "Debrid anahtarı kaydedildi.", "Clé Debrid enregistrée.", "Chiave Debrid salvata." },
    { "Debrid key removed.", "Chave Debrid removida.", "Clave Debrid eliminada.", "Debrid anahtarı kaldırıldı.", "Clé Debrid supprimée.", "Chiave Debrid rimossa." },
    { "Could not save the Debrid key.", "Não foi possível salvar a chave Debrid.", "No se pudo guardar la clave Debrid.", "Debrid anahtarı kaydedilemedi.", "Impossible d'enregistrer la clé Debrid.", "Impossibile salvare la chiave Debrid." },
    { "Could not save settings. Check storage permissions.", "Não foi possível salvar as configurações. Verifique o armazenamento.", "No se pudieron guardar los ajustes. Revisa el almacenamiento.", "Ayarlar kaydedilemedi. Depolamayı kontrol et.", "Impossible d'enregistrer les paramètres. Vérifiez le stockage.", "Impossibile salvare le impostazioni. Controlla la memoria." },
    { "Could not save favorites. Check storage permissions.", "Não foi possível salvar os favoritos. Verifique o armazenamento.", "No se pudieron guardar los favoritos. Revisa el almacenamiento.", "Favoriler kaydedilemedi. Depolamayı kontrol et.", "Impossible d'enregistrer les favoris. Vérifiez le stockage.", "Impossibile salvare i preferiti. Controlla la memoria." },
    { "Another PS4 dialog is open. Close it first.", "Outra janela do PS4 está aberta. Feche-a primeiro.", "Hay otra ventana del PS4 abierta. Ciérrala primero.", "Başka bir PS4 penceresi açık. Önce onu kapat.", "Une autre fenêtre PS4 est ouverte. Fermez-la d'abord.", "C'è un'altra finestra PS4 aperta. Chiudila prima." },
    { "Already in Downloads. Resume the existing entry with X.", "Já está nos downloads. Retome o item existente com X.", "Ya está en descargas. Reanuda la entrada existente con X.", "Zaten indirmelerde. Mevcut kaydı X ile sürdür.", "Déjà dans les téléchargements. Reprenez l'entrée existante avec X.", "Già nei download. Riprendi la voce esistente con X." },
    { "Restored the existing download instead of adding a duplicate.", "O download existente foi restaurado em vez de duplicado.", "Se restauró la descarga existente en lugar de duplicarla.", "Kopya eklemek yerine mevcut indirme geri yüklendi.", "Le téléchargement existant a été restauré au lieu d'un doublon.", "È stato ripristinato il download esistente invece di duplicarlo." },
    { "Download history is full.", "O histórico de downloads está cheio.", "El historial de descargas está lleno.", "İndirme geçmişi dolu.", "L'historique des téléchargements est plein.", "La cronologia dei download è piena." },
    { "Could not read the game page. Check internet and retry.", "Não foi possível ler a página do jogo. Verifique a internet e tente de novo.", "No se pudo leer la página del juego. Revisa internet y reinténtalo.", "Oyun sayfası okunamadı. İnterneti kontrol edip tekrar dene.", "Impossible de lire la page du jeu. Vérifiez la connexion et réessayez.", "Impossibile leggere la pagina del gioco. Controlla internet e riprova." },
    { "No public torrent link found on this game page.", "Nenhum link torrent público nesta página do jogo.", "No hay enlaces torrent públicos en esta página.", "Bu oyun sayfasında herkese açık torrent bağlantısı yok.", "Aucun lien torrent public sur la page de ce jeu.", "Nessun link torrent pubblico in questa pagina." },
    { "Download service could not start. Restart NULL_store and retry.", "O serviço de download não iniciou. Reinicie o NULL_store e tente de novo.", "El servicio de descargas no pudo iniciarse. Reinicia NULL_store y reinténtalo.", "İndirme hizmeti başlatılamadı. NULL_store'u yeniden başlatıp tekrar dene.", "Le service de téléchargement n'a pas démarré. Relancez NULL_store et réessayez.", "Il servizio di download non è partito. Riavvia NULL_store e riprova." },
    { "Download location saved. {0}", "Local de download salvo. {0}", "Ubicación de descarga guardada. {0}", "İndirme konumu kaydedildi. {0}", "Emplacement enregistré. {0}", "Posizione dei download salvata. {0}" },
    { "Could not save download location; check internal storage.", "Não foi possível salvar o local de download; verifique o armazenamento interno.", "No se pudo guardar la ubicación; revisa el almacenamiento interno.", "İndirme konumu kaydedilemedi; dahili depolamayı kontrol et.", "Impossible d'enregistrer l'emplacement ; vérifiez le stockage interne.", "Impossibile salvare la posizione; controlla la memoria interna." },
    { "Select a valid completed download first", "Selecione primeiro um download concluído", "Selecciona primero una descarga completada", "Önce tamamlanmış bir indirme seç", "Sélectionnez d'abord un téléchargement terminé", "Seleziona prima un download completato" },
    { "Already handed to the system installer. Check PS4 Notifications.", "Já foi enviado ao instalador do sistema. Veja as notificações do PS4.", "Ya se envió al instalador del sistema. Revisa las notificaciones del PS4.", "Zaten sistem yükleyicisine verildi. PS4 bildirimlerine bak.", "Déjà confié à l'installateur du système. Consultez les notifications PS4.", "Già affidato al programma di installazione. Controlla le notifiche PS4." },

    // Download engine: phases and errors
    { "Fetching torrent file", "Obtendo o arquivo torrent", "Obteniendo el archivo torrent", "Torrent dosyası alınıyor", "Récupération du fichier torrent", "Recupero del file torrent" },
    { "Queued with cached torrent file", "Na fila com o torrent salvo", "En cola con el torrent guardado", "Kayıtlı torrent ile sırada", "En attente avec le torrent enregistré", "In coda con il torrent salvato" },
    { "Queued for resume", "Na fila para retomar", "En cola para reanudar", "Sürdürmek için sırada", "En attente de reprise", "In coda per riprendere" },
    { "Paused by you. Press X to resume.", "Pausado por você. Pressione X para retomar.", "Pausado por ti. Pulsa X para reanudar.", "Sen duraklattın. Sürdürmek için X'e bas.", "Mis en pause. Appuyez sur X pour reprendre.", "In pausa. Premi X per riprendere." },
    { "Pausing; keeping downloaded pieces", "Pausando; as partes baixadas são mantidas", "Pausando; se conservan las partes descargadas", "Duraklatılıyor; indirilen parçalar korunuyor", "Mise en pause ; les parties téléchargées sont conservées", "Pausa; le parti scaricate restano" },
    { "Resuming saved download; checking existing pieces", "Retomando o download; verificando as partes existentes", "Reanudando la descarga; comprobando las partes existentes", "İndirme sürdürülüyor; mevcut parçalar denetleniyor", "Reprise du téléchargement ; vérification des parties", "Ripresa del download; verifica delle parti esistenti" },
    { "Waiting to delete downloaded files", "Aguardando para excluir os arquivos", "Esperando para borrar los archivos", "Dosyaları silmek için bekleniyor", "En attente de suppression des fichiers", "In attesa di eliminare i file" },
    { "Deleting downloaded files", "Excluindo os arquivos baixados", "Borrando los archivos descargados", "İndirilen dosyalar siliniyor", "Suppression des fichiers téléchargés", "Eliminazione dei file scaricati" },
    { "Stopping before deleting downloaded files", "Parando antes de excluir os arquivos", "Deteniendo antes de borrar los archivos", "Dosyalar silinmeden önce durduruluyor", "Arrêt avant la suppression des fichiers", "Arresto prima di eliminare i file" },
    { "Removed; downloaded files deleted", "Removido; arquivos excluídos", "Eliminado; archivos borrados", "Kaldırıldı; dosyalar silindi", "Retiré ; fichiers supprimés", "Rimosso; file eliminati" },
    { "Cancelled; files kept on disk", "Cancelado; arquivos mantidos", "Cancelado; archivos conservados", "İptal edildi; dosyalar korundu", "Annulé ; fichiers conservés", "Annullato; file mantenuti" },
    { "No peers answered. Retry later or try another source.", "Nenhum peer respondeu. Tente mais tarde ou use outra fonte.", "Ningún par respondió. Reinténtalo más tarde o usa otra fuente.", "Hiçbir eş yanıt vermedi. Sonra tekrar dene ya da başka bir kaynak kullan.", "Aucun pair n'a répondu. Réessayez plus tard ou choisissez une autre source.", "Nessun peer ha risposto. Riprova più tardi o usa un'altra fonte." },
    { "Background service is starting. Retry when startup finishes.", "O serviço em segundo plano está iniciando. Tente quando terminar.", "El servicio en segundo plano se está iniciando. Reinténtalo al terminar.", "Arka plan hizmeti başlıyor. Bitince tekrar dene.", "Le service d'arrière-plan démarre. Réessayez ensuite.", "Il servizio in background si sta avviando. Riprova al termine." },
    { "Could not fetch a valid torrent file in NULL_store. Check the network log and retry.", "Não foi possível obter um torrent válido. Veja o log de rede e tente de novo.", "No se pudo obtener un torrent válido. Revisa el registro de red y reinténtalo.", "Geçerli bir torrent dosyası alınamadı. Ağ günlüğüne bakıp tekrar dene.", "Impossible d'obtenir un torrent valide. Consultez le journal réseau et réessayez.", "Impossibile ottenere un torrent valido. Controlla il log di rete e riprova." },
    { "Download complete", "Download concluído", "Descarga completada", "İndirme tamamlandı", "Téléchargement terminé", "Download completato" },
    { "Could not fetch a torrent file. The source site may require a browser login.", "Não foi possível obter o arquivo torrent. O site pode exigir login no navegador.", "No se pudo obtener el archivo torrent. El sitio puede requerir iniciar sesión.", "Torrent dosyası alınamadı. Site tarayıcıda oturum açmayı gerektirebilir.", "Impossible d'obtenir le fichier torrent. Le site peut exiger une connexion.", "Impossibile ottenere il file torrent. Il sito potrebbe richiedere l'accesso." },
    { "Disk write failed. Check free space.", "Falha ao gravar no disco. Verifique o espaço livre.", "Error al escribir en el disco. Revisa el espacio libre.", "Diske yazılamadı. Boş alanı kontrol et.", "Échec d'écriture sur le disque. Vérifiez l'espace libre.", "Scrittura su disco non riuscita. Controlla lo spazio libero." },
    { "Torrent download did not complete.", "O download do torrent não terminou.", "La descarga del torrent no terminó.", "Torrent indirmesi tamamlanmadı.", "Le téléchargement du torrent n'est pas terminé.", "Il download del torrent non è terminato." },
    { "Duplicate torrent: using existing download #{0}", "Torrent duplicado: usando o download #{0}", "Torrent duplicado: se usa la descarga #{0}", "Yinelenen torrent: #{0} indirmesi kullanılıyor", "Torrent en double : téléchargement n°{0} utilisé", "Torrent duplicato: uso del download n. {0}" },
    { "Connecting to peers: {0} connected, {1} known", "Conectando a peers: {0} conectados, {1} conhecidos", "Conectando con pares: {0} conectados, {1} conocidos", "Eşlere bağlanılıyor: {0} bağlı, {1} bilinen", "Connexion aux pairs : {0} connectés, {1} connus", "Connessione ai peer: {0} connessi, {1} noti" },
    { "Finding peers (trackers: {0} ok, {1} failing; {2})", "Procurando peers (trackers: {0} ok, {1} com falha; {2})", "Buscando pares (trackers: {0} ok, {1} con fallos; {2})", "Eşler aranıyor (izleyiciler: {0} tamam, {1} hatalı; {2})", "Recherche de pairs (trackers : {0} ok, {1} en échec ; {2})", "Ricerca di peer (tracker: {0} ok, {1} con errori; {2})" },
    { "Waiting for peers to send data: {0} connected, {1} choking us", "Aguardando dados dos peers: {0} conectados, {1} bloqueando", "Esperando datos de los pares: {0} conectados, {1} bloqueando", "Eşlerden veri bekleniyor: {0} bağlı, {1} engelliyor", "En attente de données : {0} pairs connectés, {1} bloquants", "In attesa di dati: {0} peer connessi, {1} bloccano" },
    { "Verifying saved pieces: {0}%", "Verificando partes salvas: {0}%", "Verificando partes guardadas: {0}%", "Kayıtlı parçalar doğrulanıyor: %{0}", "Vérification des parties enregistrées : {0} %", "Verifica delle parti salvate: {0}%" },
    { "Checking files: {0}% (full scan)", "Verificando arquivos: {0}% (completo)", "Comprobando archivos: {0}% (completo)", "Dosyalar denetleniyor: %{0} (tam tarama)", "Vérification des fichiers : {0} % (complète)", "Verifica dei file: {0}% (completa)" },
    { "Existing data: {0}/{1} pieces verified", "Dados existentes: {0}/{1} partes verificadas", "Datos existentes: {0}/{1} partes verificadas", "Mevcut veri: {0}/{1} parça doğrulandı", "Données existantes : {0}/{1} parties vérifiées", "Dati esistenti: {0}/{1} parti verificate" },

    // Debrid
    { "Sending the torrent to {0}", "Enviando o torrent ao {0}", "Enviando el torrent a {0}", "Torrent {0} hizmetine gönderiliyor", "Envoi du torrent à {0}", "Invio del torrent a {0}" },
    { "{0} is downloading the torrent: {1}", "{0} está baixando o torrent: {1}", "{0} está descargando el torrent: {1}", "{0} torrent'i indiriyor: {1}", "{0} télécharge le torrent : {1}", "{0} sta scaricando il torrent: {1}" },
    { "Downloading from {0} ({1}): {2}", "Baixando do {0} ({1}): {2}", "Descargando de {0} ({1}): {2}", "{0} üzerinden indiriliyor ({1}): {2}", "Téléchargement depuis {0} ({1}) : {2}", "Download da {0} ({1}): {2}" },
    { "Downloaded from {0}", "Baixado do {0}", "Descargado de {0}", "{0} üzerinden indirildi", "Téléchargé depuis {0}", "Scaricato da {0}" },
    { "{0} refused the API key. Check it in Settings.", "O {0} recusou a chave de API. Confira em Configurações.", "{0} rechazó la clave API. Revísala en Ajustes.", "{0} API anahtarını reddetti. Ayarlar'dan kontrol et.", "{0} a refusé la clé API. Vérifiez-la dans Paramètres.", "{0} ha rifiutato la chiave API. Controllala in Impostazioni." },
    { "Could not reach {0}. Check the internet connection.", "Não foi possível acessar o {0}. Verifique a internet.", "No se pudo conectar con {0}. Revisa la conexión.", "{0} hizmetine ulaşılamadı. İnternet bağlantısını kontrol et.", "Impossible de joindre {0}. Vérifiez la connexion.", "Impossibile raggiungere {0}. Controlla la connessione." },
    { "The download from {0} was interrupted ({1}); retrying.", "O download do {0} foi interrompido ({1}); tentando de novo.", "La descarga de {0} se interrumpió ({1}); reintentando.", "{0} indirmesi kesildi ({1}); yeniden deneniyor.", "Le téléchargement depuis {0} a été interrompu ({1}) ; nouvelle tentative.", "Il download da {0} si è interrotto ({1}); nuovo tentativo." },
    { "{0} sent a file larger than it listed: {1}.", "O {0} enviou um arquivo maior que o informado: {1}.", "{0} envió un archivo más grande que el indicado: {1}.", "{0} belirttiğinden büyük bir dosya gönderdi: {1}.", "{0} a envoyé un fichier plus grand qu'annoncé : {1}.", "{0} ha inviato un file più grande di quello indicato: {1}." },
    { "{0} lost the transfer.", "O {0} perdeu a transferência.", "{0} perdió la transferencia.", "{0} aktarımı kaybetti.", "{0} a perdu le transfert.", "{0} ha perso il trasferimento." },
    { "{0} could not download this torrent ({1}).", "O {0} não conseguiu baixar este torrent ({1}).", "{0} no pudo descargar este torrent ({1}).", "{0} bu torrent'i indiremedi ({1}).", "{0} n'a pas pu télécharger ce torrent ({1}).", "{0} non è riuscito a scaricare questo torrent ({1})." },
    { "{0} could not download this torrent.", "O {0} não conseguiu baixar este torrent.", "{0} no pudo descargar este torrent.", "{0} bu torrent'i indiremedi.", "{0} n'a pas pu télécharger ce torrent.", "{0} non è riuscito a scaricare questo torrent." },
    { "Debrid is not set up. Choose a service and its API key in Settings.", "O Debrid não está configurado. Escolha o serviço e a chave em Configurações.", "Debrid no está configurado. Elige el servicio y su clave en Ajustes.", "Debrid ayarlanmadı. Ayarlar'dan hizmeti ve anahtarını seç.", "Debrid n'est pas configuré. Choisissez le service et sa clé dans Paramètres.", "Debrid non è configurato. Scegli il servizio e la chiave in Impostazioni." },
};
// clang-format on

Language gLanguage = Language::English;

const char* column(const Row& row, Language language)
{
    switch (language) {
    case Language::Portuguese: return row.pt;
    case Language::Spanish: return row.es;
    case Language::Turkish: return row.tr;
    case Language::French: return row.fr;
    case Language::Italian: return row.it;
    default: return row.en;
    }
}

const std::unordered_map<std::string, const Row*>& rows()
{
    static const std::unordered_map<std::string, const Row*> map = [] {
        std::unordered_map<std::string, const Row*> m;
        for (const Row& row : kRows) {
            m.emplace(row.en, &row);
        }
        return m;
    }();
    return map;
}

std::string fill(std::string text, const std::string* parts, int count)
{
    for (int i = 0; i < count; i++) {
        const std::string mark = "{" + std::to_string(i) + "}";
        for (size_t at = text.find(mark); at != std::string::npos; at = text.find(mark, at + parts[i].size())) {
            text.replace(at, mark.size(), parts[i]);
        }
    }
    return text;
}

// Matches `text` against a pattern with {n} marks; fills `parts`.
bool match(const std::string& pattern, const std::string& text, std::string parts[3])
{
    size_t p = 0, t = 0;
    while (p < pattern.size()) {
        const size_t mark = pattern.find('{', p);
        const std::string literal = pattern.substr(p, mark == std::string::npos ? std::string::npos : mark - p);
        if (text.compare(t, literal.size(), literal) != 0) return false;
        t += literal.size();
        if (mark == std::string::npos) return t == text.size();
        const int index = pattern[mark + 1] - '0';
        if (index < 0 || index > 2 || mark + 2 >= pattern.size() || pattern[mark + 2] != '}') return false;
        p = mark + 3;
        // The part ends where the next literal starts (its last occurrence when
        // nothing follows it, so the end of the text closes the pattern).
        const size_t next = pattern.find('{', p);
        const std::string after = pattern.substr(p, next == std::string::npos ? std::string::npos : next - p);
        size_t end;
        if (after.empty()) {
            if (next != std::string::npos) return false; // two marks in a row
            end = text.size();
        } else if (next == std::string::npos) {
            if (text.size() < after.size() || text.compare(text.size() - after.size(), after.size(), after) != 0) return false;
            end = text.size() - after.size();
        } else {
            end = text.find(after, t);
        }
        if (end == std::string::npos || end < t) return false;
        parts[index] = text.substr(t, end - t);
        t = end;
    }
    return t == text.size();
}

} // namespace

void setLanguage(Language language)
{
    gLanguage = language;
}

Language currentLanguage()
{
    return gLanguage;
}

Language languageFromConsole(int code)
{
    switch (code) {
    case 7:  // Portuguese (Portugal)
    case 17: // Portuguese (Brazil)
        return Language::Portuguese;
    case 3:  // Spanish
    case 20: // Spanish (Latin America)
        return Language::Spanish;
    case 19: return Language::Turkish;
    case 2:  // French
    case 22: // French (Canada)
        return Language::French;
    case 5: return Language::Italian;
    default: return Language::English;
    }
}

Language languageFromName(const std::string& name)
{
    if (name == "pt") return Language::Portuguese;
    if (name == "es") return Language::Spanish;
    if (name == "tr") return Language::Turkish;
    if (name == "fr") return Language::French;
    if (name == "it") return Language::Italian;
    return Language::English;
}

const char* tr(const char* english)
{
    if (gLanguage == Language::English || !english) return english;
    const auto found = rows().find(english);
    return found == rows().end() ? english : column(*found->second, gLanguage);
}

std::string tr(const std::string& english)
{
    if (gLanguage == Language::English) return english;
    const auto found = rows().find(english);
    return found == rows().end() ? english : std::string(column(*found->second, gLanguage));
}

std::string trf(const char* english, const std::string& a, const std::string& b, const std::string& c)
{
    const std::string parts[3] = { a, b, c };
    return fill(tr(english), parts, 3);
}

std::string trMessage(const std::string& english)
{
    if (gLanguage == Language::English || english.empty()) return english;
    const auto found = rows().find(english);
    if (found != rows().end()) return column(*found->second, gLanguage);
    for (const Row& row : kRows) {
        if (!strchr(row.en, '{')) continue;
        std::string parts[3];
        if (match(row.en, english, parts)) {
            // A part may itself be a known text ("3 updated / ..." inside a notice).
            for (std::string& part : parts) part = trMessage(part);
            return fill(column(row, gLanguage), parts, 3);
        }
    }
    return english;
}

} // namespace ns
