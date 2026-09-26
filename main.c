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

// Disegna una barra visiva per le statistiche (es. [██████░░░░] 85/200)
void print_stat_bar(const char *label, int value, const char *color) {
    int max_val = 200;
    int bar_width = 20;
    int filled = (value * bar_width) / max_val;
    if (filled > bar_width) filled = bar_width;

    printf("  %-16s %s[", label, color);
    for (int i = 0; i < filled; i++) printf("■");
    for (int i = filled; i < bar_width; i++) printf(" ");
    printf("]%s %3d\n", RESET, value);
}

// Scarica lo sprite in un file temporaneo e lo mostra a colori nel
// terminale usando 'chafa'. Se chafa non è installato o lo sprite non è
// disponibile, stampa un messaggio informativo invece di fallire.
void print_sprite(const char *sprite_url) {
    if (!sprite_url) {
        fprintf(stderr, YELLOW "(sprite non disponibile per questo Pokémon)\n" RESET);
        return;
    }

    long code = 0;
    struct MemoryStruct img = http_get(sprite_url, &code);
    if (!img.memory || code != 200) {
        fprintf(stderr, YELLOW "(sprite non disponibile)\n" RESET);
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
        printf(YELLOW "(installa 'chafa' per vedere lo sprite: sudo pacman -S chafa)\n" RESET);
    }

    remove(tmp_path);
}

// Recupera e stampa il flavor text (descrizione del Pokédex) in inglese
// interrogando l'endpoint /pokemon-species/.
void print_flavor_text(const char *species_url) {
    if (!species_url) return;

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
    cJSON_ArrayForEach(entry, entries) {
        cJSON *lang = cJSON_GetObjectItemCaseSensitive(entry, "language");
        cJSON *lang_name = cJSON_GetObjectItemCaseSensitive(lang, "name");
        cJSON *text = cJSON_GetObjectItemCaseSensitive(entry, "flavor_text");

        if (lang_name && text && strcmp(lang_name->valuestring, "en") == 0) {
            // La PokéAPI inserisce '\n' e '\f' nel testo: li normalizziamo in spazi
            char clean[512];
            size_t j = 0;
            for (size_t i = 0; text->valuestring[i] != '\0' && j < sizeof(clean) - 1; i++) {
                char c = text->valuestring[i];
                clean[j++] = (c == '\n' || c == '\f') ? ' ' : c;
            }
            clean[j] = '\0';

            printf(BOLD "Descrizione:" RESET " %s\n\n", clean);
            break;
        }
    }

    cJSON_Delete(json);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <nome-pokemon>\nEsempio: %s gengar\n", argv[0], argv[0]);
        return 1;
    }

    char *pokemon = argv[1];
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
        printf(RED "Errore: Pokémon '%s' non trovato!\n" RESET, pokemon);
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

    print_sprite(sprite_url);
    printf("\n");

    // --- Altezza e peso (l'API li restituisce in decimetri ed ettogrammi) ---
    cJSON *height_item = cJSON_GetObjectItemCaseSensitive(json, "height");
    cJSON *weight_item = cJSON_GetObjectItemCaseSensitive(json, "weight");
    double height_m = height_item ? height_item->valueint / 10.0 : 0.0;
    double weight_kg = weight_item ? weight_item->valueint / 10.0 : 0.0;

    printf(BOLD "Altezza:" RESET " %.1f m    " BOLD "Peso:" RESET " %.1f kg\n\n", height_m, weight_kg);

    // --- Tipi ---
    printf(BOLD "Tipi:" RESET " ");
    cJSON *types = cJSON_GetObjectItemCaseSensitive(json, "types");
    cJSON *type_entry = NULL;
    cJSON_ArrayForEach(type_entry, types) {
        cJSON *type_obj = cJSON_GetObjectItemCaseSensitive(type_entry, "type");
        cJSON *t_name = cJSON_GetObjectItemCaseSensitive(type_obj, "name");
        if (t_name) {
            printf(YELLOW "[%s] " RESET, t_name->valuestring);
        }
    }
    printf("\n\n");

    // --- Descrizione (flavor text): richiede una seconda chiamata a /pokemon-species/ ---
    cJSON *species = cJSON_GetObjectItemCaseSensitive(json, "species");
    cJSON *species_url_item = cJSON_GetObjectItemCaseSensitive(species, "url");
    if (species_url_item && species_url_item->valuestring) {
        print_flavor_text(species_url_item->valuestring);
    }

    printf(BOLD "Statistiche Base:" RESET "\n");

    // --- Statistiche ---
    cJSON *stats = cJSON_GetObjectItemCaseSensitive(json, "stats");
    cJSON *stat_entry = NULL;
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

            print_stat_bar(s_name->valuestring, base_stat->valueint, color);
        }
    }
    printf("\n");

    // Pulizia memoria
    cJSON_Delete(json);
    free(chunk.memory);
    curl_global_cleanup();

    return 0;
}
