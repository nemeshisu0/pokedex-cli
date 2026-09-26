#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"

// Codici colore ANSI
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"

// 780 = base stat total più alto conosciuto tra le forme "standard"
// (es. Mega Rayquaza, Mega Mewtwo X/Y). Usato solo come riferimento visivo,
// modificalo pure se preferisci un altro valore.
#define BST_MAX 780

typedef enum { LANG_EN, LANG_IT } Lang;

// Struttura per memorizzare la risposta HTTP in memoria
struct MemoryStruct {
    char *memory;
    size_t size;
};

// Callback per libcurl per accumulare i dati ricevuti
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "Memoria insufficiente per realloc()\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// Esegue una richiesta HTTP GET generica e restituisce il corpo della
// risposta in memoria (binario-safe, quindi va bene anche per le immagini).
// Il chiamante deve fare free() del campo .memory della struct restituita.
struct MemoryStruct http_get(const char *url, long *http_code_out) {
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    CURL *curl_handle = curl_easy_init();
    if (!curl_handle) {
        fprintf(stderr, "Errore durante l'inizializzazione di curl\n");
        exit(1);
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "pokedex-cli/1.0");

    CURLcode res = curl_easy_perform(curl_handle);
    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        fprintf(stderr, "Errore connessione (%s): %s\n", url, curl_easy_strerror(res));
        free(chunk.memory);
        chunk.memory = NULL;
        chunk.size = 0;
    }

    if (http_code_out) *http_code_out = http_code;
    curl_easy_cleanup(curl_handle);
    return chunk;
}

// Piccolo helper per scegliere la stringa giusta in base alla lingua attiva.
static const char *L(Lang lang, const char *it, const char *en) {
    return lang == LANG_IT ? it : en;
}

// Etichette localizzate per i singoli stat (HP, Attacco, ecc.). Il confronto
// per determinare il colore della barra usa sempre il nome inglese originale
// restituito dall'API, che non cambia con la lingua.
static const char *stat_label(Lang lang, const char *stat_name) {
    if (lang == LANG_IT) {
        if (strcmp(stat_name, "hp") == 0) return "PS";
        if (strcmp(stat_name, "attack") == 0) return "Attacco";
        if (strcmp(stat_name, "defense") == 0) return "Difesa";
        if (strcmp(stat_name, "special-attack") == 0) return "Att. Speciale";
        if (strcmp(stat_name, "special-defense") == 0) return "Dif. Speciale";
        if (strcmp(stat_name, "speed") == 0) return "Velocita'";
        return stat_name;
    }
    if (strcmp(stat_name, "hp") == 0) return "HP";
    if (strcmp(stat_name, "attack") == 0) return "Attack";
    if (strcmp(stat_name, "defense") == 0) return "Defense";
    if (strcmp(stat_name, "special-attack") == 0) return "Sp. Atk";
    if (strcmp(stat_name, "special-defense") == 0) return "Sp. Def";
    if (strcmp(stat_name, "speed") == 0) return "Speed";
    return stat_name;
}

// Disegna una barra visiva per le statistiche (es. [██████░░░░] 85/200)
void print_stat_bar(const char *display_label, int value, const char *color) {
    int max_val = 200;
    int bar_width = 20;
    int filled = (value * bar_width) / max_val;
    if (filled > bar_width) filled = bar_width;

    printf("  %-16s %s[", display_label, color);
    for (int i = 0; i < filled; i++) printf("■");
    for (int i = filled; i < bar_width; i++) printf(" ");
    printf("]%s %3d\n", RESET, value);
}

// Scarica lo sprite in un file temporaneo e lo mostra a colori nel
// terminale usando 'chafa'. Se chafa non è installato o lo sprite non è
// disponibile, stampa un messaggio informativo invece di fallire.
void print_sprite(const char *sprite_url, Lang lang) {
    if (!sprite_url) {
        fprintf(stderr, YELLOW "%s\n" RESET,
                L(lang, "(sprite non disponibile per questo Pokémon)",
                        "(sprite not available for this Pokémon)"));
        return;
    }

    long code = 0;
    struct MemoryStruct img = http_get(sprite_url, &code);
    if (!img.memory || code != 200) {
        fprintf(stderr, YELLOW "%s\n" RESET, L(lang, "(sprite non disponibile)", "(sprite not available)"));
        free(img.memory);
        return;
    }

    char tmp_path[] = "/tmp/pokedex_sprite_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd == -1) {
        fprintf(stderr, "Impossibile creare file temporaneo per lo sprite\n");
        free(img.memory);
        return;
    }

    FILE *f = fdopen(fd, "wb");
    if (f) {
        fwrite(img.memory, 1, img.size, f);
        fclose(f);
    }
    free(img.memory);

    // chafa rileva il formato immagine dal contenuto, non serve estensione
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "chafa --size=32x16 '%s' 2>/dev/null", tmp_path);
    int ret = system(cmd);
    if (ret != 0) {
        printf(YELLOW "%s\n" RESET,
               L(lang, "(installa 'chafa' per vedere lo sprite: sudo pacman -S chafa)",
                       "(install 'chafa' to see the sprite: sudo pacman -S chafa)"));
    }

    remove(tmp_path);
}

// Recupera da un endpoint PokéAPI che espone un array "names" (es. /type/{id})
// il nome localizzato per il language_code richiesto (es. "it"). Se non lo
// trova, lascia invariato il valore di fallback già presente in 'out'.
void fetch_localized_name(const char *url, const char *language_code, char *out, size_t outsize) {
    long code = 0;
    struct MemoryStruct res = http_get(url, &code);
    if (!res.memory || code != 200) {
        free(res.memory);
        return;
    }

    cJSON *json = cJSON_Parse(res.memory);
    free(res.memory);
    if (!json) return;

    cJSON *names = cJSON_GetObjectItemCaseSensitive(json, "names");
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, names) {
        cJSON *lang_obj = cJSON_GetObjectItemCaseSensitive(entry, "language");
        cJSON *lang_name = cJSON_GetObjectItemCaseSensitive(lang_obj, "name");
        cJSON *name_val = cJSON_GetObjectItemCaseSensitive(entry, "name");

        if (lang_name && name_val && strcmp(lang_name->valuestring, language_code) == 0) {
            strncpy(out, name_val->valuestring, outsize - 1);
            out[outsize - 1] = '\0';
            break;
        }
    }

    cJSON_Delete(json);
}

// Recupera e stampa il flavor text (descrizione del Pokédex) nella lingua
// richiesta, interrogando l'endpoint /pokemon-species/. Se la lingua
// richiesta non ha una entry disponibile, ripiega sull'inglese.
void print_flavor_text(const char *species_url, Lang lang) {
    if (!species_url) return;

    const char *primary_lang = (lang == LANG_IT) ? "it" : "en";

    long code = 0;
    struct MemoryStruct species = http_get(species_url, &code);
    if (!species.memory || code != 200) {
        free(species.memory);
        return;
    }

    cJSON *json = cJSON_Parse(species.memory);
    free(species.memory);
    if (!json) return;

    cJSON *entries = cJSON_GetObjectItemCaseSensitive(json, "flavor_text_entries");
    cJSON *entry = NULL;
    cJSON *fallback_entry = NULL; // prima entry in inglese, usata come ripiego

    const char *found_text = NULL;

    cJSON_ArrayForEach(entry, entries) {
        cJSON *lang_obj = cJSON_GetObjectItemCaseSensitive(entry, "language");
        cJSON *lang_name = cJSON_GetObjectItemCaseSensitive(lang_obj, "name");
        cJSON *text = cJSON_GetObjectItemCaseSensitive(entry, "flavor_text");
        if (!lang_name || !text) continue;

        if (strcmp(lang_name->valuestring, primary_lang) == 0) {
            found_text = text->valuestring;
            break;
        }
        if (!fallback_entry && strcmp(lang_name->valuestring, "en") == 0) {
            fallback_entry = entry;
        }
    }

    if (!found_text && fallback_entry) {
        cJSON *text = cJSON_GetObjectItemCaseSensitive(fallback_entry, "flavor_text");
        if (text) found_text = text->valuestring;
    }

    if (found_text) {
        // La PokéAPI inserisce '\n' e '\f' nel testo: li normalizziamo in spazi
        char clean[512];
        size_t j = 0;
        for (size_t i = 0; found_text[i] != '\0' && j < sizeof(clean) - 1; i++) {
            char c = found_text[i];
            clean[j++] = (c == '\n' || c == '\f') ? ' ' : c;
        }
        clean[j] = '\0';

        printf(BOLD "%s:" RESET " %s\n\n", L(lang, "Descrizione", "Description"), clean);
    }

    cJSON_Delete(json);
}

int main(int argc, char *argv[]) {
    Lang lang = LANG_EN; // default: inglese
    const char *pokemon = NULL;

    // Parsing argomenti: il flag di lingua puo' stare prima o dopo il nome
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-it") == 0) {
            lang = LANG_IT;
        } else if (strcmp(argv[i], "-en") == 0) {
            lang = LANG_EN;
        } else if (!pokemon) {
            pokemon = argv[i];
        }
    }

    if (!pokemon) {
        fprintf(stderr, "Uso: %s <nome-pokemon> [-it|-en]\nEsempio: %s gengar -it\n", argv[0], argv[0]);
        return 1;
    }

    char url[256];
    snprintf(url, sizeof(url), "https://pokeapi.co/api/v2/pokemon/%s", pokemon);

    curl_global_init(CURL_GLOBAL_ALL);

    long http_code = 0;
    struct MemoryStruct chunk = http_get(url, &http_code);

    if (!chunk.memory) {
        free(chunk.memory);
        curl_global_cleanup();
        return 1;
    }

    if (http_code == 404) {
        printf(RED "%s '%s' %s\n" RESET,
               L(lang, "Errore: Pokémon", "Error: Pokémon"),
               pokemon,
               L(lang, "non trovato!", "not found!"));
        free(chunk.memory);
        curl_global_cleanup();
        return 1;
    }

    cJSON *json = cJSON_Parse(chunk.memory);
    if (!json) {
        fprintf(stderr, "Errore nel parsing dei dati JSON\n");
        free(chunk.memory);
        curl_global_cleanup();
        return 1;
    }

    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(json, "id");
    cJSON *name_item = cJSON_GetObjectItemCaseSensitive(json, "name");

    printf("\n" BOLD CYAN "=== POKÉDEX ENTRY #%d: %s ===" RESET "\n\n",
           id_item ? id_item->valueint : 0,
           name_item ? name_item->valuestring : pokemon);

    // --- Sprite ---
    cJSON *sprites = cJSON_GetObjectItemCaseSensitive(json, "sprites");
    cJSON *other = cJSON_GetObjectItemCaseSensitive(sprites, "other");
    cJSON *official_art = cJSON_GetObjectItemCaseSensitive(other, "official-artwork");
    cJSON *front_official = cJSON_GetObjectItemCaseSensitive(official_art, "front_default");
    cJSON *front_default = cJSON_GetObjectItemCaseSensitive(sprites, "front_default");

    const char *sprite_url = NULL;
    if (front_official && front_official->valuestring) sprite_url = front_official->valuestring;
    else if (front_default && front_default->valuestring) sprite_url = front_default->valuestring;

    print_sprite(sprite_url, lang);
    printf("\n");

    // --- Altezza e peso (l'API li restituisce in decimetri ed ettogrammi) ---
    cJSON *height_item = cJSON_GetObjectItemCaseSensitive(json, "height");
    cJSON *weight_item = cJSON_GetObjectItemCaseSensitive(json, "weight");
    double height_m = height_item ? height_item->valueint / 10.0 : 0.0;
    double weight_kg = weight_item ? weight_item->valueint / 10.0 : 0.0;

    printf(BOLD "%s:" RESET " %.1f m    " BOLD "%s:" RESET " %.1f kg\n\n",
           L(lang, "Altezza", "Height"), height_m,
           L(lang, "Peso", "Weight"), weight_kg);

    // --- Tipi (tradotti se lingua = it, con una chiamata extra per tipo) ---
    printf(BOLD "%s:" RESET " ", L(lang, "Tipi", "Types"));
    cJSON *types = cJSON_GetObjectItemCaseSensitive(json, "types");
    cJSON *type_entry = NULL;
    cJSON_ArrayForEach(type_entry, types) {
        cJSON *type_obj = cJSON_GetObjectItemCaseSensitive(type_entry, "type");
        cJSON *t_name = cJSON_GetObjectItemCaseSensitive(type_obj, "name");
        cJSON *t_url = cJSON_GetObjectItemCaseSensitive(type_obj, "url");
        if (!t_name) continue;

        char display_name[64];
        strncpy(display_name, t_name->valuestring, sizeof(display_name) - 1);
        display_name[sizeof(display_name) - 1] = '\0';

        if (lang == LANG_IT && t_url && t_url->valuestring) {
            fetch_localized_name(t_url->valuestring, "it", display_name, sizeof(display_name));
        }

        printf(YELLOW "[%s] " RESET, display_name);
    }
    printf("\n\n");

    // --- Descrizione (flavor text): richiede una seconda chiamata a /pokemon-species/ ---
    cJSON *species = cJSON_GetObjectItemCaseSensitive(json, "species");
    cJSON *species_url_item = cJSON_GetObjectItemCaseSensitive(species, "url");
    if (species_url_item && species_url_item->valuestring) {
        print_flavor_text(species_url_item->valuestring, lang);
    }

    printf(BOLD "%s:" RESET "\n", L(lang, "Statistiche Base", "Base Stats"));

    // --- Statistiche + calcolo del Base Stat Total (BST) ---
    cJSON *stats = cJSON_GetObjectItemCaseSensitive(json, "stats");
    cJSON *stat_entry = NULL;
    int bst_total = 0;

    cJSON_ArrayForEach(stat_entry, stats) {
        cJSON *base_stat = cJSON_GetObjectItemCaseSensitive(stat_entry, "base_stat");
        cJSON *stat_obj = cJSON_GetObjectItemCaseSensitive(stat_entry, "stat");
        cJSON *s_name = cJSON_GetObjectItemCaseSensitive(stat_obj, "name");

        if (base_stat && s_name) {
            const char *color = CYAN;
            if (strcmp(s_name->valuestring, "hp") == 0) color = GREEN;
            else if (strcmp(s_name->valuestring, "attack") == 0) color = RED;
            else if (strcmp(s_name->valuestring, "defense") == 0) color = BLUE;
            else if (strcmp(s_name->valuestring, "speed") == 0) color = MAGENTA;

            print_stat_bar(stat_label(lang, s_name->valuestring), base_stat->valueint, color);
            bst_total += base_stat->valueint;
        }
    }

    printf(BOLD "  %s:" RESET " %d / %d\n\n", L(lang, "Totale", "Total"), bst_total, BST_MAX);

    // Pulizia memoria
    cJSON_Delete(json);
    free(chunk.memory);
    curl_global_cleanup();

    return 0;
}
