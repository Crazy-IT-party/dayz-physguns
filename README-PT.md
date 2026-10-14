# PhysGuns

<img src="https://cdn.simpleicons.org/claude" alt="Claude Code" height="16"> [Feito pelo Claude Code com o plugin `universal-modder`](https://github.com/rehan-remade/universal-modder)

[![made with universal-modder](https://raw.githubusercontent.com/rehan-remade/universal-modder/main/docs/media/made-with-dark.svg)](https://github.com/rehan-remade/universal-modder)

[English](README.md) · [Русский](README-RU.md) · [Español](README-ES.md) · **Português**

<table>
<tr><td align="center"><a href="docs/screenshots/01_carry.jpg"><img src="docs/screenshots/01_carry_thumb.jpg" width="400" alt="Carregando uma caixa"></a><br><sub>Arma de gravidade: carrega qualquer coisa solta</sub></td><td align="center"><a href="docs/screenshots/02_infected.jpg"><img src="docs/screenshots/02_infected_thumb.jpg" width="400" alt="Erguendo um infectado"></a><br><sub>Infectados também</sub></td></tr>
<tr><td align="center"><a href="docs/screenshots/03_portals.jpg"><img src="docs/screenshots/03_portals_thumb.jpg" width="400" alt="Dois portais"></a><br><sub>Arma de portais: um na parede, outro no chão</sub></td><td align="center"><a href="docs/screenshots/04_through.jpg"><img src="docs/screenshots/04_through_thumb.jpg" width="400" alt="Atravessando"></a><br><sub>Entra por um, sai pelo outro</sub></td></tr>
</table>

Um mod de DayZ com duas armas: uma **arma de gravidade** no espírito de Half-Life 2 que agarra, carrega, arremessa e
empurra qualquer coisa solta, e uma **arma de portais** no espírito de Portal que liga dois portais por onde qualquer
coisa passa mantendo a velocidade. As duas são seguradas como um AK sem coronha, funcionam com seis pilhas de 9V e
foram feitas para os admins distribuírem: não fazem parte do loot.

Mod de cliente + servidor `@PhysGuns`. Modelos, texturas, materiais e sons próprios, todos gerados pelos scripts em
`tools/`, sem recursos de terceiros.

## A arma de gravidade (`PG_GravityGun`)

- **Empurrão:** toque no disparo - o que estiver à frente (até 7 m) é jogado longe: itens, infectados, animais,
  jogadores e até veículos (um carro só se mexe um pouco).
- **Agarrar e carregar:** segure o disparo - ela pega o que está na mira, puxando de até 12 m, e segura à sua frente
  numa gaiola faiscante enquanto o gatilho estiver apertado; acompanha o seu olhar. Até 350 kg.
- **Arremessar:** recarregar (ou toque no disparo / botão secundário) enquanto carrega algo.
- **Soltar:** solte o disparo.
- **Criaturas:** infectados, animais e jogadores podem ser erguidos e arremessados; quem bate forte numa parede ou no
  chão sofre dano como numa queda àquela velocidade - um jogador desmaia e pode quebrar as pernas.
- **Efeitos:** um feixe saindo das garras, um brilho, um zumbido que sobe ao balançar o que carrega, e sons próprios de
  agarrar, soltar, empurrar, arremessar e disparo em vazio.

## A arma de portais (`PG_PortalGun`)

- **Dois portais, azul e laranja,** em qualquer superfície fixa - chão, paredes, rochas, construções - a até 250 m; não
  em coisas que se movem. Cada arma tem o seu par; um portal novo de uma cor substitui o anterior.
- **Disparo** abre o portal da cor escolhida; o botão de **modo de disparo** (ou **B**) alterna azul / laranja e a
  lâmpada acesa na arma mostra qual. O botão secundário (botão do meio do mouse, ou prender a respiração mirando)
  sempre abre o laranja.
- **Recarregar** fecha os dois portais.
- **Mirando** (arma erguida) aparece um contorno suave onde o portal vai abrir.
- **Atravessar:** jogadores, infectados, animais e itens que entram num saem pelo outro, virados para onde ele aponta e
  com a velocidade que tinham - caia num portal no chão e saia voando de um na parede.
- **Som:** um disparo, um «whomp» quando um portal abre e um zumbido estático grave em volta de cada portal aberto.
- Um portal é um oval brilhante: o jogo não consegue mostrar a vista através dele.

## Pilhas

Cada arma tem seis encaixes de `9V Battery` e precisa das seis carregadas; com alguma faltando ou descarregada ela só
estala e avisa. Um jogo completo (6 × 50) dura cerca de 300 empurrões, 200 arremessos, 20 minutos carregando ou
150 portais:

| ação | gasta |
|---|---|
| empurrão | 1 |
| arremesso | 1,5 |
| carregar / puxar | 0,25 por segundo (0,5 para começar a agarrar) |
| um portal | 2 (só quando abre) |

Uma arma criada pelo debug spawn do jogo - por exemplo, pelo gerador de objetos do Community Online Tools no modo debug
spawn - vem com seis pilhas novas; criada de outro jeito, vem vazia.

## Estado

As duas se desgastam como qualquer arma - nova, usada, danificada, muito danificada, arruinada - com texturas de dano
próprias: arranhões, ferrugem e fuligem, e as lâmpadas enfraquecendo e apagando.

## Controles

| | teclado + mouse | controle |
|---|---|---|
| empurrão / portal | disparo | disparo |
| agarrar e carregar | segurar disparo | segurar disparo |
| arremessar (gravidade) / fechar portais | recarregar | recarregar |
| azul / laranja (portais) | B ou modo de disparo | modo de disparo |
| secundário: arremessar / laranja | botão do meio | prender a respiração mirando |

O botão do meio e o B ficam nos controles do jogo, em **PhysGuns**, e podem ser trocados.

## Conteúdo

- `mod/PhysGuns` - o addon: config, scripts, modelos (MLOD `.p3d`), texturas (`.paa`), materiais (`.rvmat`), sons
  (`.ogg`) e os atalhos. Empacote num pbo com o prefixo `PhysGuns` e assine com a sua chave.
- `mod/mod.cpp` - nome e autor do mod, para a pasta `@PhysGuns`.
- `tools/` - os geradores: `build_guns.py` (modelos, texturas e materiais de dano das armas), `build_models.py`
  (portais, feixe), `build_sounds.py` (todos os sons, sintetizados), `build_pbo.py` (empacota o addon), `paa.py`.

## Servidor

1. Monte a pasta `@PhysGuns` - `addons/physguns.pbo` (e o seu `.bisign`), `mod.cpp`, `keys/` com o seu `.bikey` -
   coloque na pasta do servidor e carregue no servidor e nos clientes (`-mod=...;@PhysGuns`).
2. Crie as armas como admin: `PG_GravityGun`, `PG_PortalGun` (e `Battery9V` de reserva). Elas não estão no
   `types.xml` e nunca aparecem como loot.

## Compilar

Python 3 com numpy e Pillow, `python-lzo` (para `paa.py`), `ffmpeg` com libvorbis (sons) e
[armake2](https://github.com/KoffeinFlummi/armake2) (compila os materiais):

```
python3 -I tools/build_models.py
python3 -I tools/build_guns.py
python3 -I tools/build_sounds.py
python3 -I tools/build_pbo.py mod/PhysGuns @PhysGuns/addons/physguns.pbo
```

## Limites conhecidos

- Corpos não são tocados: depois que um corpo se acomoda, o servidor não consegue movê-lo de forma que os clientes vejam.
- Um veículo só pode ser empurrado, e pouco.
- Os modelos não têm esqueleto, então o jogo registra um aviso inofensivo «Weapon has no skeleton».

## Licença

Domínio público - [The Unlicense](LICENSE). Repacks, retexturas e edições são bem-vindos. As gravações foram
desenhadas com as fontes DejaVu (licença livre).

Feito pelo Claude Code (Anthropic) com o plugin [universal-modder](https://github.com/rehan-remade/universal-modder),
testado num servidor de testes e no Steam Deck.
