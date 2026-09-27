# Wind Waker Style para o Link-Span

Mod nativo do Link-Span para o Ship of Harkinian (Ocarina of Time) que traz a renderização no estilo Wind Waker do
fork do SoH feito pelo roborich
([`wind-waker-style-cel-shading`](https://github.com/roborich/Shipwright/tree/wind-waker-style-cel-shading), commit
`cbcd3e719d`). São quatro famílias de recursos, mais a luz do Deku Stick:

| Recurso | O que faz | Padrão |
|---|---|---|
| Cel Shading | Reilumina atores e objetos com uma luz-chave dominante e uma rampa toon suave | ligado |
| Light Casting | Luzes pontuais (tochas, fadas, Deku Stick aceso) projetam poças de luz facetadas no cenário | desligado |
| Actor Shadows | Cada ator projeta a própria silhueta no chão real, pela luz-chave do cel shading | desligado |
| Sky | Cúpula em gradiente, nuvens e faixa do horizonte, estrelas cintilando, fiapos de vento; reage ao clima | desligado |

O cenário estático não muda com o Cel Shading. O céu só substitui o céu normal do mundo aberto, nas cenas em que o
próprio jogo o desenha.

## Requisitos

- Ship of Harkinian com o host do Link-Span que oferece os serviços usados pelo mod: `linkspan.oot.render` v3,
  `linkspan.oot.lights` v1, `linkspan.oot.engine`, `linkspan.oot.world` e os hooks de desenho de ator, de luz do
  mundo e do céu. A ABI nativa precisa ser 1.2 ou mais nova, e o layout do host precisa ser o mesmo para o qual a DLL
  foi compilada (o loader recusa um pacote de outro layout).
- Opcional: `linkspan.oot.resources` v1, para pacotes de texturas de nuvem.
- Jogo: só OoT (`games = ["oot"]`). API do Link-Span `>=0.5.0 <0.6.0`.
- Não depende do Unbound nem do Not Enough Items.

## Instalação

Copie o `LinkSpan-WindWakerStyle-<versão>.shipmod` para a pasta `mods/` do jogo e abra o jogo. As opções ficam no
menu (Esc), aba **Mods**, nas quatro seções **Wind Waker Style**. Toda mudança vale na hora, sem reiniciar. A
configuração fica no storage do próprio mod, não nas CVars do jogo.

## Opções

### Cel Shading

| Controle | Padrão | Faixa | Efeito |
|---|---|---|---|
| Enable Cel Shading | ligado | | Reilumina atores e objetos com uma única luz-chave e a rampa toon |
| Ramp Center | 50% | 0–100% | Onde fica a transição do escuro para o claro; mais alto deixa mais da superfície na sombra |
| Ramp Softness | 0,02 | 0,01–0,20 | Largura da transição: baixo dá borda dura, alto dá gradiente suave |
| Highlight Intensity | 60% | 0–200% | Brilho do lado iluminado |
| Shadow Intensity | 60% | 0–100% | Quão escuro fica o lado da sombra (0% = chapado) |
| Point Light Range (x) | 1,5 | 1,0–4,0 | Até onde uma luz pontual continua sendo a chave, em múltiplos do raio dela (só na escolha da chave) |
| Use Navi as a Light Source | ligado | | A Navi pode ser a luz-chave; desligado, a chave fica firme no sol, na lua ou numa tocha |
| Transition Time (s) | 1,0 | 0,1–6,0 | Tempo da luz-chave para ir de uma fonte a outra |
| Reset All to Defaults | | | Volta os sliders acima ao padrão |

### Lights

| Controle | Padrão | Faixa | Efeito |
|---|---|---|---|
| Hide Vanilla Torch Glow | ligado | | Esconde o brilho chapado do jogo sobre as luzes com brilho, enquanto o Light Casting está ligado |
| Improve Flame Flicker | ligado | | Tremulação lenta e orgânica do Wind Waker no lugar da tremulação serrilhada por frame; vale na fonte, então também afeta a luz vanilla e o Cel Shading |
| Flicker Speed (x) | 1,00 | 0,10–3,00 | Com que frequência a chama escolhe um brilho novo |
| Navi's Light Tint | 20% | 0–100% | Tinge a luz da Navi com a cor atual dela (0% = branco) |
| Enable Light Casting | desligado | | Projeta uma poça de luz de cada luz pontual no cenário estático |
| Use Wind Waker default movement | ligado | | Prende o giro e o pulso de tamanho da poça às taxas do Wind Waker |
| Rotation Speed (x) | 1,00 | 0,00–3,00 | Velocidade do giro nos dois eixos (só com o movimento padrão desligado) |
| Size Flicker | 1,00 | 0,00–3,00 | Profundidade do pulso de tamanho (só com o movimento padrão desligado; a Navi fica de fora) |
| Cast Size (x) | 0,50 | 0,10–4,00 | Tamanho da poça, em múltiplos do raio da luz |
| Light Intensity | 20% | 0–200% | Brilho da poça |
| Enable Navi Light Casting | ligado | | A Navi também projeta poça |
| Navi Cast Size (x) | 0,75 | 0,10–4,00 | Tamanho da poça da Navi |
| Navi Light Intensity | 20% | 0–200% | Brilho da poça da Navi |
| Enable Other Fairy Light Casting | desligado | | Fadas que não são a Navi (Kokiri Forest, fadas de cura) emitem luz, que o Cel Shading também usa |
| Other Fairy Cast Size (x) | 0,75 | 0,10–4,00 | Tamanho da poça delas |
| Other Fairy Intensity | 20% | 0–200% | Brilho da poça delas |
| Enable Deku Stick Light Casting | ligado | | Um Deku Stick aceso na mão vira luz de verdade na ponta: pode ser luz-chave, projeta sombra e, com o Light Casting ligado, poça |
| Deku Stick Cast Size (x) | 0,50 | 0,10–4,00 | Tamanho da poça do Deku Stick |
| Reset Sliders to Defaults | | | Volta os sliders desta seção ao padrão |

### Actor Shadows

| Controle | Padrão | Faixa | Efeito |
|---|---|---|---|
| Enable Actor Shadows | desligado | | Sombra com a forma de cada ator, projetada pela luz-chave e moldada ao chão (funciona também com o Cel Shading desligado) |
| Suppress Vanilla Shadows | ligado | | Esconde as sombras de ator do jogo |
| Opacity | 20% | 0–100% | Escuridão do miolo da sombra |
| Edge Softness | 0 | 0–2 | 0 = borda dura; 1 = um degrau mais claro; 2 = rampa mais fina e franja um pouco mais larga |
| Length | 0,20 | 0,00–1,00 | Quanto uma luz baixa pode esticar a sombra |
| Slab Depth | 8 | 5–200 | Até onde, abaixo dos pés, a sombra acompanha o chão |
| Slab Rise | 8 | 0–120 | Até onde, acima dos pés, a sombra sobe em chão que se eleva |
| Render Distance | 550 | 300–5000 | Atores mais longe que isso da câmera ficam sem sombra de forma |
| Reset All to Defaults | | | Volta os sliders acima ao padrão |

### Sky

| Controle | Padrão | Faixa | Efeito |
|---|---|---|---|
| Use Sky | desligado | | Substitui o céu do mundo aberto |
| Horizon Height | -408 | -2000–2000 | Move a linha do horizonte (névoa do gradiente e faixa de nuvens juntas) |
| Horizon Parallax | 75% | 0–150% | Quanto o horizonte afunda quando a câmera sobe (0% acompanha a câmera) |
| Replace Sky Texture | ligado | | Gradiente suave que segue a hora do dia |
| Gradient Brightness | 100% | 50–150% | Brilho geral do gradiente |
| Enable Clouds | ligado | | Nuvens à deriva e a faixa do horizonte |
| Cloud Opacity | 85% | 0–100% | Opacidade das nuvens |
| Coverage | 30% | 0–100% | De nuvens esparsas a céu fechado; o mau tempo aumenta sozinho |
| Drift Speed (x) | 1,0 | 0,0–4,0 | Velocidade das nuvens no vento; a tempestade sopra mais rápido |
| Enable Stars | ligado | | Céu estrelado à noite, surgindo ao anoitecer e sumindo ao amanhecer |
| Star Count | 1000 | 50–1000 | Estrelas na noite cheia |
| Star Brightness | 100% | 0–200% | Brilho das estrelas |
| Twinkle Speed (x) | 1,0 | 0,1–5,0 | Ritmo da cintilação (1x é cerca de dez segundos por ciclo) |
| Enable Wind Wisps | ligado | | Fiapos brancos de vento, às vezes dando uma volta completa |
| Wisp Amount (x) | 1,0 | 0,5–10,0 | Quantos fiapos seguem o vento |
| Wisp Speed (x) | 1,00 | 0,25–1,50 | Velocidade dos fiapos |

## Ferramentas de debug

| Controle | Seção | Efeito |
|---|---|---|
| Light Source Viewer | Cel Shading | Um raio de cada ator para cada luz candidata (na cor da luz, mais longo quanto mais forte), um anel ciano no alcance de cada luz pontual e uma agulha magenta na chave escolhida |
| Highlight Lit Objects | Cel Shading | Objetos reiluminados desenhados em branco chapado (luz) e preto (sombra) |
| Show Light Spheres | Lights | A icosfera facetada usada em cada poça de luz |
| Show Shadow Volume | Actor Shadows | Os volumes de sombra em 3D, translúcidos |
| Split-Screen Compare | Sky | O céu do mod na metade esquerda e o céu original na direita |

A hotkey `ww_style_stats` (hotkeys do Link-Span) grava os contadores do mod no log.

## Pacotes de textura de nuvem

As nuvens usam cinco texturas. O mod as gera no carregamento, com um port byte a byte do script gerador do fork,
então o pacote não leva archive. Um pacote de texturas pode trocá-las: um `.o2r` em `mods/` com arquivos nestes
caminhos vence as texturas geradas.

| Caminho | Tamanho padrão | Limites |
|---|---|---|
| `textures/wind-waker/clouds/cloudtx_01` | 64×64 | potência de dois, até 512×512 |
| `textures/wind-waker/clouds/cloudtx_02` | 64×64 | potência de dois, até 512×512 |
| `textures/wind-waker/clouds/cloudtx_03` | 64×64 | potência de dois, até 512×512 |
| `textures/wind-waker/clouds/cloud_mae` | 256×64 | potência de dois, até 256 de largura e 512 de altura |
| `textures/wind-waker/clouds/cloud_naka` | 256×64 | potência de dois, até 256 de largura e 512 de altura |

- **Formato:** recurso de textura do libultraship (OTEX), RGBA32, até 4 MiB cada. Um arquivo que falha na conferência
  é ignorado, e a textura gerada continua.
- **Nuvens:** cada nuvem à deriva sobrepõe as três imagens `cloudtx_*`, levemente deslocadas; faça três formas
  diferentes.
- **Faixas do horizonte:** `cloud_mae` é a camada da frente, com vãos entre os grupos; `cloud_naka` é o banco
  contínuo do fundo. Precisam emendar da esquerda para a direita, e a borda de baixo fica na linha do horizonte.
- **Cor:** a transparência é a forma da nuvem. Deixe a parte visível quase branca, porque o jogo tinge as nuvens pela
  hora e pelo clima.
- **Quando vale:** o mod lê o pacote quando uma cena carrega. Pôr ou tirar um pacote vale na próxima carga de cena.

## Diferenças em relação ao fork

- **Texturas de nuvem:** geradas na DLL, em vez de viajarem no `soh.o2r`. Os caminhos de substituição são os do fork.
- **Menu e storage:** menu e storage por mod do Link-Span, no lugar do menu do SoH e das CVars
  `gEnhancements.Graphics.*`. A configuração antiga do fork não é importada.
- **Primitivas do renderer:** as primitivas de toon, stencil e sombra vêm do host do Link-Span, não de um renderer
  modificado no mod. O mod guarda só a política: qual luz é a chave, quem fica de fora, o visual.
- **Navi e fadas:** a luz da Navi e a das outras fadas chegam por um hook do host, sem mexer no ator da fada.
- **Receptores de sombra:** a lista de receptores do host é por tipo de ator, então o portão do cemitério
  (`BG_HAKA_GATE`) recebe sombra inteiro, onde o fork aceitava só o piso e a estátua.

## Créditos e licença

Veja o [`NOTICE.md`](./NOTICE.md).
