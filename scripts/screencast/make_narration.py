#!/usr/bin/env python3
"""
Write the narration scripts for every screencast phase, in English and in
Brazilian Portuguese.

Each segment belongs to one *beat* of the recorded session (see
scripts/screencast/plans/*.json).  The renderer places the segment's audio at
the moment that beat starts on screen, and burns the same text as a subtitle,
so the voice, the caption and the terminal action always agree.

A segment may also carry:
  highlight      - nodes of the lab diagram to light up while it is spoken
  diagram_caption - one-line caption shown inside the diagram panel
"""
import json
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "narration")
os.makedirs(OUT, exist_ok=True)

EN_VOICE = "English_ConfidentWoman"
PT_VOICE = "Portuguese_ConfidentWoman"

PHASES = {}

# ------------------------------------------------------------------ 00 ----
PHASES["00-ambiente"] = {
    "en": {
        "title": "The lab environment",
        "subtitle": "Four Alpine VMs under libvirt, running a kernel built from source",
        "segments": [
            {"beat": "b1", "text": "Welcome to the PF on Linux demonstration. Before we look at the firewall itself, let me show you the environment where everything runs. These are four virtual machines managed by libvirt, all of them Alpine Linux, and the machine you are looking at is the firewall: a kernel we compiled from source, version seven point three point four, with our port of the OpenBSD packet filter built in as a module."},
            {"beat": "b2", "text": "The firewall has four interfaces. The management interface on the virtual network the hypervisor uses to reach us. Then three transit networks: ten point zero point one, where the client lives; ten point zero point two, where the server lives; and ten point zero point three, a second gateway we use to demonstrate routing decisions.", "highlight": ["cli", "pf0", "srv", "gw2"], "diagram_caption": "three transit networks, four interfaces"},
            {"beat": "b3", "text": "The OpenBSD tools are installed on this machine exactly as they ship: the original pfctl, a pflog daemon and our divert tool. The module is not loaded yet, which is where we will start.", "highlight": ["pf0"], "diagram_caption": "pfctl, pflogd and pdivert, straight from OpenBSD"},
            {"beat": "b4", "text": "And from the firewall we can reach the other machines directly. The client is on ten point zero point one point ten, the server on ten point zero point two point twenty, and the second gateway on ten point zero point three point two. Every command in this demonstration is real traffic crossing real interfaces.", "highlight": ["cli", "pf0", "srv", "gw2"], "diagram_caption": "cli, srv and gw2 reachable from pf0"},
        ],
    },
    "pt": {
        "title": "O ambiente de testes",
        "subtitle": "Quatro VMs Alpine sob libvirt, rodando um kernel compilado do fonte",
        "segments": [
            {"beat": "b1", "text": "Bem-vindo à demonstração do PF on Linux. Antes de falar do firewall em si, deixa eu mostrar o ambiente onde tudo acontece. São quatro máquinas virtuais administradas pelo libvirt, todas com Alpine Linux, e a máquina que você está vendo é o firewall: um kernel que compilamos do fonte, versão sete ponto três ponto quatro, com o nosso port do packet filter do OpenBSD compilado dentro como módulo."},
            {"beat": "b2", "text": "O firewall tem quatro interfaces. A interface de gerência, na rede virtual que o hypervisor usa para falar com a gente. E depois três redes de trânsito: dez ponto zero ponto um, onde mora o cliente; dez ponto zero ponto dois, onde mora o servidor; e dez ponto zero ponto três, um segundo gateway que vamos usar para demonstrar decisões de roteamento.", "highlight": ["cli", "pf0", "srv", "gw2"], "diagram_caption": "três redes de transito, quatro interfaces"},
            {"beat": "b3", "text": "As ferramentas do OpenBSD estão instaladas nesta máquina exatamente como são distribuídas: o pfctl original, um daemon de pflog e a nossa ferramenta de divert. O módulo ainda não está carregado, e é por aí que a gente começa.", "highlight": ["pf0"], "diagram_caption": "pfctl, pflogd e pdivert originais do OpenBSD"},
            {"beat": "b4", "text": "E do firewall a gente alcança as outras máquinas diretamente. O cliente está em dez ponto zero ponto um ponto dez, o servidor em dez ponto zero ponto dois ponto vinte, e o segundo gateway em dez ponto zero ponto três ponto dois. Todo comando desta demonstração é tráfego real cruzando interfaces reais.", "highlight": ["cli", "pf0", "srv", "gw2"], "diagram_caption": "cli, srv e gw2 alcançáveis a partir do pf0"},
        ],
    },
}

# ------------------------------------------------------------------ 01 ----
PHASES["01-implementacao"] = {
    "en": {
        "title": "Implementation in the kernel",
        "subtitle": "OpenBSD 8.0 sources plus a Linux compatibility layer equals pf.ko",
        "segments": [
            {"beat": "b1", "text": "This is the heart of the work. We did not rewrite the firewall. We took the OpenBSD eight point zero sources, nearly twenty seven thousand lines of C, and we kept them almost intact. On top of that we wrote a compatibility layer, about seven thousand lines, that translates the BSD kernel interfaces into Linux ones."},
            {"beat": "b2", "text": "The result is a normal Linux kernel module. Here modinfo shows us the license, the description and the vermagic, which tells us exactly which kernel this object was compiled against."},
            {"beat": "b3", "text": "The vermagic line matters: it is the kernel version, the compiler flags and the module signature. Load the wrong one and the kernel refuses it. Everything you will see in the next minutes is running inside that module."},
            {"beat": "b4", "text": "This file is what we load into the firewall. A single artifact, built out of tree, carrying the entire OpenBSD packet filter."},
        ],
    },
    "pt": {
        "title": "Implementação no kernel",
        "subtitle": "Fontes do OpenBSD 8.0 mais uma camada de compatibilidade Linux resultam em pf.ko",
        "segments": [
            {"beat": "b1", "text": "Aqui está o coração do trabalho. A gente não reescreveu o firewall. Pegamos as fontes do OpenBSD oito ponto zero, quase vinte e sete mil linhas de C, e mantivemos praticamente intactas. Em cima disso escrevemos uma camada de compatibilidade, cerca de sete mil linhas, que traduz as interfaces de kernel do BSD para as do Linux."},
            {"beat": "b2", "text": "O resultado é um módulo de kernel Linux comum. Aqui o modinfo mostra a licença, a descrição e o vermagic, que nos diz exatamente com qual kernel este objeto foi compilado."},
            {"beat": "b3", "text": "A linha do vermagic importa: é a versão do kernel, as flags do compilador e a assinatura do módulo. Carregar o arquivo errado e o kernel recusa. Tudo que você vai ver nos próximos minutos roda dentro desse módulo."},
            {"beat": "b4", "text": "Este é o arquivo que carregamos no firewall. Um único artefato, compilado fora da árvore do kernel, carregando o packet filter inteiro do OpenBSD."},
        ],
    },
}

# ------------------------------------------------------------------ 02 ----
PHASES["02-ativacao"] = {
    "en": {
        "title": "Activation",
        "subtitle": "insmod pf.ko, then pfctl talks to it over the OpenBSD ioctl ABI",
        "segments": [
            {"beat": "b1", "text": "First we make sure nothing is loaded, so the activation is honest. No module, no firewall."},
            {"beat": "b2", "text": "Now we insert it. The kernel accepts the module and the log shows our initialization message. This is the OpenBSD engine running inside Linux."},
            {"beat": "b3", "text": "The device appears, owned by root with mode six hundred, exactly like the BSD original, and the module shows up in the list of loaded modules."},
            {"beat": "b4", "text": "pfctl is the original OpenBSD tool. We enable the firewall and ask for its status. The ioctl interface, the statistics table, the rule counters, all of it is the OpenBSD ABI, unchanged."},
            {"beat": "b5", "text": "The ruleset is still empty, and that is exactly right: with no rules, pf blocks everything and returns. The engine is armed and waiting for instructions."},
        ],
    },
    "pt": {
        "title": "Ativação",
        "subtitle": "insmod pf.ko e o pfctl conversa com ele pela ABI de ioctls do OpenBSD",
        "segments": [
            {"beat": "b1", "text": "Primeiro a gente garante que não tem nada carregado, para a ativação ser honesta. Sem módulo, sem firewall."},
            {"beat": "b2", "text": "Agora inserimos. O kernel aceita o módulo e o log mostra a nossa mensagem de inicialização. Este é o motor do OpenBSD rodando dentro do Linux."},
            {"beat": "b3", "text": "O dispositivo aparece, do root, com modo seiscentos, exatamente como no BSD original, e o módulo aparece na lista de módulos carregados."},
            {"beat": "b4", "text": "O pfctl é a ferramenta original do OpenBSD. Habilitamos o firewall e pedimos o status. A interface de ioctl, a tabela de estatísticas, os contadores das regras: tudo é a ABI do OpenBSD, sem alteração."},
            {"beat": "b5", "text": "O ruleset ainda está vazio, e isso está certo: sem regras, o pf bloqueia tudo e devolve. O motor está armado e esperando instruções."},
        ],
    },
}

# ------------------------------------------------------------------ 03 ----
PHASES["03-regras"] = {
    "en": {
        "title": "Basic rules",
        "subtitle": "Tables, anchors, keep state and block drop",
        "segments": [
            {"beat": "b1", "text": "Now we write a real pf dot conf. This is the original OpenBSD syntax: a table with the trusted network, an anchor, and the usual quick rules with keep state. pfctl parses it and the kernel accepts it."},
            {"beat": "b2", "text": "pfctl shows us what the kernel understood: the rules as they were compiled, the list of tables, and the contents of the trusted table."},
            {"beat": "b3", "text": "Let the traffic through. From the client we ping the server, and the firewall creates a state. You can see it in the state table: the five tuple, the timeout, the counters."},
            {"beat": "b4", "text": "Now the same thing with a drop. We replace the pass rule with a block drop rule for the client network, and reload."},
            {"beat": "b5", "text": "The ping is dead. One hundred percent packet loss. And the rule counters show the packets that were evaluated and the ones that were blocked. This is a stateful firewall behaving exactly like the original."},
            {"beat": "b6", "text": "Finally, anchors. We load a ruleset into the anchor called demo, and pfctl lists the rules that live inside it, separately from the main ruleset."},
        ],
    },
    "pt": {
        "title": "Regras básicas",
        "subtitle": "Tabelas, anchors, keep state e block drop",
        "segments": [
            {"beat": "b1", "text": "Agora escrevemos um pf ponto conf de verdade. Esta é a sintaxe original do OpenBSD: uma tabela com a rede confiável, um anchor, e as regras quick de sempre com keep state. O pfctl faz o parsing e o kernel aceita."},
            {"beat": "b2", "text": "O pfctl nos mostra o que o kernel entendeu: as regras como foram compiladas, a lista de tabelas e o conteúdo da tabela trusted."},
            {"beat": "b3", "text": "Deixa o tráfego passar. Do cliente a gente dá ping no servidor, e o firewall cria um estado. Você vê isso na tabela de estados: a tupla de cinco campos, o timeout, os contadores."},
            {"beat": "b4", "text": "Agora a mesma coisa com um drop. Trocamos a regra de pass por uma regra block drop para a rede do cliente, e recarregamos."},
            {"beat": "b5", "text": "O ping morreu. Cem por cento de perda. E os contadores da regra mostram os pacotes avaliados e os bloqueados. Este é um firewall stateful se comportando exatamente como o original."},
            {"beat": "b6", "text": "Por fim, os anchors. Carregamos um ruleset no anchor chamado demo, e o pfctl lista as regras que moram dentro dele, separadamente do ruleset principal."},
        ],
    },
}

# ------------------------------------------------------------------ 04 ----
PHASES["04-roteamento"] = {
    "en": {
        "title": "Routing",
        "subtitle": "route-to forces the next hop, reply-to forces the way back",
        "segments": [
            {"beat": "b1", "text": "Now the routing features. This rule matches traffic from the client to the server and sends it through the second gateway, ten point zero point three point two, instead of the normal path. pfctl shows the rule exactly as the kernel stored it.", "highlight": ["cli", "pf0", "gw2"], "diagram_caption": "route-to: normal path is eth2, forced path is eth3"},
            {"beat": "b2", "text": "To prove where the packet actually went, we capture on both interfaces at the same time: eth three, towards the gateway, and eth two, towards the server."},
            {"beat": "b3", "text": "And now the client pings the server.", "highlight": ["cli", "pf0", "gw2"], "diagram_caption": "the request leaves through eth3, to the gateway"},
            {"beat": "b4", "text": "The evidence is in the captures. The request appears on eth three, on the way to the gateway, and it never appears on eth two. The firewall decided the next hop, not the routing table.", "highlight": ["cli", "pf0", "gw2"], "diagram_caption": "1 request on eth3, 0 on eth2"},
            {"beat": "b5", "text": "The other half of the problem is the way back. reply to forces the reply of a flow to leave through a different gateway than the request came in, which is how you build asymmetric routing.", "highlight": ["cli", "pf0", "gw2"], "diagram_caption": "reply-to: the answer leaves through another gateway"},
            {"beat": "b6", "text": "We capture eth three again and also eth one, the link where the client is."},
            {"beat": "b7", "text": "The replies show up on eth three, on the gateway link, and not on the client link. The request and the reply now take different paths, exactly as commanded.", "highlight": ["cli", "pf0", "gw2"], "diagram_caption": "reply on eth3, nothing on the client link"},
        ],
    },
    "pt": {
        "title": "Roteamento",
        "subtitle": "route-to força o próximo salto, reply-to força o caminho de volta",
        "segments": [
            {"beat": "b1", "text": "Agora as funcionalidades de roteamento. Esta regra casa com o tráfego do cliente para o servidor e o manda pelo segundo gateway, dez ponto zero ponto três ponto dois, em vez do caminho normal. O pfctl mostra a regra exatamente como o kernel guardou.", "highlight": ["cli", "pf0", "gw2"], "diagram_caption": "route-to: o caminho normal e' eth2; o forcado, eth3"},
            {"beat": "b2", "text": "Para provar por onde o pacote realmente passou, capturamos nas duas interfaces ao mesmo tempo: eth três, rumo ao gateway, e eth dois, rumo ao servidor."},
            {"beat": "b3", "text": "E agora o cliente dá ping no servidor.", "highlight": ["cli", "pf0", "gw2"], "diagram_caption": "o pedido sai por eth3, rumo ao gateway"},
            {"beat": "b4", "text": "A prova está nas capturas. O pedido aparece na eth três, indo para o gateway, e nunca aparece na eth dois. Quem decidiu o próximo salto foi o firewall, não a tabela de rotas.", "highlight": ["cli", "pf0", "gw2"], "diagram_caption": "1 pedido em eth3, 0 em eth2"},
            {"beat": "b5", "text": "A outra metade do problema é o caminho de volta. O reply to força a resposta de um fluxo a sair por um gateway diferente do por onde o pedido entrou, que é assim que se constrói roteamento assimétrico.", "highlight": ["cli", "pf0", "gw2"], "diagram_caption": "reply-to: a resposta sai por um gateway diferente"},
            {"beat": "b6", "text": "Capturamos a eth três de novo e também a eth um, o link onde está o cliente."},
            {"beat": "b7", "text": "As respostas aparecem na eth três, no link do gateway, e não no link do cliente. O pedido e a resposta agora seguem caminhos diferentes, exatamente como foi pedido.", "highlight": ["cli", "pf0", "gw2"], "diagram_caption": "resposta em eth3, nada no link do cliente"},
        ],
    },
}

# ------------------------------------------------------------------ 05 ----
PHASES["05-balanceamento"] = {
    "en": {
        "title": "Load balancing",
        "subtitle": "NAT pools in round-robin and source-hash, plus dup-to",
        "segments": [
            {"beat": "b1", "text": "Load balancing in pf is done with pools. This rule translates the source of the client network into a pool of two addresses, the two gateways of the firewall, and selects between them round robin.", "highlight": ["cli", "pf0", "srv"], "diagram_caption": "pool { 10.0.2.1, 10.0.3.1 }: round-robin"},
            {"beat": "b2", "text": "We capture on the interface towards the server to see which source address each packet carries."},
            {"beat": "b3", "text": "Six packets from the client."},
            {"beat": "b4", "text": "And the capture shows the truth: three packets went out with one address, three with the other. The pool is really rotating between its members, and the server sees a balanced load.", "highlight": ["cli", "pf0", "srv"], "diagram_caption": "3 packets with each source address"},
            {"beat": "b5", "text": "Sometimes you do not want rotation. source hash keeps a given origin always mapped to the same pool member, which is what you want when a session has to be sticky.", "highlight": ["cli", "pf0", "srv"], "diagram_caption": "pool { 10.0.2.1, 10.0.3.1 }: source-hash"},
            {"beat": "b6", "text": "Four more packets, and now every single one carries the same source address. The mapping is stable per origin.", "highlight": ["cli", "pf0", "srv"], "diagram_caption": "every packet with the same source"},
            {"beat": "b7", "text": "And dup to is the mirror case: the packet keeps its normal path, and a copy is delivered to the second gateway. We count the requests on both interfaces: one on the normal path, one on the mirrored path. The packet was duplicated, not moved.", "highlight": ["cli", "pf0", "srv", "gw2"], "diagram_caption": "dup-to: the normal path and the mirrored copy"},
        ],
    },
    "pt": {
        "title": "Balanceamento",
        "subtitle": "Pools de NAT em round-robin e source-hash, mais o dup-to",
        "segments": [
            {"beat": "b1", "text": "O balanceamento no pf é feito com pools. Esta regra traduz a origem da rede do cliente para um pool de dois endereços, os dois gateways do firewall, e escolhe entre eles em round robin.", "highlight": ["cli", "pf0", "srv"], "diagram_caption": "pool { 10.0.2.1, 10.0.3.1 }: round-robin"},
            {"beat": "b2", "text": "Capturamos na interface que vai para o servidor para ver qual endereço de origem cada pacote carrega."},
            {"beat": "b3", "text": "Seis pacotes vindo do cliente."},
            {"beat": "b4", "text": "E a captura mostra a verdade: três pacotes saíram com um endereço, três com o outro. O pool está realmente rodando entre os membros, e o servidor enxerga a carga balanceada.", "highlight": ["cli", "pf0", "srv"], "diagram_caption": "3 pacotes com cada endereço de origem"},
            {"beat": "b5", "text": "Às vezes você não quer rotação. O source hash mantém uma origem sempre mapeada para o mesmo membro do pool, que é o que se quer quando a sessão precisa ser sticky.", "highlight": ["cli", "pf0", "srv"], "diagram_caption": "pool { 10.0.2.1, 10.0.3.1 }: source-hash"},
            {"beat": "b6", "text": "Mais quatro pacotes, e agora todos carregam o mesmo endereço de origem. O mapeamento é estável por origem.", "highlight": ["cli", "pf0", "srv"], "diagram_caption": "todos os pacotes com a mesma origem"},
            {"beat": "b7", "text": "E o dup to é o caso espelho: o pacote mantém o caminho normal e uma cópia é entregue no segundo gateway. Contamos os pedidos nas duas interfaces: um no caminho normal, um no caminho espelhado. O pacote foi duplicado, não movido.", "highlight": ["cli", "pf0", "srv", "gw2"], "diagram_caption": "dup-to: o caminho normal e a cópia espelhada"},
        ],
    },
}

# ------------------------------------------------------------------ 06 ----
PHASES["06-qos"] = {
    "en": {
        "title": "Quality of service",
        "subtitle": "OpenBSD 8.0 queues: hfsc classes attached to the interface",
        "segments": [
            {"beat": "b1", "text": "The last piece is quality of service. This is the OpenBSD eight point zero queue syntax: a root class with ten megabits of bandwidth, a leaf class with two megabits as the default, and a rule that steers the traffic from the client to that leaf class."},
            {"beat": "b2", "text": "pfctl shows the queues with their statistics, and the rule set shows the queue attached to the rule."},
            {"beat": "b3", "text": "Now we move real traffic through the queue. On the server we prepare two megabytes and listen on port ninety ninety five."},
            {"beat": "b4", "text": "And the client downloads it, through the firewall, through the class."},
            {"beat": "b5", "text": "Two million bytes transferred, and look at the queue counters: the hfsc class counted the packets and the bytes. The scheduler is in the data path, not just in the configuration."},
        ],
    },
    "pt": {
        "title": "Qualidade de serviço",
        "subtitle": "Filas do OpenBSD 8.0: classes hfsc anexadas à interface",
        "segments": [
            {"beat": "b1", "text": "A última peça é qualidade de serviço. Esta é a sintaxe de filas do OpenBSD oito ponto zero: uma classe raiz com dez megabits de banda, uma classe folha com dois megabits como default, e uma regra que direciona o tráfego do cliente para essa classe folha."},
            {"beat": "b2", "text": "O pfctl mostra as filas com as estatísticas, e o ruleset mostra a fila anexada à regra."},
            {"beat": "b3", "text": "Agora movemos tráfego de verdade pela fila. No servidor preparamos dois megabytes e escutamos na porta noventa e noventa e cinco."},
            {"beat": "b4", "text": "E o cliente baixa isso, passando pelo firewall, passando pela classe."},
            {"beat": "b5", "text": "Dois milhões de bytes transferidos, e olha os contadores das filas: a classe hfsc contou os pacotes e os bytes. O escalonador está no caminho de dados, não só na configuração."},
        ],
    },
}

# the diagram is drawn over these phases
DIAGRAM_PHASES = {"00-ambiente", "04-roteamento", "05-balanceamento"}

for phase, langs in PHASES.items():
    for lang, data in langs.items():
        out = {
            "phase": phase, "lang": lang,
            "voice": EN_VOICE if lang == "en" else PT_VOICE,
            "title": data["title"], "subtitle": data["subtitle"],
            "segments": data["segments"],
        }
        with open(os.path.join(OUT, f"{phase}.{lang}.json"), "w") as f:
            json.dump(out, f, ensure_ascii=False, indent=2)
print(f"wrote {sum(len(v) for v in PHASES.values())} narration scripts to {OUT}")
