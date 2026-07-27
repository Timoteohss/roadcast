# Arquitetura inicial

## Objetivo

Roadcast deve funcionar como uma segunda fonte de propriedades do veiculo:
completa, read-only e independente dos aplicativos consumidores.

Ele nao tenta se registrar como o VHAL oficial do Android. Em vez disso, oferece
um protocolo local proprio para dados que o VHAL OEM mantem em memoria, mas nao
publica por `CarPropertyManager`.

## Limites

Roadcast e responsavel por:

- localizar o processo e as estruturas relevantes do VHAL;
- ler frames CAN e a `VehiclePropertyStore`;
- decodificar e normalizar os dados conhecidos;
- preservar tambem dados crus e metadados de procedencia;
- manter um snapshot completo em RAM;
- distribuir schema, snapshots e mudancas para multiplos clientes;
- relatar vivacidade, perdas, capacidade e versoes.

Roadcast nao e responsavel por:

- alterar o VHAL;
- escrever properties;
- transmitir comandos CAN;
- persistir viagens ou telemetria;
- definir como cada app apresenta ou armazena os dados;
- inventar escala, unidade ou validade quando elas nao forem conhecidas.

## Visao geral

```text
┌─────────────────────────────────────────────────────────────┐
│ Processo VHAL OEM                                           │
│                                                             │
│  buffers CAN                  VehiclePropertyStore          │
└───────────────┬──────────────────────────┬──────────────────┘
                │ leitura de memoria       │
                ▼                          ▼
┌─────────────────────────────────────────────────────────────┐
│ roadcastd                                                   │
│                                                             │
│  source/vhal-reader                                         │
│       │                                                     │
│       ├── raw CAN frames                                    │
│       └── raw VHAL properties                               │
│                 │                                           │
│                 ▼                                           │
│  decode/catalog                                             │
│                 │                                           │
│                 ▼                                           │
│  canonical in-memory snapshot                               │
│                 │                                           │
│                 ▼                                           │
│  client/session manager                                     │
└───────────────┬──────────────────┬──────────────────────────┘
                │                  │
          @roadcast          @roadcast
                │                  │
          ┌─────▼─────┐      ┌─────▼─────┐
          │ Client A  │      │ Client B  │
          │ local RAM │      │ local RAM │
          └───────────┘      └───────────┘
```

## Fluxo de dados

### 1. Aquisicao

Um unico sampler le todas as fontes. O numero de clientes nao aumenta a
quantidade de leituras feitas no processo do VHAL.

A frequencia inicial desejada e 60 Hz, configuravel em runtime. Essa frequencia
e a cadencia de observacao do Roadcast, nao uma promessa de que todo ECU ou frame
CAN produzira valores novos a 60 Hz.

### 2. Decodificacao

Cada amostra pode gerar tres classes de entrada:

- frame CAN cru;
- sinal extraido de um frame;
- property encontrada na memoria do VHAL.

O dado cru nunca e descartado apenas porque existe uma representacao fisica
decodificada.

### 3. Snapshot canonico

O daemon mantem uma entrada por item do catalogo. Cada entrada contem, no minimo:

- ID estavel;
- indice do schema atual;
- valor cru;
- valor fisico, quando conhecido;
- timestamp de origem, quando existente;
- timestamp monotonic observado pelo daemon;
- estado de presenca e validade;
- contador da ultima alteracao;
- flags de calibracao e procedencia.

O snapshot existe apenas em RAM.

### 4. Distribuicao

Ao conectar, um cliente recebe o schema e um snapshot completo. Depois disso,
recebe lotes incrementais. Todos os sinais sao expostos; enviar apenas deltas nao
e filtragem, pois o cliente ja possui o estado completo.

O cliente pode solicitar uma ressincronizacao completa a qualquer momento.

## Isolamento entre clientes

O sampler nunca escreve diretamente em um socket bloqueante.

Cada sessao possui uma fila limitada em RAM. Quando um consumidor nao acompanha
a producao, o daemon pode descartar lotes intermediarios e marcar a sessao como
necessitando ressincronizacao. Um cliente lento nao pode:

- atrasar a leitura do VHAL;
- aumentar memoria sem limite;
- prejudicar outros clientes;
- obrigar o daemon a gravar backlog em disco.

## Runtime and transport implementation

`roadcastd` uses [libuv](https://github.com/libuv/libuv) as its runtime
infrastructure. libuv owns:

- the event loop;
- accepted client sessions;
- asynchronous reads and writes;
- timers and Unix signal handling;
- per-stream write queue observation;
- orderly connection shutdown.

The acquisition path remains independent from the libuv loop. It publishes
normalized changes into a bounded in-memory handoff so that socket activity
cannot delay VHAL sampling.

Roadcast uses the Linux abstract Unix socket `@roadcast`. libuv does not bind
abstract namespace sockets through `uv_pipe_bind()`, so a small Linux-specific
adapter creates and binds the listening socket with `socket()` and `bind()`.
The resulting descriptor is adopted by libuv with `uv_pipe_open()`. This adapter
is the only transport-specific socket setup that Roadcast implements directly.

Each client has a bounded logical queue. libuv's stream write queue metrics are
used as an additional signal for detecting slow consumers; they do not replace
the protocol-level resynchronization and discard policy.

Roadcast does not combine libuv with another event library.

## Serialization boundary

The high-frequency data path uses Roadcast's own explicit binary framing and
fixed-width delta records. This keeps decoding deterministic, avoids allocation
in the common path, and allows a client to implement the protocol without
linking a Roadcast-specific dependency.

[FlatBuffers](https://github.com/google/flatbuffers) is the preferred candidate
for catalog, schema, and control payloads when generated schema bindings become
necessary. It is not required for the first tracer and is not used to wrap each
small CAN or property delta.

The daemon must not introduce a FlatBuffers dependency through an unofficial C
binding without a separate decision. The preferred adoption path is to keep the
VHAL reader boundary in C and compile the protocol/catalog boundary as C++11 or
newer, using the official FlatBuffers C++ implementation.

The following are intentionally not part of the baseline architecture:

- NNG or ZeroMQ as the client transport;
- gRPC as the local telemetry protocol;
- protobuf-c or nanopb for high-frequency delta records;
- libevent alongside libuv;
- runtime DBC parsing when a generated catalog is sufficient.

## Vivacidade e consistencia

O daemon publica:

- `sample_seq`: incrementado a cada ciclo de aquisicao;
- `change_seq`: incrementado a cada lote de mudancas;
- `sample_time_ns`: instante monotonic do ultimo ciclo;
- hash e versao do schema;
- contadores de lotes perdidos por cliente.

Um cliente que detectar salto de sequencia solicita novo snapshot.

## Frequencia e gauges

O alvo de 60 Hz reduz latencia e permite que clientes renderizem no ritmo da
tela. Entretanto, um sinal produzido pelo carro a 10 Hz continuara mudando a
10 Hz. Interpolacao visual e smoothing pertencem ao consumidor, pois o daemon
deve preservar a medicao real e seu timestamp.

## Ausencia de pressao de disco

Durante operacao normal, Roadcast nao cria:

- arquivo de snapshot;
- arquivo de socket;
- journal proprio;
- fila persistente;
- dump periodico;
- banco de dados.

Logs de alta frequencia tambem sao proibidos. Diagnostico deve usar contadores
consultaveis e mensagens esparsas. A forma de instalar o executavel e externa a
essa garantia; ela se aplica aos dados produzidos em runtime.

## Current source layout

The first tracer deliberately keeps a small number of modules while preserving
the documented responsibility boundaries:

```text
roadcast/
├── README.md
├── Makefile
├── docs/
│   ├── ARCHITECTURE.md
│   ├── DECISIONS.md
│   ├── PROTOCOL.md
│   └── VALIDATION.md
├── include/
│   ├── roadcast_frames.h
│   ├── roadcast_protocol.h
│   └── roadcast_vhal.h
├── scripts/
│   ├── build-android.sh
│   └── build-libuv-android.sh
├── src/
│   ├── protocol.c
│   ├── roadcastctl.c
│   ├── roadcastd.c
│   └── vhal_source.c
└── tests/
    ├── integration.sh
    └── test_protocol.c
```

`roadcastd.c` currently contains the sampler handoff and server/session runtime.
These responsibilities should move into separate modules when the next tracer
requires independent snapshot or server tests. Files must be extracted around
testable ownership boundaries, not merely to match an aspirational directory
diagram.
