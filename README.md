# pokedex-cli

A terminal-based Pokédex written in C. Fetches live data from [PokéAPI](https://pokeapi.co) and renders it in the terminal with ANSI colors and a rendered sprite (via [chafa](https://hpjansson.org/chafa/)).

```
=== POKÉDEX ENTRY #94: gengar ===

  (sprite rendered here by chafa)

Altezza: 1.5 m    Peso: 40.5 kg

Tipi: [ghost] [poison]

Descrizione: Under a full moon, this Pokémon likes to mimic the moves of the people it is hunting.

Statistiche Base:
  hp               [■■■■■■■             ]  60
  attack            [■■■■■■■■■           ]  65
  defense           [■■■■■■              ]  60
  speed             [■■■■■■■■■■■         ] 110
```

## Features

- Fetches live Pokémon data by name from PokéAPI
- Displays ID, name, types, height, weight and Pokédex flavor text
- Renders base stats as colored ANSI bars
- Shows the official artwork sprite directly in the terminal via `chafa`
- Graceful error handling (unknown Pokémon, network errors, missing `chafa`)

## Requirements

| Dependency | Purpose |
|---|---|
| [libcurl](https://curl.se/libcurl/) | HTTP requests to PokéAPI |
| [cJSON](https://github.com/DaveGamble/cJSON) | JSON parsing |
| [chafa](https://hpjansson.org/chafa/) | Terminal sprite rendering (optional, but recommended) |
| GCC / Clang | Build |

### Install on Arch Linux

```bash
sudo pacman -S curl chafa base-devel
```

`cJSON.h` / `cJSON.c` are vendored directly in this repository, so no separate cJSON package is required.

## Build

```bash
gcc main.c cJSON.c -o pokedex -lcurl
```

## Usage

```bash
./pokedex <pokemon-name>

# Example
./pokedex gengar
```

If `chafa` isn't installed, the sprite step is skipped with a friendly hint instead of a crash.

## Project structure

```
.
├── main.c       # application source
├── cJSON.c      # vendored JSON library (implementation)
├── cJSON.h      # vendored JSON library (header)
└── README.md
```

## Roadmap

- [ ] Support for multiple languages in flavor text (currently hardcoded to English)
- [ ] Evolution chain display
- [ ] Local caching to reduce redundant API calls
- [ ] Config file for default sprite size / color theme

## License

MIT — see [LICENSE](LICENSE) for details.

## Credits

Data provided by [PokéAPI](https://pokeapi.co). Sprite rendering by [chafa](https://hpjansson.org/chafa/). JSON parsing by [cJSON](https://github.com/DaveGamble/cJSON).
