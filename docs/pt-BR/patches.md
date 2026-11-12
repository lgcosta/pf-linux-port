# Conjunto de patches de core do Linux

English: [docs/en/patches.md](../en/patches.md).

## Status atual: VAZIO

O port exige **zero modificações no core do Linux 7.3.4**. O design
(spec §5) previa um patch obrigatório — **P1: exportar símbolos usados pelo
módulo PF** — mas o compile-port do M1 provou que ele é desnecessário: o
modpost reportou **nenhum símbolo indefinido** para o `pf.ko`. A camada de
compat foi escrita contra APIs que o kernel puro já exporta
(`__inet_lookup_established`, `__inet_lookup_listener`,
`__inet6_lookup_established`, `inet6_lookup_listener`, `__udp4_lib_lookup`,
`__udp6_lib_lookup`, `ip_route_output_flow`/`ip6_route_output_flags` via
wrappers inline, `icmp_send`, …). A evidência e a lista exata estão em
`patches/kernel/p1-needs.txt`, que também traz uma **watch list**: por
exemplo, `tcp_v4_send_reset` só precisaria de export se helpers de RST de
userland forem usados.

Portanto, `patches/kernel/` não contém nenhum `000N-*.patch` hoje, e aplicar
o conjunto (vazio) é um no-op.

> Nota honesta: `scripts/apply-patches.sh`, `scripts/unpatch.sh` e
> `patches/kernel/series` **estão na árvore**. O `series` é vazio por projeto,
> então aplicá-lo é um no-op verificado (`PATCHES_APPLIED=0`). Quando um patch
> real for necessário, o commit que o adiciona também atualiza o `series` e
> as regras abaixo.

## O mecanismo (como o conjunto deve funcionar)

- Toda mudança no core do kernel é um arquivo independente
  `patches/kernel/000N-descricao-curta.patch`, listado em ordem em
  `patches/kernel/series`, aplicado à árvore pristina com ferramenta no
  estilo `git apply` e revertido do mesmo jeito (`unpatch.sh` aplica ao
  contrário).
- A árvore nunca pode ficar suja em silêncio: builds reproduzíveis aplicam,
  compilam e registram o estado (`out/.config`, `out/kernelversion`).
- Regras obrigatórias para qualquer patch futuro:
  1. **Aditivo, zero mudança de comportamento** (por exemplo, linhas
     `EXPORT_SYMBOL_GPL()` junto aos clusters de export existentes). Qualquer
     coisa que altere lógica exige antes uma emenda ao design.
  2. Uma preocupação por patch, numerado em sequência — nunca editar um
     patch já aceito; substitua com um novo número.
  3. Uma seção neste arquivo (nos dois idiomas) **antes** do patch entrar:
     o quê/por quê/impacto/reversão + a evidência em VM.
  4. O patch tem de sobreviver a `make -C <kerneldir> bzImage modules` a
     partir de árvore limpa (CI da bancada: `vm-run-tests.sh` depois de um
     boot novo).

## Template para uma futura entrada `000N`

Copie este bloco para `docs/en/patches.md` e `docs/pt-BR/patches.md` (mesma
âncora) ao adicionar `000N-*.patch`:

```
### 000N — <título em uma linha>

- Arquivo: `patches/kernel/000N-<slug>.patch`
- Status: aplicado (kernel 7.3.4) / substituído por 000M
- Toca: <arquivo(s) da árvore do kernel>, <n> linhas adicionadas, 0 removidas
- O quê: <símbolos / hooks / opções de Kconfig adicionados, exatos>
- Por quê: <qual código do PF-on-Linux precisa; por que nenhuma API
  exportada basta>
- Impacto: <impacto em runtime/ABI/memória no Linux puro; esperado: nenhum>
- Reversão: `./scripts/unpatch.sh <kerneldir>` (aplicação reversa); o módulo
  passa a não linkar, com o símbolo faltante nomeado no modpost.
- Evidência: <id da rodada na VM / data / saída do gate relevante>
```

## Histórico deste diretório

| Data | Mudança |
|---|---|
| 2026-11-11 | P1 avaliado após o compile-port do M1 → **não necessário**; `p1-needs.txt` registra a evidência e a watch list. Conjunto permanece vazio. |
