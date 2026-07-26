# Roadcast

Roadcast e um broker local, read-only, de sinais do veiculo.

Seu objetivo e expor para multiplos aplicativos os dados que chegam ao VHAL, mas
nao sao publicados pelas APIs Android Automotive. O daemon le a memoria do VHAL
uma unica vez, mantem em RAM o estado completo conhecido do veiculo e distribui
esse estado para qualquer cliente conectado.

O projeto nasce a partir da investigacao feita no `vhalpeek`, mas nao e uma
extensao dele:

- `vhalpeek` continua sendo uma ferramenta de investigacao e diagnostico;
- Roadcast sera um servico de longa duracao, com protocolo estavel;
- os consumidores nao conhecem detalhes da memoria interna do VHAL;
- nenhum consumidor e tratado como o "app principal".

## Principios iniciais

- **Read-only:** Roadcast observa o veiculo; nao envia comandos ao VHAL ou ao CAN.
- **App-agnostic:** N aplicativos podem consumir o mesmo daemon.
- **Catalogo completo:** sinais uteis e aparentemente inuteis sao igualmente
  expostos.
- **Sem persistencia implicita:** snapshots, filas e transporte ficam em RAM.
- **Baixa latencia:** aquisicao alvo de 60 Hz, configuravel.
- **Consumidor lento nao bloqueia produtor:** clientes possuem filas limitadas.
- **Descoberta dinamica:** o daemon publica schema, IDs, tipos, unidades e origem.
- **Compatibilidade explicita:** protocolo, schema e implementacao possuem versoes
  independentes.

## Transporte inicial

O transporte principal sera um socket Unix abstrato, portanto sem arquivo de
socket no filesystem:

```text
@roadcast
```

Cada cliente recebe:

1. schema/catalogo;
2. snapshot completo inicial;
3. lotes incrementais com as entradas que mudaram;
4. heartbeats e numeros de sequencia para detectar perda ou congelamento.

Memoria compartilhada podera existir como fast path opcional, mas nao sera
necessaria para implementar um cliente Roadcast.

## Documentacao

- [Arquitetura](docs/ARCHITECTURE.md)
- [Protocolo](docs/PROTOCOL.md)
- [Decisoes](docs/DECISIONS.md)
- [Validation](docs/VALIDATION.md)

## Estado

The first executable tracer is implemented and validated. It provides:

- a libuv-based multi-client daemon;
- a deterministic in-memory fake source for host validation;
- explicit binary framing for raw CAN snapshots and delta batches;
- a generated catalog containing 815 decoded CAN signals;
- paged schema discovery with stable IDs and a canonical schema hash;
- consistent decoded-signal snapshots with subscription catch-up;
- a reference CLI client;
- protocol and two-client integration tests;
- Android ARM64/API 28 cross-compilation scripts.

The wire protocol is still experimental and is not frozen.

Inspect a decoded signal by name:

```bash
roadcastctl --socket @roadcast --signal VehicleSpeed --seconds 10
```

## Host build

Requirements:

- a C11 compiler;
- libuv discoverable through `pkg-config`;
- `libelf` headers on hosts that do not provide `elf.h`.

On macOS:

```bash
brew install libuv libelf
make test
make integration
```

The host tracer uses a filesystem Unix socket because macOS does not support the
Linux abstract Unix socket namespace. The socket is created inside a temporary
test directory and removed after the test.

## Android build

```bash
export ANDROID_NDK="$HOME/Library/Android/sdk/ndk/28.2.13676358"
scripts/build-libuv-android.sh
make android
```

An existing Android libuv installation can be reused:

```bash
LIBUV_PREFIX=/path/to/libuv-android make android
```
