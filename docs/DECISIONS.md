# Decisoes de arquitetura

Registro inicial das decisoes tomadas durante o desenho. Este arquivo sera
dividido em ADRs individuais se o volume ou a necessidade de historico crescer.

## D-001: projeto independente do vhalpeek

**Estado:** aceita.

Roadcast sera um projeto novo. O `vhalpeek` preserva seu papel de ferramenta de
engenharia reversa e diagnostico. Codigo de leitura comprovado pode ser extraido
ou reutilizado, mas o protocolo de producao nao sera adicionado implicitamente a
uma CLI monolitica.

## D-002: servidor app-agnostic

**Estado:** aceita.

Nenhum app possui o snapshot ou controla o ciclo de vida do daemon. Todas as
sessoes possuem os mesmos direitos de leitura, sujeitos a uma politica de acesso
que ainda sera definida.

## D-003: catalogo completo

**Estado:** aceita.

Roadcast nao seleciona apenas sinais considerados uteis. Frames, sinais
decodificados e properties observaveis entram no catalogo, inclusive quando
escala ou significado ainda nao estiverem confirmados.

Incerteza sera representada por metadados; nao por exclusao silenciosa.

## D-004: runtime sem persistencia

**Estado:** aceita.

O daemon nao grava snapshots, backlog ou telemetria. Estado operacional, filas e
transporte permanecem em RAM. Persistencia e uma escolha de cada consumidor.

## D-005: 60 Hz configuravel

**Estado:** aceita como alvo inicial.

O sampler tera alvo de 60 Hz, com configuracao disponivel. A frequencia efetiva
sera medida e exposta. Frequencia de amostragem nao sera confundida com a
frequencia real de publicacao de cada ECU.

## D-006: socket Unix abstrato como transporte base

**Estado:** aceita para o primeiro tracer.

O socket abstrato evita arquivo no filesystem e ja foi comprovado no aparelho
durante a investigacao do `vhalpeek`. O protocolo base nao dependera de memoria
compartilhada.

Loopback TCP permanece uma alternativa de compatibilidade caso testes com apps
em outros dominios encontrem restricoes de conexao ao socket Unix.

## D-007: snapshot inicial seguido de deltas

**Estado:** aceita.

Todo cliente recebe estado completo ao conectar e depois somente entradas
alteradas. Isso expoe todo o catalogo sem retransmitir milhares de entradas
paradas a cada ciclo.

## D-008: nenhum cliente bloqueia o sampler

**Estado:** aceita.

Filas por cliente sao limitadas. Sob atraso, o daemon privilegia o estado atual,
marca descontinuidade e exige ressincronizacao. Backpressure nunca alcanca a
thread de aquisicao.

## D-009: shared memory nao e requisito

**Estado:** aceita.

Memfd ou ashmem podem ser adicionados como fast path. O cliente mais simples
precisa apenas implementar o protocolo de socket.

Um futuro fd adotado por cliente nao substituira um buffer global, pois isso
congelaria consumidores existentes.

## D-010: filtragem inicial pertence ao cliente

**Estado:** aceita para o primeiro protocolo.

As origens tecnicas iniciais sao frames CAN crus, sinais CAN decodificados e
properties da `VehiclePropertyStore`. Nomes como PEPS, BCM, BMSH e VCU descrevem
o produtor ou dominio semantico; nao sao transportes mutuamente exclusivos. Um
sinal PEPS pode existir no CAN e ser republicado como property pelo VHAL.

O schema identificara `kind`, origem e dominio para permitir filtragem local.
O servidor enviara snapshot completo e deltas completos por default. Subscriptions
server-side por origem ou dominio so serao adicionadas se medidas mostrarem custo
relevante, ou se surgirem requisitos de acesso diferentes por classe de dado.

Essa escolha evita estado e ressincronizacao adicionais por sessao. Medicoes no
carro em 2026-07-26 mostraram, a 120 Hz, aproximadamente 2,8 frames, 0,4 sinais e
0,4 properties alterados por tick, embora o catalogo total tenha 111 frames, 815
sinais e 1804 properties.

## D-011: libuv runtime with a small native wire protocol

**Status:** accepted.

Roadcast uses libuv for the event loop, timers, signal handling, client
lifecycle, asynchronous I/O, and write queue observation. The project does not
implement a growing custom `poll()` loop, partial-write scheduler, or socket
lifecycle framework.

The Linux abstract socket still requires a narrow native adapter because
`uv_pipe_bind()` does not bind abstract namespace addresses. Roadcast creates
and binds `@roadcast` with the Linux socket API, then adopts the descriptor in
libuv. This exception does not justify a second event library.

The hot data path retains an explicit Roadcast message header and fixed-width
delta records. This wire format is intentionally simple enough for consumers to
implement without installing libuv or a Roadcast SDK.

FlatBuffers is the preferred future representation for catalog, schema, and
control payloads, where schema evolution and generated Dart/Kotlin/C++ bindings
provide concrete value. It will not wrap every small telemetry delta. Adopting
FlatBuffers in the daemon requires either an official supported C path or a
C++ protocol boundary; adding an unofficial C binding implicitly is rejected.

NNG, ZeroMQ, gRPC, protobuf-c, nanopb, and libevent are not baseline
dependencies. They may only be reconsidered when a measured requirement cannot
be met by libuv and the existing protocol.

## D-012: generated CAN catalog and protocol version 2

**Status:** accepted.

`data/dbc.json` is the source of truth for the initial 111 raw CAN frames and
815 decoded signals. A deterministic generator emits the compiled frame list,
bit layout, metadata, stable IDs, and canonical schema hash. The daemon performs
no JSON or DBC parsing at runtime.

A CAN signal stable ID is FNV-1a 64 over:

```text
"roadcast.can.signal.v1" NUL can_id_be16 signal_name_utf8
```

Including the CAN ID distinguishes equal names such as `Diag_req` on `0x7C1`
and `0x7DF`. Bit layout, width, signedness, scale, offset, unit, calibration,
and invalid-signal relationship affect the schema hash but not semantic
identity.

Raw signal values are 64-bit because the catalog contains 64-bit diagnostic
signals. Physical values use IEEE-754 binary64.

Schema and signal snapshots use client-requested pages bounded by the existing
message limit. A per-client frozen snapshot keeps all signal pages at one
`sample_seq`. Before acknowledging `SUBSCRIBE_ALL`, the daemon sends a bounded
catch-up delta from that frozen snapshot to current state. This prevents a
client from losing changes that occur while it retrieves pages.

Protocol version 2 introduces schema discovery, decoded signal snapshots, and
decoded signal delta batches. FlatBuffers remains deferred: the current
generated fixed format meets the C, Dart, and Kotlin compatibility boundary
without a runtime serialization dependency.

## D-013: bounded local session setup

**Status:** accepted.

The operating system remains the authority for whether a process may connect to
the Roadcast socket. The daemon reads immutable peer credentials from the
accepted socket and uses the peer UID for resource accounting. A UID may hold at
most eight concurrent sessions.

An accepted client has two seconds to send a valid `HELLO` and ten seconds after
`WELCOME` to complete snapshot setup and subscribe. A client that needs
resynchronization has the same bounded setup window. Fully subscribed clients
have no inbound idle timeout because a read-only consumer is not required to
send periodic traffic.

Peer credentials and quotas are availability controls, not a complete
authorization mechanism. The production UID allowlist, if one is required,
must be decided after validating the actual app and daemon execution domains on
an enforcing vehicle.

## D-014: latest-snapshot sampler handoff

**Status:** accepted.

The sampler publishes one latest frame snapshot through a short mutex-protected
copy, then notifies the libuv loop. It never waits for client queues or socket
I/O. The event loop may coalesce multiple notifications and consume only the
newest snapshot; sequence deltas preserve the number of elapsed samples and the
daemon reports intermediate coalesced samples.

Publishing does not use `pthread_mutex_trylock`: lock contention must not
silently discard a completed source read. A ring buffer is unnecessary while
the product contract promises current state rather than every intermediate
sample.

## Questoes abertas

- formato binario exato e endianness;
- limites de fila e politica de descarte;
- production authorization policy beyond peer-credential resource accounting;
- conditions that would justify replacing schema pages with FlatBuffers;
- API do SDK C;
- estrategia de teste sem depender do carro;
- mecanismo definitivo de instalacao e inicializacao do daemon;
- quais partes do leitor atual podem ser extraidas sem acoplar Roadcast ao
  layout interno do `vhalpeek`.
