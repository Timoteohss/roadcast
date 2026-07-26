# Protocolo Roadcast

## Estado do documento

Este documento define a direcao inicial. Valores numericos, layouts C e nomes de
mensagens ainda nao sao contrato estavel.

## Objetivos

O protocolo deve:

- funcionar sobre socket Unix abstrato;
- aceitar multiplos clientes simultaneos;
- ser implementavel sem SDK especifico;
- usar framing binario inequivoco;
- permitir descoberta de todos os sinais;
- transmitir snapshot completo e atualizacoes incrementais;
- detectar incompatibilidade e perda de mensagens;
- permitir extensao sem quebrar clientes antigos.

## Separacao de versoes

Tres numeros nao devem ser confundidos:

- **protocol version:** framing, mensagens e semantica da conexao;
- **schema version/hash:** catalogo e ordem das entradas;
- **daemon version:** versao da implementacao Roadcast.

Atualizar o catalogo nao implica necessariamente atualizar o protocolo.

## Sessao inicial

Protocol version 3 session flow:

```text
client                              roadcastd
  │                                    │
  ├── HELLO protocol=[2..2] ──────────►│
  │◄─ WELCOME protocol=2 capabilities ─┤
  │                                    │
  ├── GET_SCHEMA start=0 ─────────────►│
  │◄─ SCHEMA_CHUNK start=0 count=N ────┤
  ├── GET_SCHEMA start=N ─────────────►│
  │◄─ SCHEMA_CHUNK ... ────────────────┤
  │                                    │
  ├── GET_SNAPSHOT start=0 ───────────►│
  │◄─ SNAPSHOT start=0 count=N ────────┤
  ├── GET_SNAPSHOT start=N ───────────►│
  │◄─ SNAPSHOT ... ────────────────────┤
  ├── GET_SIGNAL_SNAPSHOT start=0 ────►│
  │◄─ SIGNAL_SNAPSHOT_CHUNK ───────────┤
  ├── GET_SIGNAL_SNAPSHOT ... ────────►│
  │◄─ SIGNAL_SNAPSHOT_CHUNK ... ───────┤
  │                                    │
  ├── SUBSCRIBE_ALL ──────────────────►│
  │◄─ UPDATE_BATCH catch-up ───────────┤
  │◄─ SIGNAL_UPDATE_BATCH catch-up ────┤
  │◄─ SUBSCRIBED ──────────────────────┤
  │◄─ UPDATE_BATCH ────────────────────┤
  │◄─ SIGNAL_UPDATE_BATCH ─────────────┤
  │◄─ UPDATE_BATCH ────────────────────┤
  │◄─ HEARTBEAT ───────────────────────┤
```

Raw-frame, schema, and decoded-signal snapshots are pulled by index. Each
response remains within the negotiated response payload limit. The frame and
signal snapshots are frozen together per client; all pages carry the same
`sample_seq` and change sequence.

Changes that happen during snapshot transfer are not lost. `SUBSCRIBE_ALL`
first queues the difference between the frozen snapshot and current canonical
state, then queues `SUBSCRIBED`. Live updates follow that acknowledgement in
stream order.

## Framing

Cabecalho conceitual:

```c
struct roadcast_message_header {
    uint32_t magic;
    uint16_t protocol_version;
    uint16_t message_type;
    uint32_t flags;
    uint32_t payload_bytes;
    uint64_t sequence;
    uint64_t timestamp_ns;
};
```

Protocol version 3 encodes the 32-byte header in network byte order:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic |
| 4 | 2 | protocol version |
| 6 | 2 | message type |
| 8 | 4 | flags |
| 12 | 4 | payload bytes |
| 16 | 8 | change sequence |
| 24 | 8 | monotonic sample timestamp in nanoseconds |

Requisitos:

- inteiros possuem endianness definida pelo protocolo;
- `payload_bytes` nao inclui o cabecalho;
- tamanho maximo de mensagem e anunciado no handshake;
- mensagens desconhecidas podem ser ignoradas somente quando marcadas como
  opcionais;
- payload malformado encerra apenas a sessao do cliente.

The framing header and high-frequency delta records are native Roadcast wire
types, not FlatBuffers or Protocol Buffers envelopes. Their serialized layout
must be defined field by field; an in-memory C struct must never be sent
directly when compiler padding or host endianness could affect the result.

This fixed representation is part of the public protocol and does not require
clients to link libuv. libuv is a daemon implementation detail.

`WELCOME` announces two directional limits:

- `max_request_payload`: the largest payload the daemon input buffer accepts;
- `max_response_payload`: the largest payload the daemon will emit.

The current daemon announces 4064 and 2016 bytes respectively. These are
transport limits, not permission to use arbitrary payload sizes: each command
still has an exact payload contract.

Frame and signal batches begin with this 20-byte prefix:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | sample sequence |
| 8 | 4 | total catalog count for this kind |
| 12 | 4 | page start index, or `UINT32_MAX` for a delta batch |
| 16 | 2 | record count |
| 18 | 2 | reserved, zero |

Snapshot pages must be contiguous and retain one sample and change sequence.
Delta records may be split across multiple messages carrying the same change
sequence.

## Schema

Cada entrada do catalogo precisa descrever sua identidade e procedencia.

Campos conceituais:

```text
stable_id
index
kind                 frame | signal | property
namespace
name
source
raw_type
physical_type
unit
scale
offset
calibrated
update_mode
source_address       CAN id, property id/area ou equivalente
```

O schema deve permitir representar informacao desconhecida sem inventar valores.
Por exemplo, unidade ausente e diferente de unidade vazia confirmada.

Protocol version 3 uses an explicitly versioned generated binary schema. Each
entry carries stable ID, index, invalid-signal index, CAN ID, kind, source,
width, signed/calibrated flags, scale, offset, name, and unit.

FlatBuffers remains a possible future encoding for schema and control payloads.
It is not used by protocol version 3 and will not wrap telemetry deltas.

## IDs e indices

`stable_id` identifica semanticamente uma entrada entre schemas compativeis.
`index` e apenas a posicao compacta no snapshot atual.

Clientes usam indices no caminho quente, mas devem invalida-los quando o hash do
schema mudar.

CAN signal IDs use FNV-1a 64 over the versioned namespace, big-endian 16-bit CAN
ID, and UTF-8 signal name. The schema hash additionally covers numeric
interpretation and bit layout.

O schema tambem distingue:

- `kind`: frame CAN, sinal CAN decodificado ou VHAL property;
- `source`: mecanismo tecnico pelo qual o Roadcast obteve o dado;
- `domain`: produtor ou agrupamento semantico inferido, como PEPS, BCM ou BMSH.

`domain` nao substitui `source`: um valor PEPS pode aparecer tanto como sinal CAN
quanto como property republicada pelo VHAL.

## Snapshot

O snapshot representa o estado completo conhecido em uma sequencia especifica.

Cada valor deve distinguir:

- nunca observado;
- observado e invalido;
- observado e valido;
- cru disponivel, fisico desconhecido;
- fisico calculado com escala estimada;
- fisico calculado com escala calibrada.

Decoded CAN values contain a 64-bit raw value, an IEEE-754 binary64 physical
value, first-observed and last-change monotonic timestamps, an observation
state, and a calibration flag.

Protocol version 3 observation states are:

| Value | Meaning |
|---:|---|
| 0 | source unavailable or the frame could not be read |
| 1 | source resolved, but no transition has been observed since daemon start |
| 2 | observed and valid |
| 3 | observed but invalid according to an explicit catalog invalid flag |

Raw frames use states 0 through 2. Signals may also use state 3. A non-zero
memory value at daemon startup is a baseline, not proof that Roadcast observed
the frame arrive. A frame becomes observed when its eight source bytes first
change after that baseline. Therefore a frame that remains constant for the
entire daemon lifetime honestly remains `never observed`.

`calibrated` describes whether scale, offset, and unit are confirmed. It is
independent from observation validity.

## Atualizacoes

Um `UPDATE_BATCH` contem todas as entradas alteradas em um ciclo ou em uma
pequena janela de agregacao.

O servidor nao promete entregar todo estado intermediario para um cliente lento.
Ele promete:

- preservar o estado mais recente;
- sinalizar perda de continuidade;
- permitir nova sincronizacao;
- nunca bloquear o sampler por causa do cliente.

Clientes que precisem registrar cada transicao deverao negociar uma capacidade
especifica, com limites de memoria claramente anunciados.

O protocolo inicial envia todos os deltas e deixa filtragem por `kind`, `source`
ou `domain` para o cliente. Filtros server-side sao uma extensao futura, nao uma
condicao para expor o catalogo completo.

## Heartbeat

Heartbeats exist even when no signal changes. Protocol version 3 carries:

- latest sample sequence;
- latest change sequence, equal to the header sequence;
- batches dropped for this client;
- sampler observations coalesced by the event-loop handoff;
- effective recent sampling frequency in milliHertz;
- source state: unavailable, available, or degraded fallback.

The header timestamp is the monotonic timestamp of the latest source sample.

Assim, valor parado nao e confundido com daemon morto.

## Memoria compartilhada opcional

O protocolo podera anunciar um fast path por memoria compartilhada. Ele deve ser
opcional: todo dado e toda operacao de controle precisam continuar acessiveis
pelo transporte base.

Se implementado sem politica SELinux dedicada, cada fd fornecido por um cliente
representara uma view exclusiva daquele cliente. Ele nunca substituira o
snapshot canonico ou o buffer de outro consumidor.
